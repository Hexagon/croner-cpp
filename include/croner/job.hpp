// Croner for C++ - MIT License - Hexagon <github.com/Hexagon>
//
// croner::Job runs a function on a Cron schedule, on background threads.

#ifndef CRONER_JOB_HPP
#define CRONER_JOB_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include "cron.hpp"
#include "error.hpp"

namespace croner {

class Job;

/// Options for a job. All are optional.
struct JobOptions {
  /// Number of scheduled runs before the job stops. Unlimited if not set.
  std::optional<std::int64_t> max_runs;
  /// Create the job paused
  bool paused = false;
  /// Skip a run while the previous one is still running
  bool protect = false;
  /// Called when a run is skipped by protect. Setting it implies protect.
  std::function<void(Job&)> on_protect;
  /// Called with exceptions thrown by the job function. Without it, an exception thrown on a
  /// worker thread calls std::terminate, like in any std::thread. Use an empty handler to
  /// ignore exceptions.
  std::function<void(std::exception_ptr, Job&)> on_error;
  /// Let a single-run schedule dated in the past run immediately
  bool allow_past = false;
};

/// Runs a function on a schedule.
///
/// Each run starts a new thread, so a slow job function does not delay the schedule; runs may
/// overlap unless `protect` is set. The job function is called as `f()` or `f(Job&)`; the
/// Job& refers to this job, and can be used to stop or pause it.
///
/// A Job is a move-only handle. Destroying it (or assigning to it) stops the job and waits for
/// running job functions to finish, except one that destroys the job from inside itself.
///
///     croner::Job job(croner::Cron("*/5 * * * * *"), [] { std::puts("tick"); });
class Job {
 public:
  using Function = std::function<void(Job&)>;

  /// Starts scheduling right away, unless options.paused is set.
  /// @throws CronError if the options are invalid
  template <class F>
  Job(Cron cron, F&& fn, JobOptions options = {})
      : s_(std::make_shared<State>(std::move(cron), std::move(options),
                                   make_function(std::forward<F>(fn)))) {
    if (s_->options.on_protect) s_->options.protect = true;
    s_->runs_left = s_->options.max_runs;
    s_->paused = s_->options.paused;
    start(s_);
  }

  Job(Job&& other) noexcept : s_(std::move(other.s_)), owning_(other.owning_) {}

  Job& operator=(Job&& other) noexcept {
    if (this != &other) {
      release();
      s_ = std::move(other.s_);
      owning_ = other.owning_;
    }
    return *this;
  }

  Job(const Job&) = delete;
  Job& operator=(const Job&) = delete;

  ~Job() { release(); }

  /// Pause the job. Runs that are due while paused are skipped.
  void pause() {
    std::lock_guard<std::mutex> lock(s_->mtx);
    s_->paused = true;
  }

  void resume() {
    std::lock_guard<std::mutex> lock(s_->mtx);
    s_->paused = false;
  }

  /// Stop the job permanently. A running job function finishes.
  void stop() {
    {
      std::lock_guard<std::mutex> lock(s_->mtx);
      s_->kill = true;
    }
    s_->cv.notify_all();
  }

  /// Block until the job has no more runs (or is stopped) and running job functions have
  /// finished. When called from a job function, that function is not waited for.
  void join() { join_threads(*s_); }

  /// Run the job function now, on the calling thread. Exceptions propagate unless on_error is
  /// set. Doesn't count towards max_runs.
  void trigger() {
    const detail::Millis now = Cron::now_ms();
    {
      std::lock_guard<std::mutex> lock(s_->mtx);
      begin_run(*s_, now);
    }
    run_job(s_, now);
  }

  /// Scheduled, not paused or stopped, and has a next run
  [[nodiscard]] bool is_running() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return !s_->paused && next_target(*s_, s_->current_run, Cron::now_ms()).has_value();
  }

