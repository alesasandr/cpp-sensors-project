#pragma once
#include <cstdint>
#include <ctime>

namespace sensors {

// Приводит timestamp к секундам (UTC).
// Если приходит миллисекунды/микросекунды — конвертируем эвристикой.
inline std::time_t to_time_t_seconds(int64_t ts) {
  if (ts > 10'000'000'000LL) {
    if (ts > 10'000'000'000'000LL) {
      return static_cast<std::time_t>(ts / 1'000'000); // микросекунды → секунды
    }
    return static_cast<std::time_t>(ts / 1'000); // миллисекунды → секунды
  }
  return static_cast<std::time_t>(ts); // секунды
}

} // namespace sensors
