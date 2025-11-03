#include "sensors/clickhouse_pool.hpp"
#include "sensors/http_server.hpp"
#include "sensors/threadsafe_queue.hpp"
#include "sensors/types.hpp"
#include <csignal>
#include <exception>
#include <fstream>
#include <iostream>

#include <nlohmann/json.hpp>

static void term_handler() {
  try {
    throw; // поймать текущее исключение
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[FATAL] std::terminate: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "[FATAL] std::terminate: unknown exception\n");
  }
  std::fflush(stderr);
  std::abort();
}

using json = nlohmann::json;

static sensors::Config load_config(const std::string &path) {
  sensors::Config c;
  std::ifstream f(path);
  if (!f)
    return c;
  json j;
  f >> j;
  auto get = [&](auto key, auto def) {
    return j.contains(key) ? j[key].get<std::decay_t<decltype(def)>>() : def;
  };

  c.host = get("host", c.host);
  c.port = static_cast<unsigned short>(get("port", (int)c.port));
  c.http_threads = get("http_threads", c.http_threads);
  c.ch_pool_size = get("ch_pool_size", c.ch_pool_size);
  c.queue_capacity = get("queue_capacity", c.queue_capacity);
  c.write_timeout_ms = get("write_timeout_ms", c.write_timeout_ms);

  c.ch_host = get("ch_host", c.ch_host);
  c.ch_port = get("ch_port", c.ch_port);
  c.ch_user = get("ch_user", c.ch_user);
  c.ch_password = get("ch_password", c.ch_password);
  c.ch_database = get("ch_database", c.ch_database);
  c.ch_table = get("ch_table", c.ch_table);
  c.redis_enabled = get("redis_enabled", c.redis_enabled);
  c.redis_host = get("redis_host", c.redis_host);
  c.redis_port = get("redis_port", c.redis_port);

  return c;
}

int main(int argc, char **argv) {
  std::set_terminate(term_handler);

  std::string cfg_path = "server.json";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc)
      cfg_path = argv[++i];
  }

  auto cfg = load_config(cfg_path);

  sensors::ThreadSafeQueue<sensors::EnqueuedTask> queue(cfg.queue_capacity);

  boost::asio::io_context ioc;

  // Держим io_context живым всегда, пока сами не отпустим
  auto guard = boost::asio::make_work_guard(ioc.get_executor());

  // Периодический таймер, чтобы в ioc ВСЕГДА была хотя бы одна async-операция
  auto ticker = std::make_shared<boost::asio::steady_timer>(ioc);
  std::function<void()> tick = [ticker, &tick]() {
    ticker->expires_after(std::chrono::seconds(5));
    ticker->async_wait([&tick](const boost::system::error_code &ec) {
      if (!ec)
        tick(); // перевооружаемся бесконечно
    });
  };
  tick();

  sensors::HttpServer server(ioc, cfg, queue);
  sensors::ClickHousePool chpool(cfg, queue);

  try {
    server.run();   // должен поставить async_accept
    chpool.start(); // поднимает воркеры пула
  } catch (const std::exception &e) {
    std::cerr << "[FATAL] startup error: " << e.what() << std::endl;
    return 1;
  }

  const std::size_t n_threads = std::max<std::size_t>(1, cfg.http_threads);

  std::vector<std::unique_ptr<boost::thread>> threads;
  threads.reserve(n_threads > 0 ? n_threads - 1 : 0);

  for (std::size_t i = 0; i + 1 < n_threads; ++i) {
    threads.emplace_back(std::make_unique<boost::thread>([&ioc] {
      std::cout << "[DBG] worker thread: ioc.run() enter\n";
      ioc.run();
      std::cout << "[DBG] worker thread: ioc.run() exit\n";
    }));
  }

  std::cout << "Server listening on " << cfg.host << ":" << cfg.port << "\n"
            << "Press Ctrl+C to stop\n";

  boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
  signals.async_wait([&](const boost::system::error_code &, int) {
    std::cout << "[SIG] stopping...\n";
    queue.stop();
    guard.reset(); // отпускаем «несгораемую» работу
    ioc.stop();    // будим все потоки, чтобы они вышли из run()
  });

  // Главный поток тоже крутит ioc
  std::cout << "[DBG] main thread: ioc.run() enter\n";
  ioc.run();
  std::cout << "[DBG] main thread: ioc.run() exit\n";

  for (auto &t : threads)
    t->join();
  chpool.stop();
  return 0;
}
