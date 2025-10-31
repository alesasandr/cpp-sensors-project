#pragma once
#include <string>
#include <unordered_map>

namespace sensors {

struct IngestRequest {
  std::string sensor_id;
  std::int64_t ts; // unix epoch seconds
  std::unordered_map<std::string, double> metrics;
  // correlation id для обратной связи в ответ
  std::string request_id;
};

struct Config {
  std::string host = "0.0.0.0";
  unsigned short port = 8080;
  std::size_t http_threads = 4;
  std::size_t ch_pool_size = 4;
  std::size_t queue_capacity = 100000;
  int write_timeout_ms = 200; // сколько ждём, чтобы ответить 200 OK
  // ClickHouse
  std::string ch_host = "127.0.0.1";
  int ch_port = 9000;
  std::string ch_user = "default";
  std::string ch_password = "";
  std::string ch_database = "sensors";
  std::string ch_table = "metrics";
};

} // namespace sensors
