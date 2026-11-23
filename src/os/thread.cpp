#include "os_internal.h"

namespace oat::os {

struct ThreadStart {
  Thread::Fn fn;
  void* ctx;
};

// SDL's entry point has its own calling convention on some targets, so the
// user's function is called through this.
static int SDLCALL thread_entry(void* data) {
  ThreadStart start = *static_cast<ThreadStart*>(data);
  Allocator h = heap();
  h.free(data, sizeof(ThreadStart));
  return start.fn(start.ctx);
}

Result<Thread> Thread::start(const char* name, Fn fn, void* ctx) {
  Allocator h = heap();
  ThreadStart* start = static_cast<ThreadStart*>(h.alloc(sizeof(ThreadStart), alignof(ThreadStart)));
  start->fn = fn;
  start->ctx = ctx;
  SDL_Thread* thread = SDL_CreateThread(thread_entry, name, start);
  if (!thread) {
    h.free(start, sizeof(ThreadStart));
    return detail::fail(Error::Unknown);
  }
  Thread t;
  t.handle = thread;
  return t;
}

i32 Thread::join() {
  OAT_ASSERT(handle != nullptr);
  int status = 0;
  SDL_WaitThread(handle, &status);
  handle = nullptr;
  return status;
}

const char* Thread::name() const { return handle ? SDL_GetThreadName(handle) : ""; }

Mutex Mutex::create() {
  Mutex m;
  m.handle = SDL_CreateMutex();
  OAT_CHECK(m.handle != nullptr);
  return m;
}
void Mutex::destroy() {
  SDL_DestroyMutex(handle);
  handle = nullptr;
}
void Mutex::lock() { SDL_LockMutex(handle); }
bool Mutex::try_lock() { return SDL_TryLockMutex(handle); }
void Mutex::unlock() { SDL_UnlockMutex(handle); }

CondVar CondVar::create() {
  CondVar c;
  c.handle = SDL_CreateCondition();
  OAT_CHECK(c.handle != nullptr);
  return c;
}
void CondVar::destroy() {
  SDL_DestroyCondition(handle);
  handle = nullptr;
}
void CondVar::wait(Mutex& mutex) { SDL_WaitCondition(handle, mutex.handle); }
bool CondVar::wait_timeout(Mutex& mutex, i32 timeout_ms) {
  return SDL_WaitConditionTimeout(handle, mutex.handle, timeout_ms);
}
void CondVar::signal() { SDL_SignalCondition(handle); }
void CondVar::broadcast() { SDL_BroadcastCondition(handle); }

Semaphore Semaphore::create(u32 initial_value) {
  Semaphore s;
  s.handle = SDL_CreateSemaphore(initial_value);
  OAT_CHECK(s.handle != nullptr);
  return s;
}
void Semaphore::destroy() {
  SDL_DestroySemaphore(handle);
  handle = nullptr;
}
void Semaphore::wait() { SDL_WaitSemaphore(handle); }
bool Semaphore::try_wait() { return SDL_TryWaitSemaphore(handle); }
bool Semaphore::wait_timeout(i32 timeout_ms) { return SDL_WaitSemaphoreTimeout(handle, timeout_ms); }
void Semaphore::signal() { SDL_SignalSemaphore(handle); }

u32 core_count() {
  int n = SDL_GetNumLogicalCPUCores();
  return n > 0 ? static_cast<u32>(n) : 1;
}

u64 current_thread_id() { return SDL_GetCurrentThreadID(); }

} // namespace oat::os
