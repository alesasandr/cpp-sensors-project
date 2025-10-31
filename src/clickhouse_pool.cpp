#include "sensors/clickhouse_pool.hpp"
#include <clickhouse/client.h>
#include <boost/thread.hpp>
#include <iostream>

using namespace clickhouse;

namespace sensors {

ClickHousePool::ClickHousePool(const Config& cfg, ThreadSafeQueue<EnqueuedTask>& q)
  : cfg_(cfg), queue_(q) {}

ClickHousePool::~ClickHousePool() { stop(); }

void ClickHousePool::start() {
  if (running_.exchange(true)) return;
  workers_.reserve(cfg_.ch_pool_size);
  for (std::size_t i = 0; i < cfg_.ch_pool_size; ++i) {
    workers_.emplace_back(std::make_unique<boost::thread>(
      [this]{ worker_loop(); }
    ));
  }
}

void ClickHousePool::stop() {
  if (!running_.exchange(false)) return;
  queue_.stop();
  for (auto& w : workers_) if (w && w->joinable()) w->join();
  workers_.clear();
}

void ClickHousePool::worker_loop() {
  ClientOptions opts;
  opts.SetHost(cfg_.ch_host);
  // перед SetPort – проверка диапазона
  if (cfg_.ch_port < 0 || cfg_.ch_port > 65535) {
      throw std::runtime_error("ClickHouse port is out of range (0..65535)");
  }
  opts.SetPort(cfg_.ch_port);
  opts.SetUser(cfg_.ch_user);
  opts.SetPassword(cfg_.ch_password);
  opts.SetDefaultDatabase(cfg_.ch_database);

  Client client(opts);

  const std::string table = cfg_.ch_table;

  while (running_) {
    auto item = queue_.pop();
    if (!item.has_value()) {
      if (!running_) break;
      continue;
    }
    const auto& t = *item;

    // Преобразуем в строчный batch (узкий пример: key/value)
    // Для производительности лучше использовать вставки ColumnString/ColumnFloat64
    try {
      // Создадим колонки
      auto col_sensor = std::make_shared<ColumnString>();
      auto col_ts     = std::make_shared<ColumnDateTime>();
      auto col_key    = std::make_shared<ColumnString>();
      auto col_value  = std::make_shared<ColumnFloat64>();

      for (const auto& [k, v] : t.kv) {
        col_sensor->Append(t.sensor_id);
        col_ts->Append(static_cast<std::time_t>(t.ts));
        col_key->Append(k);
        col_value->Append(v);
      }

      Block block;
      block.AppendColumn("sensor_id", col_sensor);
      block.AppendColumn("ts",        col_ts);
      block.AppendColumn("key",       col_key);
      block.AppendColumn("value",     col_value);

      client.Insert(table, block);

      if (t.reply && t.reply->respond) {
        t.reply->respond(200, R"({"status":"ok"})");
      }
    } catch (const std::exception& ex) {
      if (t.reply && t.reply->respond) {
        t.reply->respond(500, std::string(R"({"status":"error","msg":")") + ex.what() + "\"}");
      }
    }
  }
}

} // namespace sensors
