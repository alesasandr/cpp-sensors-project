#include <sensors/metrics_export.hpp>

namespace sensors {
std::atomic<unsigned long long> g_total_received{0ULL};
std::atomic<unsigned long long> g_queue_size{0ULL};
} // namespace sensors