  [[nodiscard]] bool is_paused() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return s_->paused;
  }

  [[nodiscard]] bool is_stopped() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return s_->kill;
  }

  /// A job function is running
  [[nodiscard]] bool is_busy() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return s_->blocking;
  }

  /// Remaining scheduled runs, nullopt if unlimited
  [[nodiscard]] std::optional<std::int64_t> runs_left() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return s_->runs_left;
  }

  /// When the job will run next, nullopt if it won't
  [[nodiscard]] std::optional<TimePoint> next_run() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return Cron::to_time_point(next_target(*s_, std::nullopt, Cron::now_ms()));
  }

  /// Start of the current (or last started) run
  [[nodiscard]] std::optional<TimePoint> current_run() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return Cron::to_time_point(s_->current_run);
  }

  /// Start of the last finished run
  [[nodiscard]] std::optional<TimePoint> previous_run() const {
    std::lock_guard<std::mutex> lock(s_->mtx);
    return Cron::to_time_point(s_->previous_run);
  }

  [[nodiscard]] const Cron& cron() const noexcept { return s_->cron; }
  [[nodiscard]] const JobOptions& options() const noexcept { return s_->options; }

 private:
  using Millis = detail::Millis;

  struct Worker {
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> done;
  };

  // Shared by the handle and the threads, so the handle can move and a job function can
  // outlive it
  struct State {
    State(Cron c, JobOptions o, Function f)
        : cron(std::move(c)), options(std::move(o)), fn(std::move(f)) {}

    const Cron cron;
    JobOptions options;
    const Function fn;

    std::mutex mtx;  // guards everything below, except the workers
    std::condition_variable cv;
    bool kill = false;
    bool blocking = false;
    bool paused = false;
    std::optional<Millis> previous_run;
    std::optional<Millis> current_run;
    std::optional<std::int64_t> runs_left;
    std::thread scheduler;

    std::mutex workers_mtx;  // taken after mtx, never before
    std::vector<Worker> workers;
  };

  /// Longest sleep between checks, so that clock changes are noticed
  static constexpr Millis MAX_DELAY_MS = 30000;

  std::shared_ptr<State> s_;
  bool owning_ = true;

  // A non-owning handle, passed to job functions and handlers
  Job(std::shared_ptr<State> s, bool owning) : s_(std::move(s)), owning_(owning) {}

  template <class F>
  static Function make_function(F&& fn) {
    using Fn = std::decay_t<F>;
    if constexpr (std::is_invocable_v<Fn&, Job&>) {
      return Function(std::forward<F>(fn));
    } else {
      static_assert(std::is_invocable_v<Fn&>, "a job function is called as f() or f(Job&)");
      return [f = Fn(std::forward<F>(fn))](Job&) mutable { f(); };
    }
  }

  void release() {
    if (s_ && owning_) {
      stop();
      join();
    }
    s_.reset();
  }

  static std::optional<Millis> next_target(const State& s, const std::optional<Millis>& reference,
                                           Millis now) {
    if (s.kill || (s.runs_left && *s.runs_left <= 0)) return std::nullopt;
    return s.cron.next_target(reference, now, s.current_run, s.options.allow_past);
  }

  /// When to check next, and the target time: (target, wait ms)
  static std::optional<std::pair<Millis, Millis>> compute_schedule(const State& s,
                                                                   Millis current_time) {
    const auto next = next_target(s, std::nullopt, current_time);
    if (!next) return std::nullopt;
    Millis wait = *next - current_time;
    // If the clock has stepped backward (current run > now), search from now instead
    std::optional<Millis> previous = s.current_run;
    if (previous && *previous > current_time) previous.reset();
    const auto target = next_target(s, previous, current_time);
    if (!target) return std::nullopt;
    if (wait > MAX_DELAY_MS) wait = MAX_DELAY_MS;
    if (wait < 0) wait = s.paused ? 1000 : 0;
    return std::make_pair(*target, wait);
  }

  static void start(const std::shared_ptr<State>& s) {
    std::lock_guard<std::mutex> lock(s->mtx);
    const auto plan = compute_schedule(*s, Cron::now_ms());
    if (!plan) return;
    s->scheduler = std::thread(run_scheduler, s, plan->first, plan->second);
  }

  static void run_scheduler(std::shared_ptr<State> s, Millis target, Millis wait_ms) {
    std::unique_lock<std::mutex> lock(s->mtx);
    for (;;) {
      const auto deadline = Clock::now() + std::chrono::milliseconds(wait_ms);
      s->cv.wait_until(lock, deadline, [&] { return s->kill; });
      if (s->kill) break;

      const Millis now = Cron::now_ms();
      const bool should_run = !s->paused && now >= target;
      const bool is_blocked = s->blocking && s->options.protect;
      if (should_run && !is_blocked) {
        if (s->runs_left) --*s->runs_left;
        begin_run(*s, now);
        spawn_worker(s, now);
      } else if (should_run && is_blocked && s->options.on_protect) {
        const auto handler = s->options.on_protect;
        lock.unlock();
        try {
          Job self(s, false);
          handler(self);
        } catch (...) {
        }
        lock.lock();
        if (s->kill) break;
      }

      const auto plan = compute_schedule(*s, should_run ? Cron::now_ms() : now);
      if (!plan) break;
      target = plan->first;
      wait_ms = plan->second;
    }
  }

  static void begin_run(State& s, Millis now) {
    s.blocking = true;
    s.current_run = now;
  }

  static void spawn_worker(const std::shared_ptr<State>& s, Millis now) {
    std::lock_guard<std::mutex> lock(s->workers_mtx);
    // Reap finished workers
    for (auto it = s->workers.begin(); it != s->workers.end();) {
      if (it->done->load()) {
        it->thread.join();
        it = s->workers.erase(it);
      } else {
        ++it;
      }
    }
    auto done = std::make_shared<std::atomic<bool>>(false);
    s->workers.push_back(Worker{std::thread([s, now, done] {
                                  run_job(s, now);
                                  done->store(true);
                                }),
                                done});
  }

  static void run_job(const std::shared_ptr<State>& s, Millis initiation) {
    struct Finish {
      State& s;
      Millis initiation;
      ~Finish() {
        std::lock_guard<std::mutex> lock(s.mtx);
        s.previous_run = initiation;
        s.blocking = false;
      }
    } finish{*s, initiation};

    // Each run gets its own copy of the function, so runs that overlap don't share its state
    Function fn = s->fn;
    Job self(s, false);
    if (s->options.on_error) {
      try {
        fn(self);
      } catch (...) {
        try {
          s->options.on_error(std::current_exception(), self);
        } catch (...) {
          // Exceptions from the error handler are ignored
        }
      }
    } else {
      fn(self);
    }
  }

  static void join_threads(State& s) {
    std::thread scheduler;
    {
      std::lock_guard<std::mutex> lock(s.mtx);
      scheduler = std::move(s.scheduler);
    }
    const auto join_or_detach = [](std::thread& t) {
      if (!t.joinable()) return;
      if (t.get_id() == std::this_thread::get_id()) {
        t.detach();  // called from this thread's own function
      } else {
        t.join();
      }
    };
    join_or_detach(scheduler);
    std::vector<Worker> workers;
    {
      std::lock_guard<std::mutex> lock(s.workers_mtx);
      workers.swap(s.workers);
    }
    for (auto& w : workers) join_or_detach(w.thread);
  }
};

}  // namespace croner

#endif  // CRONER_JOB_HPP
