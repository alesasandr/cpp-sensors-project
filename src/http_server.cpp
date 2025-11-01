// src/http_server.cpp
#include "sensors/http_server.hpp"
#include <boost/beast.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
using tcp = net::ip::tcp;
using json = nlohmann::json;

namespace sensors {

struct HttpServer::Session
    : public std::enable_shared_from_this<HttpServer::Session> {
  tcp::socket socket;
  ThreadSafeQueue<EnqueuedTask> &queue;
  const Config cfg;

  beast::flat_buffer buffer;
  http::request<http::string_body> req;
  http::response<http::string_body> res;

  // strand от executora сокета — корректный тип под any_io_executor
  net::strand<net::any_io_executor> strand;

  explicit Session(tcp::socket s, ThreadSafeQueue<EnqueuedTask> &q,
                   const Config &c)
      : socket(std::move(s)), queue(q), cfg(c),
        strand(net::make_strand(socket.get_executor())) {}

  void run() { read_request(); }

  void read_request() {
    auto self = shared_from_this();
    http::async_read(
        socket, buffer, req,
        net::bind_executor(strand, [self](beast::error_code ec, std::size_t) {
          if (!ec)
            self->handle_request();
        }));
  }

  static std::string gen_req_id() {
    static thread_local std::mt19937_64 rng{std::random_device{}()};
    std::stringstream ss;
    ss << std::hex << rng();
    return ss.str();
  }

  void handle_request() {
    if (req.method() != http::verb::post || req.target() != "/ingest") {
      write_response(404, R"({"error":"not found"})");
      return;
    }

    IngestRequest in;
    try {
      auto j = json::parse(req.body());
      in.sensor_id = j.at("sensor_id").get<std::string>();
      in.ts = j.at("ts").get<std::int64_t>();
      in.request_id = gen_req_id();
      for (auto &[k, v] : j.at("metrics").items()) {
        in.metrics[k] = v.get<double>();
      }
    } catch (const std::exception &e) {
      write_response(400, std::string(R"({"error":"bad json","msg":")") +
                              e.what() + "\"}");
      return;
    }

    auto reply = std::make_shared<ReplyHandle>();
    reply->respond = [self = shared_from_this()](int code, std::string body) {
      net::dispatch(self->strand,
                    [self, code, body = std::move(body)]() mutable {
                      self->write_response(code, std::move(body));
                    });
    };

    EnqueuedTask task;
    task.request_id = in.request_id;
    task.sensor_id = in.sensor_id;
    task.ts = in.ts;
    for (const auto &[k, v] : in.metrics)
      task.kv.emplace_back(k, v);
    task.reply = reply;

    if (!queue.try_push(task)) {
      write_response(503, R"({"error":"queue full"})");
      return;
    }

    auto self = shared_from_this();
    auto timer =
        std::make_shared<net::steady_timer>(strand.get_inner_executor());
    timer->expires_after(std::chrono::milliseconds(cfg.write_timeout_ms));
    timer->async_wait(net::bind_executor(
        strand, [self, timer](const boost::system::error_code &ec) {
          if (ec)
            return; // отменён — уже ответили
          if (self->socket.is_open()) {
            self->write_response(202, R"({"status":"accepted"})");
          }
        }));
    // Ветка успеха/ошибки из воркера отменит таймер внутри
    // write_response/dispatch
  }

  void write_response(int status, std::string body) {
    res.version(req.version());
    res.keep_alive(false);
    res.result(static_cast<http::status>(status));
    res.set(http::field::content_type, "application/json");
    res.body() = std::move(body);
    res.prepare_payload();

    auto self = shared_from_this();
    http::async_write(
        socket, res,
        net::bind_executor(strand, [self](beast::error_code, std::size_t) {
          beast::error_code ec;
          self->socket.shutdown(tcp::socket::shutdown_send, ec);
        }));
  }
};

HttpServer::HttpServer(net::io_context &ioc, const Config &cfg,
                       ThreadSafeQueue<EnqueuedTask> &queue)
    : ioc_(ioc), cfg_(cfg), queue_(queue), acceptor_(ioc), socket_(ioc),
      work_guard_(net::make_work_guard(ioc_)) {
  tcp::endpoint ep{net::ip::make_address(cfg_.host),
                   static_cast<unsigned short>(cfg_.port)};
  beast::error_code ec;
  acceptor_.open(ep.protocol(), ec);
  if (!ec)
    acceptor_.set_option(net::socket_base::reuse_address(true), ec);
  if (!ec)
    acceptor_.bind(ep, ec);
  if (!ec)
    acceptor_.listen(net::socket_base::max_listen_connections, ec);
}

void HttpServer::run() {
  if (running_.exchange(true))
    return;
  do_accept();
}

void HttpServer::stop() {
  if (!running_.exchange(false))
    return;
  work_guard_.reset(); // отпускаем io_context
  beast::error_code ec;
  acceptor_.close(ec);
}

void HttpServer::do_accept() {
  acceptor_.async_accept(socket_, [this](beast::error_code ec) {
    if (!ec) {
      std::make_shared<Session>(std::move(socket_), queue_, cfg_)->run();
    }
    if (running_)
      do_accept();
  });
}

} // namespace sensors
