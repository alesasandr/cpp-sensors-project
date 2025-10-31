#include "sensors/clickhouse_pool.hpp"
#include "sensors/http_server.hpp"
#include "sensors/threadsafe_queue.hpp"
#include "sensors/types.hpp"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>


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
  return c;
}

int main(int argc, char **argv) {
  std::string cfg_path = "server.json";
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--config" && i + 1 < argc)
      cfg_path = argv[++i];
  }

  auto cfg = load_config(cfg_path);

  sensors::ThreadSafeQueue<sensors::EnqueuedTask> queue(cfg.queue_capacity);

  boost::asio::io_context ioc;
  sensors::HttpServer server(ioc, cfg, queue);
  sensors::ClickHousePool chpool(cfg, queue);

  server.run();
  chpool.start();

  std::vector<std::unique_ptr<boost::thread>> threads;
  threads.reserve(cfg.http_threads);
  for (std::size_t i = 0; i < cfg.http_threads; ++i) {
    threads.emplace_back(
        std::make_unique<boost::thread>([&ioc] { ioc.run(); }));
  }

  std::cout << "Server listening on " << cfg.host << ":" << cfg.port
            << std::endl;
  std::cout << "Press Ctrl+C to stop" << std::endl;

  // Простейшая обработка Ctrl+C
  boost::asio::signal_set signals(ioc, SIGINT, SIGTERM);
  signals.async_wait([&](auto, auto) {
    ioc.stop();
    queue.stop();
  });

  for (auto &t : threads)
    t->join();
  chpool.stop();
  return 0;
}
