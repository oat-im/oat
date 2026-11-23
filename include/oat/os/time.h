// oat/os/time.h - clocks and sleeping.
#pragma once

#include "../base.h"

namespace oat::os {

// Monotonic nanoseconds since the process started using SDL. For measuring
// intervals; unrelated to the time of day.
u64 now_ns();

void sleep_ns(u64 ns);

// Nanoseconds since the Unix epoch, UTC. Can jump when the system clock is set.
i64 wall_clock();

struct Calendar {
  i32 year;
  i32 month;       // 1-12
  i32 day;         // 1-31
  i32 hour;        // 0-23
  i32 minute;      // 0-59
  i32 second;      // 0-60
  i32 nanosecond;  // 0-999999999
  i32 day_of_week; // 0 is Sunday
  i32 utc_offset;  // seconds east of UTC
};

// Splits a wall_clock() value into calendar fields, in local time or UTC.
[[nodiscard]] Result<Calendar> to_calendar(i64 wall_ns, bool local_time = true);

} // namespace oat::os
