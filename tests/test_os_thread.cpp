#include "test.h"

#include <oat/oat.h>
#include <oat/os.h>

#include <atomic>

using namespace oat;

TEST(thread_start_join) {
  struct Ctx {
    int input;
    u64 thread_id;
  } ctx{20, 0};
  Result<os::Thread> t = os::Thread::start(
      "oat-test",
      [](void* p) -> i32 {
        Ctx* c = static_cast<Ctx*>(p);
        c->thread_id = os::current_thread_id();
        return c->input + 1;
      },
      &ctx);
  CHECK(t.has_value());
  if (!t) return;
  CHECK(std::strcmp(t->name(), "oat-test") == 0);
  CHECK(t->join() == 21);
  CHECK(t->handle == nullptr);
  CHECK(ctx.thread_id != 0 && ctx.thread_id != os::current_thread_id());
  CHECK(os::core_count() >= 1);
}

TEST(mutex_protects_counter) {
  struct Shared {
    os::Mutex mutex;
    u64 counter = 0;
  } shared;
  shared.mutex = os::Mutex::create();

  CHECK(shared.mutex.try_lock());
  shared.mutex.unlock();

  os::Thread threads[4];
  for (os::Thread& t : threads) {
    t = *os::Thread::start(
        "oat-counter",
        [](void* p) -> i32 {
          Shared* s = static_cast<Shared*>(p);
          for (u32 i = 0; i < 20000; ++i) {
            os::MutexLock lock(s->mutex);
            ++s->counter;
          }
          return 0;
        },
        &shared);
  }
  for (os::Thread& t : threads) t.join();
  CHECK(shared.counter == 80000);
  shared.mutex.destroy();
}

TEST(condvar_and_semaphore) {
  struct Shared {
    os::Mutex mutex;
    os::CondVar cond;
    os::Semaphore started;
    bool ready = false;
    bool saw_ready = false;
  } shared;
  shared.mutex = os::Mutex::create();
  shared.cond = os::CondVar::create();
  shared.started = os::Semaphore::create(0);

  CHECK(!shared.started.try_wait());
  CHECK(!shared.started.wait_timeout(1));
  {
    os::MutexLock lock(shared.mutex);
    CHECK(!shared.cond.wait_timeout(shared.mutex, 1)); // nobody signals: times out
  }

  os::Thread waiter = *os::Thread::start(
      "oat-waiter",
      [](void* p) -> i32 {
        Shared* s = static_cast<Shared*>(p);
        s->started.signal();
        os::MutexLock lock(s->mutex);
        while (!s->ready) s->cond.wait(s->mutex);
        s->saw_ready = true;
        return 0;
      },
      &shared);

  shared.started.wait(); // the waiter is running
  {
    os::MutexLock lock(shared.mutex);
    shared.ready = true;
  }
  shared.cond.broadcast();
  waiter.join();
  CHECK(shared.saw_ready);

  shared.started.signal();
  shared.started.signal();
  CHECK(shared.started.try_wait() && shared.started.wait_timeout(100) && !shared.started.try_wait());

  shared.started.destroy();
  shared.cond.destroy();
  shared.mutex.destroy();
}

TEST(jobs_submit_and_wait) {
  os::JobSystem jobs;
  jobs.start(4);
  CHECK(jobs.workers.len == 4);

  std::atomic<u32> counter{0};
  os::JobGroup group;
  for (u32 i = 0; i < 1000; ++i)
    jobs.submit([](void* p) { static_cast<std::atomic<u32>*>(p)->fetch_add(1); }, &counter, &group);
  group.wait();
  CHECK(counter.load() == 1000);
  CHECK(group.pending == 0);

  // A group can be reused, and waiting on an unused group returns at once.
  jobs.submit([](void* p) { static_cast<std::atomic<u32>*>(p)->fetch_add(1); }, &counter, &group);
  group.wait();
  CHECK(counter.load() == 1001);
  os::JobGroup unused;
  unused.wait();

  // Jobs without a group still run before stop() returns.
  for (u32 i = 0; i < 100; ++i)
    jobs.submit([](void* p) { static_cast<std::atomic<u32>*>(p)->fetch_add(1); }, &counter);
  jobs.stop();
  CHECK(counter.load() == 1101);
  jobs.stop(); // harmless

  // A system can be started again after stopping.
  jobs.start(1);
  jobs.submit([](void* p) { static_cast<std::atomic<u32>*>(p)->fetch_add(1); }, &counter, &group);
  group.wait();
  jobs.stop();
  CHECK(counter.load() == 1102);
}

TEST(jobs_nested_wait_does_not_deadlock) {
  // Every worker blocks in a group wait of its own; they have to help each
  // other finish, or nothing would ever run the inner jobs.
  struct Ctx {
    os::JobSystem* jobs;
    std::atomic<u32> inner_done{0};
  } ctx;
  os::JobSystem jobs;
  jobs.start(2);
  ctx.jobs = &jobs;

  os::JobGroup outer;
  for (u32 i = 0; i < 16; ++i) {
    jobs.submit(
        [](void* p) {
          Ctx* c = static_cast<Ctx*>(p);
          os::JobGroup inner;
          for (u32 k = 0; k < 8; ++k)
            c->jobs->submit(
                [](void* q) { static_cast<Ctx*>(q)->inner_done.fetch_add(1); }, c, &inner);
          inner.wait();
        },
        &ctx, &outer);
  }
  outer.wait();
  CHECK(ctx.inner_done.load() == 16 * 8);
  jobs.stop();
}

