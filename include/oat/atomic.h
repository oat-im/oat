// oat/atomic.h - Atomic<T>.
#pragma once

#include <atomic>

#include "base.h"

namespace oat {

// std::atomic under oat's name: load(), store(), fetch_add(), exchange(),
// compare_exchange_weak() and the rest work as usual.
template <class T>
using Atomic = std::atomic<T>;

} // namespace oat
