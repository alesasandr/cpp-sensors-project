#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace sw {
namespace redis {
class Redis;
} // namespace redis
} // namespace sw

namespace sensors {

struct RedisConfig {
  bool redis_enabled{false};
  std::string redis_host{"127.0.0.1"};
  int redis_port{6379};
};

class RedisClient {
public:
  explicit RedisClient(const RedisConfig &cfg);
  ~RedisClient();

  bool is_enabled() const noexcept { return enabled_; }

  void save_metric(const std::string &sensor_id, const std::string &key,
                   double value, std::int64_t ts);

private:
  bool enabled_{false};
  std::unique_ptr<sw::redis::Redis> redis_;
};

} // namespace sensors
