#include "sensors/redis_client.hpp"

#include <sstream>
#include <sw/redis++/redis++.h>

namespace sensors {

using sw::redis::Redis;

RedisClient::RedisClient(const RedisConfig &cfg) {
  if (!cfg.redis_enabled) {
    enabled_ = false;
    return;
  }

  try {
    std::ostringstream uri;
    uri << "tcp://" << cfg.redis_host << ":" << cfg.redis_port;

    redis_ = std::make_unique<Redis>(uri.str());
    redis_->ping(); // проверяем, что соединение живое
    enabled_ = true;
  } catch (...) {
    enabled_ = false;
    redis_.reset();
  }
}

RedisClient::~RedisClient() = default;

void RedisClient::save_metric(const std::string &sensor_id,
                              const std::string &key, double value,
                              std::int64_t ts) {
  if (!enabled_ || !redis_) {
    return;
  }

  try {
    const std::string redis_key = "sensor:" + sensor_id;
    const std::string field_value = key;
    const std::string field_ts = key + ":ts";

    redis_->hset(redis_key, field_value, std::to_string(value));
    redis_->hset(redis_key, field_ts, std::to_string(ts));
  } catch (...) {
    // на первом шаге просто молчим при ошибках Redis
  }
}

} // namespace sensors
