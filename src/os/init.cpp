#include "os_internal.h"

#include <atomic>

namespace oat::os {

namespace detail {

static thread_local char t_error_text[256];

void set_error_text(const char* text) {
  usize n = text ? std::strlen(text) : 0;
  if (n >= sizeof(t_error_text)) n = sizeof(t_error_text) - 1;
  if (n) std::memcpy(t_error_text, text, n);
  t_error_text[n] = '\0';
}

std::unexpected<Error> fail(Error e) {
  set_error_text(SDL_GetError());
  return std::unexpected<Error>(e);
}

std::unexpected<Error> fail(Error e, const char* text) {
  set_error_text(text);
  return std::unexpected<Error>(e);
}

Str copy_path(Str s, Allocator& a) {
  char* p = static_cast<char*>(a.alloc(s.size() + 1, 1));
  for (usize i = 0; i < s.size(); ++i) p[i] = s[i] == '\\' ? '/' : s[i];
  p[s.size()] = '\0';
  return Str(p, s.size());
}

} // namespace detail

Str last_error_text() { return Str(detail::t_error_text); }

// SDL's allocator interface does not pass sizes back to free, and oat's does
// not remember them, so each block carries its size in a 16-byte header.
static constexpr usize SDL_BLOCK_HEADER = 16;

static void* SDLCALL sdl_malloc(size_t size) {
  Allocator h = heap();
  u64* base = static_cast<u64*>(h.alloc(size + SDL_BLOCK_HEADER, SDL_BLOCK_HEADER));
  base[0] = size;
  return base + 2;
}

static void* SDLCALL sdl_calloc(size_t count, size_t size) {
  if (size != 0 && count > (~usize{0} - SDL_BLOCK_HEADER) / size) return nullptr;
  void* p = sdl_malloc(count * size);
  std::memset(p, 0, count * size);
  return p;
}

static void* SDLCALL sdl_realloc(void* mem, size_t size) {
  if (!mem) return sdl_malloc(size);
  Allocator h = heap();
  u64* base = static_cast<u64*>(mem) - 2;
  base = static_cast<u64*>(h.resize(base, static_cast<usize>(base[0]) + SDL_BLOCK_HEADER,
                                    size + SDL_BLOCK_HEADER, SDL_BLOCK_HEADER));
  base[0] = size;
  return base + 2;
}

static void SDLCALL sdl_free(void* mem) {
  if (!mem) return;
  Allocator h = heap();
  u64* base = static_cast<u64*>(mem) - 2;
  h.free(base, static_cast<usize>(base[0]) + SDL_BLOCK_HEADER);
}

static std::atomic<bool> g_initialized{false};

Result<void> init() {
  if (g_initialized.load()) return {};
  if (SDL_GetNumAllocations() > 0)
    return detail::fail(Error::InvalidArgument,
                        "oat::os::init must run before SDL has allocated any memory");
  if (!SDL_SetMemoryFunctions(sdl_malloc, sdl_calloc, sdl_realloc, sdl_free))
    return detail::fail(Error::Unknown);
  g_initialized.store(true);
  return {};
}

} // namespace oat::os
