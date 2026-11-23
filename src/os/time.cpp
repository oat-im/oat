#include "os_internal.h"

namespace oat::os {

u64 now_ns() { return SDL_GetTicksNS(); }

void sleep_ns(u64 ns) { SDL_DelayNS(ns); }

i64 wall_clock() {
  SDL_Time t = 0;
  if (!SDL_GetCurrentTime(&t)) return 0;
  return t;
}

Result<Calendar> to_calendar(i64 wall_ns, bool local_time) {
  SDL_DateTime dt;
  if (!SDL_TimeToDateTime(wall_ns, &dt, local_time)) return detail::fail(Error::InvalidArgument);
  Calendar c;
  c.year = dt.year;
  c.month = dt.month;
  c.day = dt.day;
  c.hour = dt.hour;
  c.minute = dt.minute;
  c.second = dt.second;
  c.nanosecond = dt.nanosecond;
  c.day_of_week = dt.day_of_week;
  c.utc_offset = dt.utc_offset;
  return c;
}

} // namespace oat::os
