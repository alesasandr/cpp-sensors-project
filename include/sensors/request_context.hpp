#pragma once
#include <functional>
#include <string>
#include <memory>
#include <vector>

namespace sensors {

// Обратный канал к сетевому exe для ответа клиенту
struct ReplyHandle {
  // вызывает write внутри strand/executor соединения
  std::function<void(int http_status, std::string body)> respond;
};

struct EnqueuedTask {
  std::string request_id; // корреляция
  std::string sensor_id;
  std::int64_t ts;
  std::vector<std::pair<std::string, double>> kv; // (key,value)
  std::shared_ptr<ReplyHandle> reply; // может быть nullptr, если ответим 202 сразу
};

} // namespace sensors
