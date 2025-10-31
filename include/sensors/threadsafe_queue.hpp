#pragma once
#include <boost/thread.hpp>
#include <deque>
#include <optional>

namespace sensors {

template <class T>
class ThreadSafeQueue {
public:
  explicit ThreadSafeQueue(std::size_t cap) : capacity_(cap) {}

  // блокирующая попытка положить
  bool push(const T& v) {
    boost::unique_lock<boost::mutex> lk(m_);
    cv_not_full_.wait(lk, [&]{ return stopped_ || q_.size() < capacity_; });
    if (stopped_) return false;
    q_.push_back(v);
    cv_not_empty_.notify_one();
    return true;
  }

  // неблокирующая попытка
  bool try_push(const T& v) {
    boost::unique_lock<boost::mutex> lk(m_);
    if (stopped_ || q_.size() >= capacity_) return false;
    q_.push_back(v);
    cv_not_empty_.notify_one();
    return true;
  }

  // блокирующее извлечение
  std::optional<T> pop() {
    boost::unique_lock<boost::mutex> lk(m_);
    cv_not_empty_.wait(lk, [&]{ return stopped_ || !q_.empty(); });
    if (q_.empty()) return std::nullopt;
    T v = std::move(q_.front());
    q_.pop_front();
    cv_not_full_.notify_one();
    return v;
  }

  void stop() {
    {
      boost::lock_guard<boost::mutex> lk(m_);
      stopped_ = true;
    }
    cv_not_empty_.notify_all();
    cv_not_full_.notify_all();
  }

private:
  std::deque<T> q_;
  std::size_t capacity_;
  bool stopped_{false};
  boost::mutex m_;
  boost::condition_variable_any cv_not_empty_;
  boost::condition_variable_any cv_not_full_;
};

} // namespace sensors
