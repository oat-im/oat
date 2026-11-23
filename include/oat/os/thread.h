// oat/os/thread.h - threads and synchronization primitives.
#pragma once

#include "../base.h"

struct SDL_Thread;
struct SDL_Mutex;
struct SDL_Condition;
struct SDL_Semaphore;

namespace oat::os {

struct Thread {
  using Fn = i32 (*)(void* ctx);

  SDL_Thread* handle = nullptr;

  // `name` shows up in debuggers and profilers.
  [[nodiscard]] static Result<Thread> start(const char* name, Fn fn, void* ctx);
  // Waits for the thread to finish and returns what its function returned.
  // Every started thread must be joined exactly once.
  i32 join();
  const char* name() const;
};

// Not recursive-safe by contract: do not lock a Mutex the thread already holds.
struct Mutex {
  SDL_Mutex* handle = nullptr;

  static Mutex create();
  void destroy();
  void lock();
  bool try_lock();
  void unlock();
};

// Locks on construction, unlocks on destruction.
struct MutexLock {
  Mutex& mutex;
  explicit MutexLock(Mutex& m) : mutex(m) { mutex.lock(); }
  ~MutexLock() { mutex.unlock(); }
  MutexLock(const MutexLock&) = delete;
  MutexLock& operator=(const MutexLock&) = delete;
};

struct CondVar {
  SDL_Condition* handle = nullptr;

  static CondVar create();
  void destroy();
  // The mutex must be locked by the caller; it is released while waiting and
  // locked again on return. Wake-ups can be spurious: wait in a loop.
  void wait(Mutex& mutex);
  // False if the time ran out.
  bool wait_timeout(Mutex& mutex, i32 timeout_ms);
  void signal();
  void broadcast();
};

struct Semaphore {
  SDL_Semaphore* handle = nullptr;

  static Semaphore create(u32 initial_value);
  void destroy();
  void wait();
  bool try_wait();
  // False if the time ran out.
  bool wait_timeout(i32 timeout_ms);
  void signal();
};

u32 core_count();

// A number that identifies the calling thread.
u64 current_thread_id();

} // namespace oat::os
