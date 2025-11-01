#include "sensors/clickhouse_pool.hpp"
#include "sensors/time_utils.hpp"
#include <atomic> // добавлено для метрик
#include <boost/thread.hpp>
#include <chrono>
#include <clickhouse/client.h>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>


using namespace clickhouse;
using sensors::to_time_t_seconds;

namespace sensors {

// глобальные счётчики метрик, определяются в одном .cpp (например,
// src/metrics_export.cpp)
extern std::atomic<unsigned long long> g_total_received; // counter
extern std::atomic<unsigned long long> g_queue_size;     // gauge

namespace {

inline void log_err(const char *tag, const std::string &msg) {
  std::fprintf(stderr, "[%s] %s\n", tag, msg.c_str());
  std::fflush(stderr);
}

inline void log_dbg(const char *tag, const std::string &msg) {
  std::fprintf(stderr, "[%s] %s\n", tag, msg.c_str());
  std::fflush(stderr);
}

inline void sleep_with_checks(std::atomic_bool &running,
                              std::chrono::milliseconds dur) {
  const auto step = std::chrono::milliseconds(100);
  auto left = dur;
  while (running && left.count() > 0) {
    std::this_thread::sleep_for(std::min(step, left));
    left -= step;
  }
}

} // namespace

ClickHousePool::ClickHousePool(const Config &cfg,
                               ThreadSafeQueue<EnqueuedTask> &q)
    : cfg_(cfg), queue_(q) {}

ClickHousePool::~ClickHousePool() { stop(); }

void ClickHousePool::start() {
  if (running_.exchange(true))
    return;

  const std::size_t n = cfg_.ch_pool_size ? cfg_.ch_pool_size : 1;
  workers_.reserve(n);

  for (std::size_t i = 0; i < n; ++i) {
    workers_.emplace_back(std::make_unique<boost::thread>([this] {
      try {
        worker_loop();
      } catch (const std::exception &e) {
        log_err("ERR", std::string("ClickHouse worker fatal: ") + e.what());
      } catch (...) {
        log_err("ERR", "ClickHouse worker fatal: unknown exception");
      }
    }));
  }
}

void ClickHousePool::stop() {
  if (!running_.exchange(false))
    return;

  queue_.stop();

  for (auto &w : workers_) {
    if (w && w->joinable())
      w->join();
  }
  workers_.clear();
}

void ClickHousePool::worker_loop() {
  if (cfg_.ch_port < 0 || cfg_.ch_port > 65535) {
    throw std::runtime_error("ClickHouse port is out of range (0..65535)");
  }

  const std::string table = cfg_.ch_table;
  const std::chrono::milliseconds connect_retry_delay(3000);

  while (running_) {
    try {
      ClientOptions opts;
      opts.SetHost(cfg_.ch_host)
          .SetPort(static_cast<uint16_t>(cfg_.ch_port))
          .SetDefaultDatabase(cfg_.ch_database);

      if (!cfg_.ch_user.empty())
        opts.SetUser(cfg_.ch_user);
      if (!cfg_.ch_password.empty())
        opts.SetPassword(cfg_.ch_password);

      Client client(opts);

      // Ранний ping
      try {
        client.Execute("SELECT 1");
        log_dbg("CH", "connected: host=" + cfg_.ch_host +
                          " port=" + std::to_string(cfg_.ch_port) + " db=" +
                          (cfg_.ch_database.empty() ? "(default)"
                                                    : cfg_.ch_database));
      } catch (const std::exception &ping_ex) {
        throw std::runtime_error(std::string("handshake failed: ") +
                                 ping_ex.what());
      }

      // Основной цикл обработки очереди
      while (running_) {
        auto item_opt = queue_.pop();
        if (!item_opt.has_value()) {
          if (!running_)
            break;
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
          continue;
        }

        // элемент извлечён из очереди → уменьшаем gauge
        g_queue_size.fetch_sub(1ULL, std::memory_order_relaxed);

        const auto &t = *item_opt;

        try {
          auto col_sensor = std::make_shared<ColumnString>();
          auto col_ts = std::make_shared<ColumnDateTime>(); // секунды (UTC)
          auto col_key = std::make_shared<ColumnString>();
          auto col_value = std::make_shared<ColumnFloat64>();

          for (const auto &kv : t.kv) {
            col_sensor->Append(t.sensor_id);
            col_ts->Append(to_time_t_seconds(static_cast<int64_t>(t.ts)));
            col_key->Append(kv.first);
            col_value->Append(kv.second);
          }

          Block block;
          block.AppendColumn("sensor_id", col_sensor);
          block.AppendColumn("ts", col_ts);
          block.AppendColumn("key", col_key);
          block.AppendColumn("value", col_value);

          client.Insert(table, block);

          // успешная вставка: увеличиваем counter на количество пар key/value
          g_total_received.fetch_add(
              static_cast<unsigned long long>(t.kv.size()),
              std::memory_order_relaxed);

          if (t.reply && t.reply->respond) {
            t.reply->respond(200, R"({"status":"ok"})");
          }
        } catch (const std::exception &ex) {
          const std::string msg = std::string("insert error: ") + ex.what();
          if (t.reply && t.reply->respond) {
            t.reply->respond(500, std::string(R"({"status":"error","msg":")") +
                                      msg + "\"}");
          } else {
            log_err("CH", msg);
          }
        }
      }

    } catch (const std::exception &e) {
      log_err("CH",
              std::string("connection/loop error: ") + e.what() +
                  " (host=" + cfg_.ch_host +
                  " port=" + std::to_string(cfg_.ch_port) + " db=" +
                  (cfg_.ch_database.empty() ? "(default)" : cfg_.ch_database) +
                  ") — retry in " +
                  std::to_string(connect_retry_delay.count()) + "ms");
      sleep_with_checks(running_, connect_retry_delay);
      continue;
    } catch (...) {
      log_err("CH", "connection/loop error: unknown — retry in 3000ms");
      sleep_with_checks(running_, connect_retry_delay);
      continue;
    }
  }
}

#ifdef SENSORS_CH_SELFTEST
// Сборка: добавить -DSENSORS_CH_SELFTEST и залинковать с clickhouse-cpp
// Запуск: ch_selftest 127.0.0.1 9000 default chpass sensors metrics
#include <cstdlib>

int main(int argc, char **argv) {
  if (argc < 7) {
    std::fprintf(
        stderr,
        "Usage: %s <host> <port> <user> <password> <database> <table>\n",
        argv[0]);
    return 2;
  }

  const std::string host = argv[1];
  const uint16_t port = static_cast<uint16_t>(std::stoi(argv[2]));
  const std::string user = argv[3];
  const std::string pass = argv[4];
  const std::string db = argv[5];
  const std::string tbl = argv[6];

  try {
    ClientOptions opts;
    opts.SetHost(host)
        .SetPort(port)
        .SetUser(user)
        .SetPassword(pass)
        .SetDefaultDatabase(db);
    Client client(opts);

    client.Execute("SELECT 1");

    auto col_sensor = std::make_shared<ColumnString>();
    auto col_ts = std::make_shared<ColumnDateTime>();
    auto col_key = std::make_shared<ColumnString>();
    auto col_value = std::make_shared<ColumnFloat64>();

    col_sensor->Append("probe");
    col_ts->Append(std::time(nullptr));
    col_key->Append("selftest");
    col_value->Append(1.0);

    Block block;
    block.AppendColumn("sensor_id", col_sensor);
    block.AppendColumn("ts", col_ts);
    block.AppendColumn("key", col_key);
    block.AppendColumn("value", col_value);

    client.Insert(tbl, block);

    std::fprintf(stdout, "Selftest OK: inserted 1 row into %s.%s\n", db.c_str(),
                 tbl.c_str());
    return 0;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Selftest FAIL: %s\n", e.what());
    return 1;
  }
}
#endif // SENSORS_CH_SELFTEST

} // namespace sensors
