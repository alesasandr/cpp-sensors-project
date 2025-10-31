#pragma once
#include "types.hpp"
#include "request_context.hpp"
#include "threadsafe_queue.hpp"
#include <memory>
#include <vector>
#include <atomic>

namespace sensors {

class ClickHousePool {
public:
  ClickHousePool(const Config& cfg, ThreadSafeQueue<EnqueuedTask>& queue);
  ~ClickHousePool();

  void start();
  void stop();

private:
  void worker_loop();

  const Config cfg_;
  ThreadSafeQueue<EnqueuedTask>& queue_;
  std::vector<std::unique_ptr<boost::thread>> workers_;
  std::atomic<bool> running_{false};
};

} // namespace sensors
