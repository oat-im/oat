#include "os_internal.h"

#include <atomic>

namespace oat::os {

// ---------------------------------------------------------------------------
// Scratch arenas
// ---------------------------------------------------------------------------

static thread_local Arena t_scratch;
static thread_local bool t_scratch_ready = false;

Arena& scratch_arena() {
  if (!t_scratch_ready) {
    Allocator h = heap();
    t_scratch = Arena::create(h, 256 * 1024);
    t_scratch_ready = true;
  }
  return t_scratch;
}

void free_scratch_arena() {
  if (t_scratch_ready) {
    t_scratch.destroy();
    t_scratch_ready = false;
  }
}

// ---------------------------------------------------------------------------
// Queue. Everything below runs with the system's mutex held unless it says
// otherwise.
// ---------------------------------------------------------------------------

static bool jobs_pending(const JobSystem* s) { return s->queue_head < s->queue.len; }

static Job jobs_pop(JobSystem* s) {
  Job job = s->queue[s->queue_head++];
  if (s->queue_head == s->queue.len) {
    s->queue.clear();
    s->queue_head = 0;
  }
  return job;
}

// Runs one job with the mutex released, then settles its group.
static void jobs_run(JobSystem* s, Job job) {
  s->mutex.unlock();
  job.fn(job.ctx);
  s->mutex.lock();
  if (job.group && --job.group->pending == 0) s->wake.broadcast();
}

static i32 jobs_worker(void* ctx) {
  JobSystem* s = static_cast<JobSystem*>(ctx);
  s->mutex.lock();
  for (;;) {
    while (!jobs_pending(s) && !s->stopping) s->wake.wait(s->mutex);
    if (!jobs_pending(s)) break; // stopping, and the queue is drained
    jobs_run(s, jobs_pop(s));
  }
  s->mutex.unlock();
  free_scratch_arena();
  return 0;
}

void JobSystem::start(u32 worker_count) {
  OAT_ASSERT_MSG(workers.len == 0, "JobSystem is already started");
  if (worker_count == 0) {
    u32 cores = core_count();
    worker_count = cores > 1 ? cores - 1 : 1;
  }
  mutex = Mutex::create();
  wake = CondVar::create();
  stopping = false;
  queue_head = 0;
  Allocator h = heap();
  workers.reserve(worker_count, h);
  for (u32 i = 0; i < worker_count; ++i) {
    Result<Thread> t = Thread::start("oat-worker", jobs_worker, this);
    OAT_CHECK(t.has_value());
    workers.push(*t, h);
  }
}

void JobSystem::stop() {
  if (!mutex.handle) return;
  mutex.lock();
  stopping = true;
  wake.broadcast();
  mutex.unlock();
  for (Thread& t : workers) t.join();
  Allocator h = heap();
  workers.free(h);
  queue.free(h);
  queue_head = 0;
  wake.destroy();
  mutex.destroy();
}

void JobSystem::submit(JobFn fn, void* ctx, JobGroup* group) {
  Allocator h = heap();
  mutex.lock();
  OAT_ASSERT_MSG(!stopping, "submit after JobSystem::stop");
  if (group) {
    OAT_ASSERT_MSG(!group->system || group->system == this, "JobGroup used with two systems");
    group->system = this;
    ++group->pending;
  }
  queue.push(Job{fn, ctx, group}, h);
  // Either a worker or a thread helping from JobGroup::wait picks it up.
  wake.signal();
  mutex.unlock();
}

void JobGroup::wait() {
  JobSystem* s = system;
  if (!s) return; // nothing was ever submitted with this group
  s->mutex.lock();
  while (pending > 0) {
    if (jobs_pending(s)) jobs_run(s, jobs_pop(s)); // help instead of sleeping
    else s->wake.wait(s->mutex);
  }
  s->mutex.unlock();
}

// ---------------------------------------------------------------------------
// parallel_for: every participant claims the next batch from a shared counter,
// so there is nothing to allocate per batch.
// ---------------------------------------------------------------------------

struct ParallelFor {
  RangeFn fn;
  void* ctx;
  u32 count;
  u32 batch;
  std::atomic<u64> next{0};
};

static void parallel_for_job(void* p) {
  ParallelFor* pf = static_cast<ParallelFor*>(p);
  for (;;) {
    u64 begin = pf->next.fetch_add(pf->batch, std::memory_order_relaxed);
    if (begin >= pf->count) return;
    u64 end = begin + pf->batch;
    if (end > pf->count) end = pf->count;
    pf->fn(pf->ctx, static_cast<u32>(begin), static_cast<u32>(end));
  }
}

void JobSystem::parallel_for(u32 count, u32 batch, RangeFn fn, void* ctx) {
  if (count == 0) return;
  ParallelFor pf;
  pf.fn = fn;
  pf.ctx = ctx;
  pf.count = count;
  pf.batch = batch ? batch : 1;

  u32 batches = (count - 1) / pf.batch + 1;
  u32 helpers = oat::min(batches - 1, workers.len);
  JobGroup group;
  for (u32 i = 0; i < helpers; ++i) submit(parallel_for_job, &pf, &group);
  parallel_for_job(&pf); // the caller takes batches too
  group.wait();          // pf must outlive every helper
}

} // namespace oat::os
