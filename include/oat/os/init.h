// oat/os/init.h - start-up and error text.
#pragma once

#include "../base.h"

namespace oat::os {

// Routes SDL's allocations through oat::heap(), so they show up in
// heap_stats(). Call it once, before any other SDL call: SDL cannot switch
// allocators after it has allocated, and init() returns InvalidArgument if it
// already has. Safe to call again after it has succeeded.
//
// The rest of oat::os works without init(); only the memory tracking needs it.
[[nodiscard]] Result<void> init();

// SDL reports failures as text, with no code. After an oat::os function
// returns an error, this is SDL's message for it. The Error code oat returns
// is its best reading of the situation; this text is what actually happened. Per thread, and valid until
// the calling thread's next oat::os call.
Str last_error_text();

} // namespace oat::os
