# Croner for C++

Croner is available for [JavaScript/TypeScript](https://github.com/Hexagon/croner) · [Rust](https://github.com/Hexagon/croner-rust) · [Python](https://github.com/Hexagon/croner-py) · [.NET](https://github.com/Hexagon/croner-dotnet) · **C++**

Parse and evaluate cron expressions, and run functions on a schedule. Header-only C++17, no
dependencies beyond the standard library.

- **Full cron syntax**, following the [Open Cron Pattern Standard](https://github.com/open-source-cron/ocps)
  (OCPS) 1.0 to 1.4: optional seconds and year fields, `L`, `W`, `LW`, `#`, `?`, `+` (AND of
  day-of-month and day-of-week), names (`MON`, `JAN`) and nicknames (`@daily`).
- **Time zones without dependencies.** Reads the system zoneinfo database on Linux, macOS and
  the BSDs, or uses C++20 `std::chrono::tzdb`. Fixed UTC offsets and custom time zone
  implementations are supported too.
- **Defined DST behaviour.** Fixed-time jobs run once when the clock goes back and still run
  when the clock skips their time; interval jobs follow the real clock (see [DST](#dst)).
- **Evaluation:** `croner::Cron` is an immutable, copyable value with thread-safe queries for
  next and previous runs, matching, and iteration with range-for.
- **Scheduling:** `croner::Job` runs a function on a schedule on background threads, with
  pause and resume, `max_runs`, overrun protection and error handlers.
- **Tested** with unit tests, the OCPS conformance test vectors, a zoneinfo reader checked against
  the C library for every zone, sanitizers, and differential testing against the other Croner
  implementations.

```cpp
#include <croner/croner.hpp>
#include <iostream>

int main() {
  croner::CronOptions opts;
  opts.zone = croner::locate_zone("Europe/Stockholm");

  // Evaluate
  const croner::Cron weekdays("0 30 9 * * MON-FRI", opts);
  for (auto run : weekdays.next_runs(5)) std::cout << croner::to_iso_string(run) << "\n";

  // Schedule
  croner::Job job(croner::Cron("*/5 * * * * *"), [] {
    std::cout << "This runs every five seconds\n";
  });
  job.join();  // Block for as long as the job has runs left
}
```

## Installation

The library is the headers in `include/croner`. Requires a C++17 compiler and threads. Any of these work:

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
find_package(croner 0.2 REQUIRED)
target_link_libraries(my_app PRIVATE croner::croner)
```

**Conan** (local recipe, `conan create .`), then `requires = "croner/0.2.0"`.

**Copy the headers** to your include path, and link threads (`-pthread`).

## Pattern syntax

```
┌──────────────── second (optional)       0-59
│ ┌────────────── minute                  0-59
│ │ ┌──────────── hour                    0-23
│ │ │ ┌────────── day of month            1-31, L, W, LW
│ │ │ │ ┌──────── month                   1-12 or JAN-DEC
│ │ │ │ │ ┌────── day of week             0-7 or SUN-SAT (0 and 7 are Sunday), #, L, +
│ │ │ │ │ │ ┌──── year (optional)         1-9999
│ │ │ │ │ │ │
* * * * * * *
```

Five fields are minute to day of week, six add seconds first, seven add the year last.

| Syntax      | Meaning                                                        | Example                        |
| ----------- | -------------------------------------------------------------- | ------------------------------ |
| `*` or `?`  | Any value                                                      | `* * * * *` every minute       |
| `a,b`       | List                                                           | `0 9,17 * * *`                 |
| `a-b`       | Range                                                          | `0 9 * * MON-FRI`              |
| `*/n`       | Every n-th value                                               | `*/15 * * * *`                 |
| `a-b/n`     | Every n-th value in a range                                    | `0 8-18/2 * * *`               |
| `L`         | Last day of the month (day of month)                           | `0 0 L * *`                    |
| `nW`        | Weekday (Mon-Fri) nearest to day n, within the month           | `0 0 15W * *`                  |
| `LW`        | Last weekday of the month                                      | `0 0 LW * *`                   |
| `day#n`     | n-th occurrence of a weekday in the month                      | `0 0 * * FRI#2`                |
| `dayL`      | Last occurrence of a weekday in the month                      | `0 0 * * 5L`                   |
| `+`         | Day of month **and** day of week must both match               | `0 0 1-7 * +MON`               |

When both day of month and day of week are restricted, a day matches if **either** does, as in
Vixie cron. Use `+` in the day-of-week field, or the `dom_and_dow` option, to require both.

Nicknames: `@yearly` / `@annually`, `@monthly`, `@weekly`, `@daily` / `@midnight`, `@hourly`.

## Evaluating schedules

`croner::Cron` is a schedule: a pattern, or a single date, in a time zone. It is immutable and
cheap to copy, and all its member functions are `const` and safe to call from several threads.

```cpp
const croner::Cron cron("0 0 12 * * FRI#2");    // second Friday of each month, at noon

cron.next_run();                                // std::optional<croner::TimePoint>
cron.next_run("2025-01-01T00:00:00");           // after a date (wall clock time in the zone)
cron.next_runs(10);                             // std::vector<croner::TimePoint>
cron.previous_run();                            // std::optional<croner::TimePoint>
cron.previous_runs(3, "2025-06-01T00:00:00Z");  // most recent first
cron.time_to_next();                            // std::optional<std::chrono::milliseconds>
cron.match("2025-01-10T12:00:00");              // bool

for (auto run : cron.runs()) {                  // infinite for most patterns, so break
  if (run > some_time_point) break;
}
```

`croner::TimePoint` is a `std::chrono::system_clock` time point with millisecond precision.
Dates passed to Croner (`croner::DateRef`) can be any `system_clock` time point, or an ISO 8601
string: `"2025-01-01"`, `"2025-01-01T12:00:00"`, `"2025-01-01T12:00:00.500Z"`,
`"2025-01-01T12:00:00+02:00"`. A string without `Z` or an offset, including a date-only string,
is a wall clock time in the time zone of the schedule.

A schedule with a single run is made with `Cron::once()`:

```cpp
const auto launch = croner::Cron::once("2030-01-01T00:00:00");
const auto soon = croner::Cron::once(std::chrono::system_clock::now() + std::chrono::minutes(5));
```

## Running jobs

`croner::Job` runs a function on a schedule. Each run starts a new thread, so a slow function
doesn't delay the schedule; runs may overlap unless `protect` is set. The function is called as
`f()` or `f(croner::Job&)`.

```cpp
croner::JobOptions opts;
opts.max_runs = 10;
opts.protect = true;                           // skip a run while the previous one is busy
opts.on_error = [](std::exception_ptr e, croner::Job& self) { /* log */ };

croner::Job job(croner::Cron("* * * * * *"), [](croner::Job& self) {
  if (self.runs_left() == 0) { /* last run */ }
}, opts);

job.pause();
job.resume();
job.trigger();      // run now, on the calling thread
job.next_run();     // std::optional<croner::TimePoint>
job.is_running();   // not paused or stopped, and has a next run
job.is_busy();      // a job function is running
job.stop();         // permanent; a running job function finishes
job.join();         // wait until there are no more runs
```

`Job` is a move-only handle, so it can be returned from functions and kept in containers.
Destroying it stops the job and waits for running job functions to finish. A job function may
stop, or even destroy, its own job.

Without `on_error`, an exception thrown by a job function on a worker thread calls
`std::terminate`, like an unhandled exception in any `std::thread`. Use an empty handler to
ignore exceptions. `trigger()` lets exceptions propagate to the caller.

## Options

All options are optional.

`croner::CronOptions`, for `croner::Cron`:

| Option                 | Type                       | Default    | Description                                                     |
| ---------------------- | -------------------------- | ---------- | --------------------------------------------------------------- |
| `zone`                 | `croner::TimeZonePtr`      | local time | Time zone, see [Time zones](#time-zones)                        |
| `mode`                 | `croner::Mode`             | `Auto`     | Accepted pattern lengths: `Auto`, `FivePart`, `SixPart`, ...    |
| `dom_and_dow`          | `bool`                     | `false`    | Require both day of month and day of week to match              |
| `alternative_weekdays` | `bool`                     | `false`    | Quartz weekday numbering, 1 = Sunday ... 7 = Saturday           |
| `sloppy_ranges`        | `bool`                     | `false`    | Accept non-standard steps like `/10` and `5/5`                  |
| `day_offset`           | `int`                      | `0`        | Shift every run by a number of days                             |
| `start_at`             | `std::optional<DateRef>`   |            | First allowed run, inclusive                                    |
| `stop_at`              | `std::optional<DateRef>`   |            | Runs must be before this, exclusive                             |
| `interval`             | `std::chrono::seconds`     | `0s`       | Minimum time between runs                                       |

`croner::JobOptions`, for `croner::Job`:

| Option        | Type                                         | Default   | Description                                                |
| ------------- | -------------------------------------------- | --------- | ---------------------------------------------------------- |
| `max_runs`    | `std::optional<std::int64_t>`                | unlimited | Number of scheduled runs before the job stops              |
| `paused`      | `bool`                                       | `false`   | Create the job paused                                      |
| `protect`     | `bool`                                       | `false`   | Skip a run while the previous one is still running         |
| `on_protect`  | `std::function<void(Job&)>`                  |           | Called when a run is skipped by `protect` (implies it)     |
| `on_error`    | `std::function<void(std::exception_ptr, Job&)>` |        | Called with exceptions thrown by the job function          |
| `allow_past`  | `bool`                                       | `false`   | Let a single-run schedule dated in the past run at once    |

## Errors

Invalid patterns, dates, time zones and options throw `croner::CronError`, which derives from
`std::runtime_error`. `kind()` tells what was invalid: `CronError::Kind::Pattern`, `Date`,
`TimeZone` or `Option`.

```cpp
try {
  croner::Cron cron("0 0 * * 9");
} catch (const croner::CronError& e) {
  // e.kind() == croner::CronError::Kind::Pattern
  // e.what() == "invalid cron pattern '0 0 * * 9': day of week 9 is out of range"
}
```

## Time zones

A time zone is a `croner::TimeZone`, which answers one question: what is the UTC offset at an
instant? Local times, DST gaps and repeated hours are derived from that.

- `croner::locate_zone("America/New_York")` reads the system zoneinfo database (`/usr/share/zoneinfo`,
  or `$TZDIR`) on Linux, macOS and the BSDs, including the POSIX TZ rule that continues each zone
  after its last listed transition. The reader is checked against the C library for every zone
  and every transition from 1900 to 2200 (`tests/tz_compare.cpp`). Zones are cached, and an
  unknown name throws `croner::CronError`.
- On Windows, built as C++20 with MSVC, zones come from `std::chrono::tzdb`. Define
  `CRONER_USE_STD_CHRONO_TZDB=1` to use `std::chrono::tzdb` on other platforms.
- `croner::fixed_offset_zone(std::chrono::minutes(330))` is a fixed offset, here +05:30.
- `croner::utc_zone()` and `croner::local_zone()` are UTC and the system local time zone. No
  time zone means the system local time zone.
- Your own subclass of `croner::TimeZone`, for example on embedded systems without a zoneinfo
  database, needs only `utc_offset()` and `name()`.

`"UTC"` and `"Etc/UTC"` work even without a time zone database. Zone names are case-sensitive,
as in the zoneinfo database.

### DST

- Fixed-time jobs (a single second, minute and hour) run once in a repeated hour, on the first
  pass, and run when a DST gap ends if their time is in the gap.
- Interval jobs (wildcard, step, range or list in second, minute or hour) run on both passes of a
  repeated hour, and skip times in a DST gap.

This follows Vixie cron, and deliberately deviates from OCPS 1.4 §4.3.1. All Croner
implementations behave this way.

## Year range

The year field accepts 1-9999, wider than the OCPS standard range of 1970-2199. Steps from a
wildcard are counted from year 0, so `*/2` matches even years, the same years as in the standard
range (OCPS 1.4 §4.3.2). Explicit ranges step from their start: `1971-2199/2` is odd years.
Searches without a year field look at most 400 years ahead or back.

## Standards and compatibility

Checked in CI:

- [OCPS](https://github.com/open-source-cron/ocps) conformance test vectors
  (`tests/ocps/ocps.ts`): all pass, except three deliberate deviations shared with the other
  Croner implementations: nicknames are case-insensitive (`@Daily`), `@reboot` is rejected when
  the pattern is parsed (there is no startup event to run on), and `?` is accepted in every
  field as an alias for `*`.
- Differential testing (`tests/differential`): random patterns, options, time zones and dates
  are evaluated by this library, croner.js and croner-rust, and the results compared.

Known differences from the other implementations, all deliberate:

- Days that don't exist in a month never skip the first days of the next month (croner.js 11.0.0
  rolls e.g. February 31 over to March 3, so `0 0 1,31 * *` searched from February 2 skips
  March 1).
- `@weekly` works with `alternative_weekdays`, years 1-99 work in the year field, and searches
  without a year field continue past the year 3000.
- The nth weekday must be a single `#` followed by a number: `FRI#` and `FRI#2#3` are rejected
  (croner.js reads them as `FRI` and `FRI#2`).
- croner-rust rejects some patterns that are accepted here, like `*,5`, and has two open issues
  that are fixed here: `*/2` in the year field matches odd years, and searches can stop early
  around the day Samoa skipped in 2011.
- Before 1900, results can differ where the time zone databases differ.

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

## License

MIT, see [LICENSE](LICENSE).
