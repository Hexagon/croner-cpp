# Review and roadmap

Review of croner-cpp 0.1.0 (2026-11-07), and a plan for the work before 1.0. Phases 1 and 2 are
done in 0.2.0, see [CHANGELOG.md](CHANGELOG.md). Release and packaging steps are in
[PUBLISHING.md](PUBLISHING.md).

## Summary

The core is solid: the pattern parser and the search are well tested (unit tests, OCPS vectors,
differential tests against two other implementations, sanitizers), the zoneinfo reader is checked
against the C library for every zone, and the DST handling is well defined. The build and
packaging setup (header-only `INTERFACE` target, `croner::croner`, install and package config,
`FetchContent`, Conan) follows common CMake practice.

The weak spot is the public API. It mirrors the JavaScript `Cron` class nearly one to one, and
several of its choices are natural in JavaScript but unusual in C++: one class that both
evaluates patterns and owns threads, a global registry of named jobs, three mutually exclusive
ways to set a time zone, plain integers for durations, `get_` prefixes, JavaScript names in error
messages, and implementation types exposed in the public namespace. As the API is still `0.x`,
now is the time to change it.

## What follows C++ conventions

- Header-only library in `include/croner/`, an umbrella header `croner/croner.hpp`, include
  guards, everything in `namespace croner` with internals in `croner::detail`.
- CMake: `INTERFACE` library with `cxx_std_17` as a usage requirement, `croner::croner` alias,
  `Threads::Threads` dependency, tests and examples off when used as a subproject, install rules
  with a relocatable package config and `SameMajorVersion ARCH_INDEPENDENT` version file.
- `std::chrono::system_clock` time points in the API, `std::optional` for "no run",
  RAII (destroying a job stops it and joins its threads), non-copyable job type.
