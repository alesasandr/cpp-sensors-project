// include/sensors/http_server.hpp
#pragma once
#include "types.hpp"
#include "request_context.hpp"
#include "threadsafe_queue.hpp"
#include <boost/asio.hpp>
#include <boost/thread.hpp>
#include <atomic>
#include <memory>
#include <vector>

namespace sensors {

class HttpServer {
public:
  HttpServer(boost::asio::io_context& ioc, const Config& cfg,
             ThreadSafeQueue<EnqueuedTask>& queue);

  void run();
  void stop();

private:
  struct Session;
  void do_accept();

  boost::asio::io_context& ioc_;
  const Config cfg_;
  ThreadSafeQueue<EnqueuedTask>& queue_;
  boost::asio::ip::tcp::acceptor acceptor_;
  boost::asio::ip::tcp::socket socket_;
  std::vector<std::unique_ptr<boost::thread>> threads_;
  std::atomic<bool> running_{false};
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
};

} // namespace sensors
