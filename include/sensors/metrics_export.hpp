#pragma once
#include <atomic>
#include <cstddef>

namespace sensors {
// сколько метрик успешно записали в ClickHouse (counter)
extern std::atomic<unsigned long long> g_total_received;
// текущий размер очереди задач (gauge, приблизительный)
extern std::atomic<unsigned long long> g_queue_size;
} // namespace sensors
