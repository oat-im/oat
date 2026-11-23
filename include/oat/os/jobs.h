// oat/os/jobs.h - a small job system: one mutex, one condition variable, one
// queue. Jobs are a function pointer and a context pointer.
#pragma once

#include "../array.h"
#include "../base.h"
#include "../memory.h"
#include "thread.h"

namespace oat::os {

using JobFn = void (*)(void* ctx);
using RangeFn = void (*)(void* ctx, u32 begin, u32 end);

struct JobSystem;

// Counts the unfinished jobs submitted with it. Must stay at one address
// until wait() returns.
struct JobGroup {
  JobSystem* system = nullptr;
  u32 pending = 0; // guarded by the system's mutex

  // Returns once every job submitted with this group has finished. The
  // calling thread runs queued jobs while it waits, so calling this from
  // inside a job is fine.
  //
  // Helping means a job run from here can itself wait, and run another job
  // that waits, each level on the same stack. A few levels deep, as in
  // parallel_for inside a job, is fine. Do not build unbounded chains of
  // jobs that wait on jobs.
  void wait();
};

struct Job {
  JobFn fn;
  void* ctx;
  JobGroup* group;
};

// Workers hold a pointer to the JobSystem: do not move or copy it between
// start() and stop().
struct JobSystem {
  Mutex mutex;
  CondVar wake;       // signalled when a job is queued or a group finishes
  Array<Job> queue;   // pending jobs are queue[queue_head .. queue.len)
  u32 queue_head = 0;
  Array<Thread> workers;
  bool stopping = false;

  // worker_count 0 means one worker per core, minus one for the calling thread.
  void start(u32 worker_count = 0);
  // Runs every job still queued, then joins the workers.
  void stop();

  // `ctx` must stay valid until the job has run.
  void submit(JobFn fn, void* ctx, JobGroup* group = nullptr);

  // Calls fn(ctx, begin, end) over [0, count) in batches of `batch`, on the
  // workers and the calling thread, and returns when all of it is done.
  void parallel_for(u32 count, u32 batch, RangeFn fn, void* ctx);
};

// A per-thread arena for temporary allocations, created on first use:
//
//   ArenaScope scope(os::scratch_arena());
//
// Workers release theirs when the JobSystem stops. Other threads can release
// theirs with free_scratch_arena().
Arena& scratch_arena();
void free_scratch_arena();

} // namespace oat::os
