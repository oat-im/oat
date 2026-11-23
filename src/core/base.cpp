#include <oat/base.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace oat {

using AssertHandler = void (*)(const char* expr, const char* file, int line);

static void default_assert_handler(const char* expr, const char* file, int line) {
  std::fprintf(stderr, "%s:%d: oat assert failed: %s\n", file, line, expr);
  std::fflush(stderr);
}

static std::atomic<AssertHandler> g_assert_handler{default_assert_handler};

void set_assert_handler(AssertHandler handler) {
  g_assert_handler.store(handler ? handler : default_assert_handler);
}

void assert_fail(const char* expr, const char* file, int line) {
  g_assert_handler.load()(expr, file, line);
  std::abort();
}

const char* error_name(Error e) {
  switch (e) {
    case Error::OutOfRange: return "OutOfRange";
    case Error::InvalidArgument: return "InvalidArgument";
    case Error::NotFound: return "NotFound";
    case Error::AlreadyExists: return "AlreadyExists";
    case Error::PermissionDenied: return "PermissionDenied";
    case Error::Io: return "Io";
    case Error::EndOfStream: return "EndOfStream";
    case Error::Timeout: return "Timeout";
    case Error::WouldBlock: return "WouldBlock";
    case Error::Unsupported: return "Unsupported";
    case Error::Unknown: return "Unknown";
  }
  return "Unknown";
}

} // namespace oat
