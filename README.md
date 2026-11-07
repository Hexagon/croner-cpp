# Croner for C++

Trigger functions and/or evaluate cron expressions in C++. Header-only, C++17, no dependencies.

A port of [Croner](https://github.com/Hexagon/croner) for JavaScript. It uses the same algorithms
and gives the same results (apart from a few bugs fixed here, see below), which is checked by a
differential test that runs random patterns, options, time zones and dates through croner.js,
croner-rust and this port.

- Follows the [Open Cron Pattern Standard](https://github.com/open-source-cron/ocps) (OCPS) 1.0
  to 1.4: seconds and year fields, `L`, `W`, `LW`, `#`, `?`, `+` (AND), nicknames such as
  `@daily`. DST handling follows Vixie cron and croner-rust (see "DST" below).
- IANA time zones with no dependencies. It reads the system zoneinfo database, or uses
  C++20 `std::chrono::tzdb`.
- Evaluates next and previous runs, matches dates, and iterates over runs.
- Schedules job functions on background threads, with pause, resume, stop, `max_runs`,
  `protect` (overrun protection), error handlers, `start_at`/`stop_at` and `interval`.

```cpp
#include <croner/croner.hpp>
#include <iostream>

int main() {
  croner::Options opts;
  opts.timezone = "Europe/Stockholm";

  // Evaluate
  croner::Cron weekdays("0 30 9 * * MON-FRI", opts);
  for (auto run : weekdays.next_runs(5)) std::cout << croner::to_iso_string(run) << "\n";

  // Schedule
  croner::Cron job("*/5 * * * * *", opts, [](croner::Cron& self) {
    std::cout << "This runs every five seconds\n";
  });
  job.join();  // Block for as long as the job has runs left
}
```

## Installation

The library is the five headers in `include/croner`. Any of these work:

**CMake FetchContent**

```cmake
include(FetchContent)
FetchContent_Declare(croner
  GIT_REPOSITORY https://github.com/Hexagon/croner-cpp.git
  GIT_TAG        <tag or commit>)
FetchContent_MakeAvailable(croner)
target_link_libraries(my_app PRIVATE croner::croner)
```

**Installed package**

```sh
cmake -S . -B build && cmake --install build --prefix /usr/local
```

```cmake
find_package(croner 0.1 REQUIRED)
target_link_libraries(my_app PRIVATE croner::croner)
```

**Conan** (local recipe, `conan create .`), then `requires = "croner/0.1.0"`.

**Copy the headers** to your include path, and link threads (`-pthread`).

## Evaluating patterns

```cpp
croner::Cron job("0 0 12 * * FRI#2");           // second Friday of each month, at noon

job.next_run();                                 // std::optional<croner::TimePoint>
job.next_run("2025-01-01T00:00:00");            // after a date (wall clock time in the job's zone)
job.next_runs(10);                              // std::vector<croner::TimePoint>
job.previous_runs(3, "2025-06-01T00:00:00Z");   // most recent first
job.ms_to_next();                               // std::optional<std::chrono::milliseconds>
job.match("2025-01-10T12:00:00");               // bool

for (auto run : job.enumerate()) {              // infinite for most patterns, so break
  if (run > some_time_point) break;
}
```

`croner::TimePoint` is a `std::chrono::system_clock` time point with millisecond precision.
Reference dates (`croner::DateRef`) can be any `system_clock` time point, or an ISO 8601
string: `"2025-01-01"`, `"2025-01-01T12:00:00"`, `"2025-01-01T12:00:00.500Z"`,
`"2025-01-01T12:00:00+02:00"`. A string without `Z` or an offset is a wall clock time in the time
zone of the job.

A string containing a `:` (or a time point) creates a one-off job instead of a pattern:

```cpp
croner::Cron once("2030-01-01T00:00:00", [](croner::Cron&) { /* ... */ });
```

## Scheduling

Job functions run on a new thread for each run, so they don't delay the schedule. The job is
stopped when the `Cron` object is destroyed, which waits for running job functions to finish.

```cpp
croner::Options opts;
opts.max_runs = 10;
opts.protect = true;                           // skip a run while the previous one is busy
opts.on_error = [](std::exception_ptr e, croner::Cron& self) { /* log */ };

croner::Cron job("* * * * * *", opts, [](croner::Cron& self) {
  if (self.runs_left() == 0) { /* last run */ }
});

job.pause();
job.resume();
job.trigger();      // run now, on the calling thread
job.is_running();   // scheduled, not paused or stopped, and has a next run
job.is_busy();      // a job function is running
job.stop();         // permanent; a running job function finishes
job.join();         // wait until there are no more runs
```

Without `catch_errors` or `on_error`, an exception thrown by a job function on the scheduler's
worker thread calls `std::terminate`, like an unhandled exception in any `std::thread` (and like
an unhandled promise rejection crashes Node.js). `trigger()` lets exceptions propagate to the
caller.

A `Cron` can't be copied or moved, as running job functions refer to it. Use
`std::unique_ptr<croner::Cron>` to store jobs in containers. Don't destroy a job from inside its
own job function; call `self.stop()` instead.

## Options

| JavaScript            | C++                                   | Notes                                              |
| --------------------- | ------------------------------------- | -------------------------------------------------- |
| `name`                | `name`                                | Named jobs are listed by `croner::scheduled_jobs()` |
| `paused`              | `paused`                              |                                                    |
| `maxRuns`             | `max_runs`                            | `std::optional`, unlimited if not set               |
| `catch`               | `catch_errors`, `on_error`            |                                                    |
| `interval`            | `interval`                            | seconds                                            |
| `protect`             | `protect`, `on_protect`               |                                                    |
| `startAt`, `stopAt`   | `start_at`, `stop_at`                 | `croner::DateRef`                                   |
| `timezone`            | `timezone`                            | IANA name, default is the system local time zone   |
| `utcOffset`           | `utc_offset`                          | minutes                                            |
|                       | `zone`                                | your own `croner::TimeZone` implementation         |
| `domAndDow`           | `dom_and_dow`                         |                                                    |
| `dayOffset`           | `day_offset`                          |                                                    |
| `mode`                | `mode`                                | `croner::Mode::Auto`, `FivePart`, ...               |
| `alternativeWeekdays` | `alternative_weekdays`                |                                                    |
| `sloppyRanges`        | `sloppy_ranges`                       |                                                    |
| `allowPast`           | `allow_past`                          |                                                    |
| `context`             |                                       | capture state in the lambda instead                |
| `unref`               |                                       | see `join()` and `stop()`                           |
| `legacyMode`          |                                       | deprecated in JavaScript, use `dom_and_dow`         |

Methods are named in snake_case: `nextRun()` is `next_run()`, `previousRuns()` is
`previous_runs()` and so on. Invalid patterns, options, dates and time zones throw
`croner::CronError`.

The pattern syntax is the same as for Croner in JavaScript, see the
[pattern documentation](https://croner.56k.guru/usage/pattern/).

## Time zones

Time zones are a `croner::TimeZone` that answers one question: what is the UTC offset at an
instant? Local times, DST gaps and repeated hours are derived from that, as in the JavaScript
version.

- `opts.timezone = "America/New_York"` reads the system zoneinfo database (`/usr/share/zoneinfo`,
  or `$TZDIR`) on Linux, macOS and the BSDs, including the POSIX TZ rule that continues each zone
  after its last listed transition. The reader is checked against the C library for every zone
  and every transition from 1900 to 2200 (`tests/tz_compare.cpp`).
- On Windows, built as C++20 with MSVC, zones come from `std::chrono::tzdb`. Define
  `CRONER_USE_STD_CHRONO_TZDB=1` to use `std::chrono::tzdb` on other platforms.
- `opts.utc_offset = 330` is a fixed offset in minutes.
- No time zone means the system local time zone.
- `opts.zone` takes your own implementation, for example on embedded systems without a zoneinfo
  database.

`"UTC"` and `"Etc/UTC"` work even without a time zone database.

### DST

As in Croner for JavaScript and croner-rust, which deviate from OCPS 1.4 §4.3.1:

- Fixed-time jobs (a single second, minute and hour) run once in a repeated hour, on the first
  pass, and run when a DST gap ends if their time is in the gap.
- Interval jobs (wildcard, step, range or list in second, minute or hour) run on both passes of a
  repeated hour, and skip times in a DST gap.

## Year range

The year field accepts 1-9999, wider than the OCPS standard range of 1970-2199. Steps from a
wildcard are counted from year 0, so `*/2` matches even years, the same years as in the standard
range (OCPS 1.4 section 4.3.2). Explicit ranges step from their start: `1971-2199/2` is odd years.
Searches without a year field look at most 400 years ahead or back.

## Standards and compatibility

Checked in CI:

- [OCPS](https://github.com/open-source-cron/ocps) conformance test vectors
  (`tests/ocps/ocps.ts`): all pass, except three deliberate deviations shared with croner.js and
  croner-rust: nicknames are case-insensitive (`@Daily`), `@reboot` is rejected when the pattern
  is parsed (there is no startup event to run on), and `?` is accepted in every field as an alias
  for `*`.
- Results are compared with croner.js and croner-rust for random patterns, options, time zones
  and dates (`tests/differential`). Expected differences with croner-rust are parsing
  strictness (croner-rust rejects some patterns croner.js accepts, like `*,5`), tz data versions,
  and two croner-rust issues: `*/2` in the year field matches odd years, and searches can stop
  early around the day Samoa skipped in 2011.

## Differences from Croner for JavaScript

Fixed here, but present in croner.js 11.0.0:

- Days that don't exist in a month don't skip the first days of the next month. croner.js tries
  e.g. February 31, rolls over to March 3, and so `0 0 1,31 * *` searched from February 2 skips
  March 1.
- `@weekly` works with `alternative_weekdays` (croner.js rejects it, as it expands to `0`).
- Years 1-99 in the year field work. In JavaScript, `Date.UTC()` reads them as 1900-1999, so they
  never match.

Other differences:

- A date-only string, `"2025-01-01"`, is midnight in the job's time zone. JavaScript reads it as
  UTC midnight.
- Time zone names are looked up in the zoneinfo database, where they are case-sensitive
  (`Intl` in JavaScript accepts any case).
- Before 1900, results can differ where the time zone databases differ (Node.js ships ICU's
  tzdata, with historic local mean times that some systems don't have).

## Tests

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

- `tests/croner_test.cpp`, unit tests, including scheduling.
- `tests/differential`, compares results with croner.js (from npm) for random patterns,
  options, time zones and dates:

  ```sh
  cd tests/differential && npm install
  npx tsx fuzz.ts ../../build/croner_differential_driver 50000
  ```

  Set `RUST_DRIVER` to also compare with croner-rust (`cargo build --release` in
  `tests/differential/rust`).
- `tests/ocps`, runs the OCPS conformance test vectors. They are not copied into this repository
  (they are licensed CC BY-SA 4.0), pass the path of an OCPS checkout:

  ```sh
  npx tsx tests/ocps/ocps.ts build/croner_ocps_driver path/to/ocps
  ```

- `tests/tz_compare.cpp`, compares the zoneinfo reader with the C library for every zone.

## Other implementations

Croner is available in several languages. The implementations share pattern syntax, DST rules and test cases:

- **JavaScript / TypeScript**: [croner](https://github.com/Hexagon/croner), Node, Deno, Bun and browsers, on [npm](https://www.npmjs.com/package/croner) and [JSR](https://jsr.io/@hexagon/croner)
- **Rust**: [croner-rust](https://github.com/Hexagon/croner-rust), on [crates.io](https://crates.io/crates/croner)
- **Python**: [croner-py](https://github.com/Hexagon/croner-py), on [PyPI](https://pypi.org/project/croner/)
- **.NET**: [croner-dotnet](https://github.com/Hexagon/croner-dotnet), on [NuGet](https://www.nuget.org/packages/Croner)
- **C++**: croner-cpp (this repository)

## License

MIT, see [LICENSE](LICENSE).
