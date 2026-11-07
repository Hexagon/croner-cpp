# Changelog

## 0.2.0

A new API that separates evaluating a schedule from running a job. This release breaks
compatibility with 0.1.0.

### Changed

- `croner::Cron` is now an immutable, copyable schedule with only `const`, thread-safe member
  functions. It no longer runs functions, owns threads or keeps run state.
- `croner::Job` runs a function on a `Cron`. It is a move-only handle, and its job function is
  called as `f()` or `f(croner::Job&)`. A job function may destroy its own job.
- `croner::Options` is split into `croner::CronOptions` (pattern, time zone and search options)
  and `croner::JobOptions` (`max_runs`, `paused`, `protect`, `on_protect`, `on_error`,
  `allow_past`).
- The time zone is a single `CronOptions::zone` (`croner::TimeZonePtr`), set with
  `croner::locate_zone()`, `croner::fixed_offset_zone()`, `croner::utc_zone()` or your own
  `croner::TimeZone`. The `timezone` and `utc_offset` options are removed.
- `interval` is a `std::chrono::seconds`, and `fixed_offset_zone()` takes a
  `std::chrono::minutes`.
- Single-run schedules are made with `Cron::once(date)`. A string with a `:` is no longer read as
  a date by the `Cron` constructor.
- Renamed: `get_pattern()` to `pattern()`, `get_once()` to `once_at()`, `ms_to_next()` to
  `time_to_next()`, `enumerate()` to `runs()` (returning `croner::RunRange`).
- `pause()` and `resume()` return `void`.
- `CronError` has a `kind()`: `Pattern`, `Date`, `TimeZone` or `Option`. Error messages use the
  C++ names and say what was wrong, e.g. `invalid cron pattern '0 0 * * 9': day of week 9 is out
  of range`.
- Internal types (`CronPattern`, `CronDate`, `PatternOptions`, `SearchOptions`, `Millis`) moved to
  `croner::detail`.
- Searches without a year field continue past the year 3000, up to the year 9999.
- The nth weekday must be a single `#` and a number: `FRI#` and `FRI#2#3` are rejected.

### Added

- `Cron::previous_run()`, `Job::next_run()`, `Job::is_paused()`.
- `error.hpp` and `version.hpp`, `[[nodiscard]]` on queries, `.clang-format`.

### Removed

- The global registry of named jobs: the `name` option and `croner::scheduled_jobs()`. Keep your
  own map of jobs if you need to look them up by name.
- `Options::catch_errors` (use an empty `on_error` to ignore exceptions), `Cron::schedule()`
  (a `Job` starts when it is created), `DateRef::from_unix_ms()`, `RunRange::reset()`.

### Internal

- Pattern fields are bitsets: a parsed pattern is about 1.3 KB without heap allocations, down
  from 10 KB on the heap, and copies of a `Cron` share it.
- A plain `std::mutex` and `std::condition_variable` replace the recursive mutex.
- Each run gets its own copy of the job function.

## 0.1.0

First release.
