#include <gtest/gtest.h>
#include <sensors/time_utils.hpp>

using sensors::to_time_t_seconds;

TEST(TimeConv, SecondsPassThrough) {
  EXPECT_EQ(to_time_t_seconds(1'730'000'000), 1'730'000'000);
}

TEST(TimeConv, MillisToSeconds) {
  EXPECT_EQ(to_time_t_seconds(1'730'000'000'000), 1'730'000'000);
}

TEST(TimeConv, MicrosToSeconds) {
  EXPECT_EQ(to_time_t_seconds(1'730'000'000'000'000), 1'730'000'000);
}

TEST(TimeConv, BoundaryNear10B) {
  // ровно 10_000_000_000 считаем секундами
  EXPECT_EQ(to_time_t_seconds(10'000'000'000LL), 10'000'000'000LL);
  // +1 — уже эвристика миллисекунд
  EXPECT_EQ(to_time_t_seconds(10'000'000'001LL), 10'000'000'001LL / 1000);
}