- snake_case names, as in the standard library.
- Strict warnings (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow`, `/W4`),
  ASan, UBSan and TSan in CI.

## Findings

### API design (inherited from JavaScript)

1. **Evaluation and scheduling are one class.** `Cron` parses and evaluates patterns, and also
   owns a scheduler thread, worker threads, a recursive mutex and a condition variable. That
   makes the type 1.2 KB, non-movable, and expensive to create just to compute `next_run()`.
   Evaluation results also depend on scheduler state: `next_runs()` is capped by the remaining
   `max_runs`, and `next_run()` returns nothing once the job is stopped. In C++ one would expect
   a cheap, copyable, immutable value type for the schedule, with `const` thread-safe queries,
   and a separate type that runs a function on it.
2. **Global registry of named jobs.** `scheduled_jobs()` returns raw `Cron*` pointers to objects
   that another thread may destroy at any time. Names are unique process-wide, which also breaks
   independent libraries that both use Croner. C++ code normally keeps its own containers.
3. **Three ways to set the time zone** (`timezone`, `utc_offset`, `zone`), checked at run time
   to be mutually exclusive. A single `TimeZonePtr` set with the existing factory functions
   (`locate_zone()`, `fixed_offset_zone()`, `utc_zone()`) would make invalid combinations
   impossible.
4. **Durations are plain integers:** `interval` (seconds), `utc_offset` (minutes),
   `day_offset` (days), `ms_to_next()`. `std::chrono::seconds` and `std::chrono::minutes` make
   the unit part of the type.
5. **Names:** `get_pattern()` and `get_once()` (C++ uses `pattern()`, `once()`); `ms_to_next()`
   (`time_to_next()`); `pause()` and `resume()` returning `bool`; `is_running()` meaning
   "scheduled, not paused, and has a next run".
6. **Job function signature** must be `void(Cron&)`. Accepting `void()` as well would let users
   pass plain lambdas.
7. **Implementation types are public:** `CronDate` (with public mutable fields),
   `SearchOptions`, `PatternOptions`, and `CronPattern`, whose fields are public and writable,
   so a user can break its invariants. `CronDate` and `SearchOptions` belong in `detail`;
   `CronPattern` should expose read-only accessors, if anything.
8. **Error messages use JavaScript names** and class prefixes: `CronOptions: utcOffset out of
   bounds.`, `Invalid value for dayOfWeek`, `CronDate: Invalid ISO8601 date string`. They
   should use the C++ option names. There is one error type without any way to tell a pattern
   error from a time zone error, other than the message.
9. **Missing `[[nodiscard]]`** on query functions (`next_run()`, `match()`, ...) and `noexcept`
   where it holds.

### Correctness and robustness

10. **Year cap without a year field.** Searches stop at the year 3000 (inherited from croner.js),
    so `0 0 1 1 *` has no next run after 2999, while the year field accepts 1-9999. Harmless in
    practice, but inconsistent. Now documented in the README.
11. **Lenient parsing inherited from JavaScript `parseInt` and `split`:** `FRI#` is accepted as
    `FRI`, `5#2#3` as `5#2`, and `L` anywhere in day of month is accepted (`1L` means day 1 or
    the last day). These should either be rejected or be documented as intended.
12. **`std::recursive_mutex` is not needed.** No locked function calls another locked function,
    and the user callbacks run without the lock. A plain `std::mutex` with
    `std::condition_variable` is simpler and makes lock misuse visible.
13. **A thread per run.** Fine for a few jobs, but the threads are only reaped when the next run
    starts, and there is no way to run jobs on an existing executor, thread pool or event loop.
14. **One-off jobs still parse a dummy pattern** (`CronPattern pattern_{"* * * * *"}`).

### Performance and footprint

15. `CronPattern` stores the year field as a `std::vector<std::uint8_t>(10000)`, a 10 KB heap
    allocation per pattern, and every other field as one byte per value. `std::bitset` would cut
    that to about 1.3 KB without a heap allocation, and makes copies cheap.
16. `match_day_value()` scans all 31 nearest-weekday flags for every candidate day. A flag for
    "pattern uses W" would skip it for the common case.

### Code and repository hygiene

17. Comments still say "a port of src/croner.ts" and refer to JavaScript (`Date.UTC()`,
    `Intl.DateTimeFormat`, the JavaScript test suite). They should describe the C++ code on its
    own terms; the differential tests document the relation to croner.js.
18. `CronError` and `Millis` are declared in `timezone.hpp`. A small `error.hpp` (or
    `common.hpp`) and a `version.hpp` would match what the headers are about. The version
    macros are only defined when the umbrella header is included.
19. No `.clang-format` or `.clang-tidy`, no `CHANGELOG.md`, no API documentation beyond the
    README.
20. CMake: `PROJECT_IS_TOP_LEVEL` (CMake 3.21) could replace the hand-written check. The Conan
    recipe has no C++17 check (`check_min_cppstd`), which Conan Center requires.

## Plan

### Phase 1: small fixes (done in 0.2.0)

- [x] Error messages with C++ names and without JavaScript class prefixes (8).
- [x] Replace `std::recursive_mutex` and `std::condition_variable_any` with `std::mutex` and
      `std::condition_variable` (12).
- [x] `std::bitset` for pattern fields (15), and skip the `W` scan when unused (16).
- [x] `[[nodiscard]]` on queries (9).
- [x] Rewrite comments that refer to the JavaScript sources (17).
- [x] Reject `FRI#` and `5#2#3` (11). `L` anywhere in day of month is kept, in step with the
      other implementations.
- [x] Searches without a year field continue past the year 3000 (10).
- [x] `.clang-format`, `CHANGELOG.md`, `check_min_cppstd` in `conanfile.py`, `error.hpp` and
      `version.hpp` (18, 19, 20).
- [ ] Raise the lenient `#` parsing upstream in croner.js.

### Phase 2: API for 0.2 (breaking, done in 0.2.0)

Split evaluation from execution, keeping the parser and search as they are. As implemented:

```cpp
// Immutable, copyable value. Thread-safe const queries, no threads, no global state.
croner::CronOptions opts;
opts.zone = croner::locate_zone("Europe/Stockholm");
croner::Cron cron("0 30 9 * * MON-FRI", opts);
cron.next_run();                          // std::optional<TimePoint>
cron.next_runs(5, from);
cron.match(tp);
for (auto run : cron.runs(tp)) { /* ... */ }

// Runs a function on a schedule. Movable handle, stops and joins on destruction.
croner::JobOptions job_opts;
job_opts.max_runs = 10;
job_opts.protect = true;
croner::Job job(cron, [] { /* ... */ }, job_opts);
job.pause(); job.resume(); job.stop(); job.join();
job.runs_left(); job.is_busy(); job.next_run();
```

- [x] `Cron` becomes the value type (1). The time zone is a single `TimeZonePtr` field (3), and
      the options are split into evaluation options (`mode`, `dom_and_dow`,
      `alternative_weekdays`, `sloppy_ranges`, `day_offset`, `start_at`, `stop_at`,
      `interval`) and job options (`max_runs`, `paused`, `protect`, `on_protect`,
      `on_error`, `allow_past`).
- [x] `Job` owns the threads, holds its state in a shared block so the handle can be moved
      (and a job function can destroy its own job), and accepts `void()` or `void(Job&)` job
      functions (6).
- [x] `std::chrono` durations for `interval` and `utc_offset` (4), and C++ names (5).
- [x] Remove the global named-job registry (2). Users who need lookup by name keep a map.
- [x] Move `CronDate`, `SearchOptions` and `CronPattern` to `detail` (7).
- [x] Error categories: `CronError::kind()` (8).
- [x] Keep the differential and OCPS drivers working; they only use the evaluation API.

### Phase 3: scheduling (0.3)

- [ ] Pluggable execution: an executor callback (`std::function<void(std::function<void()>)>`)
      so jobs can run on a thread pool, an `asio` strand or an event loop, with
      thread-per-run as the default (13).
- [ ] One scheduler thread shared by all jobs (a timer queue), instead of one per job.
- [ ] Reap finished worker threads when they finish, not when the next run starts.

### Phase 4: release 1.0

- [ ] API documentation (Doxygen or a page on croner.56k.guru).
- [ ] Single-header release asset.
- [ ] vcpkg and Conan Center submissions, see [PUBLISHING.md](PUBLISHING.md).