TEST(jobs_parallel_for) {
  os::JobSystem jobs;
  jobs.start(3);
  Allocator h = heap();

  for (u32 count : {0u, 1u, 7u, 1000u, 100000u}) {
    for (u32 batch : {1u, 64u, 1000000u}) {
      if (count > 1000 && batch == 1) continue;
      Array<u8> visits;
      visits.resize(count, h);
      jobs.parallel_for(
          count, batch,
          [](void* p, u32 begin, u32 end) {
            Array<u8>* v = static_cast<Array<u8>*>(p);
            for (u32 i = begin; i < end; ++i) ++(*v)[i]; // each index belongs to one batch
          },
          &visits);
      bool once = true;
      for (u8 v : visits) once = once && v == 1;
      CHECK(once);
      visits.free(h);
    }
  }

  // parallel_for from inside a job.
  struct Nested {
    os::JobSystem* jobs;
    std::atomic<u64> sum{0};
  } nested;
  nested.jobs = &jobs;
  os::JobGroup group;
  for (u32 i = 0; i < 4; ++i) {
    jobs.submit(
        [](void* p) {
          Nested* n = static_cast<Nested*>(p);
          n->jobs->parallel_for(
              100, 10,
              [](void* q, u32 begin, u32 end) {
                u64 local = 0;
                for (u32 k = begin; k < end; ++k) local += k;
                static_cast<Nested*>(q)->sum.fetch_add(local);
              },
              n);
        },
        &nested, &group);
  }
  group.wait();
  CHECK(nested.sum.load() == 4ull * 4950);
  jobs.stop();
}

struct SteadyCtx {
  os::JobSystem jobs;
  os::JobGroup group;
  os::Semaphore worker_parked;
  os::Semaphore release_worker;
  u32 remaining = 200000;
  u32 max_cap = 0;
  u32 ran = 0;
};

// Resubmits itself until `remaining` runs out.
static void steady_job(void* p) {
  SteadyCtx* c = static_cast<SteadyCtx*>(p);
  ++c->ran;
  c->max_cap = oat::max(c->max_cap, c->jobs.queue.cap);
  if (c->remaining > 0) {
    --c->remaining;
    c->jobs.submit(steady_job, c, &c->group);
  }
}

TEST(jobs_queue_stays_bounded_under_steady_load) {
  // Two chains of jobs that keep resubmitting themselves, so the queue never
  // drains. The only worker is parked, which leaves this thread running every
  // job from group.wait(). The queue has to reuse its space rather than grow
  // by one slot per job.
  SteadyCtx ctx;
  ctx.worker_parked = os::Semaphore::create(0);
  ctx.release_worker = os::Semaphore::create(0);
  ctx.jobs.start(1);
  ctx.jobs.submit(
      [](void* p) {
        SteadyCtx* c = static_cast<SteadyCtx*>(p);
        c->worker_parked.signal();
        c->release_worker.wait();
      },
      &ctx);
  ctx.worker_parked.wait();

  ctx.jobs.submit(steady_job, &ctx, &ctx.group);
  ctx.jobs.submit(steady_job, &ctx, &ctx.group);
  ctx.group.wait();
  CHECK(ctx.ran == 200002);
  CHECK(ctx.max_cap <= 16); // not 200,000

  ctx.release_worker.signal();
  ctx.jobs.stop();
  ctx.worker_parked.destroy();
  ctx.release_worker.destroy();
}

TEST(jobs_scratch_arena_per_thread) {
  // Each thread gets its own arena, so jobs can use it without locking.
  struct Ctx {
    os::Mutex mutex;
    Array<Arena*> seen;
    std::atomic<u32> ok{0};
  } ctx;
  ctx.mutex = os::Mutex::create();
  os::JobSystem jobs;
  jobs.start(3);

  os::JobGroup group;
  for (u32 i = 0; i < 200; ++i) {
    jobs.submit(
        [](void* p) {
          Ctx* c = static_cast<Ctx*>(p);
          Arena& arena = os::scratch_arena();
          ArenaMark before = arena.mark();
          {
            ArenaScope scope(arena);
            Allocator a = arena.allocator();
            Array<u32> tmp;
            for (u32 k = 0; k < 500; ++k) tmp.push(k, a);
            u64 sum = 0;
            for (u32 v : tmp) sum += v;
            if (sum == 124750) c->ok.fetch_add(1);
          }
          bool restored = arena.mark().block == before.block && arena.mark().used == before.used;
          if (!restored) c->ok.fetch_sub(1000);

          os::MutexLock lock(c->mutex);
          Allocator h = heap();
          bool known = false;
          for (Arena* a : c->seen) known = known || a == &arena;
          if (!known) c->seen.push(&arena, h);
        },
        &ctx, &group);
  }
  group.wait();
  CHECK(ctx.ok.load() == 200);
  CHECK(ctx.seen.len >= 1 && ctx.seen.len <= 4); // at most the workers plus this thread
  jobs.stop();

  Allocator h = heap();
  ctx.seen.free(h);
  ctx.mutex.destroy();
  os::free_scratch_arena(); // this thread helped from group.wait()
  os::free_scratch_arena(); // harmless
}
