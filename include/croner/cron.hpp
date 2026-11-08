// Croner for C++ - MIT License - Hexagon <github.com/Hexagon>
//
// croner::Cron, an immutable schedule: a cron pattern or a single date, in a time zone.

#ifndef CRONER_CRON_HPP
#define CRONER_CRON_HPP

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "date.hpp"
#include "error.hpp"
#include "pattern.hpp"
#include "timezone.hpp"

namespace croner {

using Clock = std::chrono::system_clock;
/// Instants returned by Croner, millisecond precision
using TimePoint = std::chrono::time_point<Clock, std::chrono::milliseconds>;

/// A date passed to Croner: a system_clock time point, or an ISO 8601 string such as
/// "2025-01-01", "2025-01-01T12:00:00", "2025-01-01T12:00:00.500Z" or
/// "2025-01-01T12:00:00+02:00". A string without "Z" or a UTC offset is a wall clock time in the
/// time zone of the schedule.
class DateRef {
 public:
  template <class Duration>
  DateRef(std::chrono::time_point<Clock, Duration> tp)  // NOLINT(google-explicit-constructor)
      : value_(std::chrono::floor<std::chrono::milliseconds>(tp).time_since_epoch().count()) {}
  DateRef(std::string iso) : value_(std::move(iso)) {}         // NOLINT
  DateRef(const char* iso) : value_(std::string(iso)) {}       // NOLINT
  DateRef(std::string_view iso) : value_(std::string(iso)) {}  // NOLINT

 private:
  friend class Cron;
  std::variant<detail::Millis, std::string> value_;
};

/// Options for a schedule. All are optional.
struct CronOptions {
  /// Time zone, e.g. croner::locate_zone("Europe/Stockholm") or
  /// croner::fixed_offset_zone(std::chrono::minutes(330)). Default: the system local time zone.
  TimeZonePtr zone;
  /// Which pattern lengths to accept
  Mode mode = Mode::Auto;
  /// Day-of-month AND day-of-week must match (default is OR, as in Vixie cron)
  bool dom_and_dow = false;
  /// Quartz weekday numbering, 1 = Sunday ... 7 = Saturday
  bool alternative_weekdays = false;
  /// Allow non-standard stepping like "/10" and "5/5"
  bool sloppy_ranges = false;
  /// Shift every run by a number of days
  int day_offset = 0;
  /// First allowed run, inclusive (with an interval: the first run, and the interval is counted
  /// from it)
  std::optional<DateRef> start_at;
  /// Runs must be before this, exclusive
  std::optional<DateRef> stop_at;
  /// Minimum time between runs, in whole seconds
  std::chrono::seconds interval{0};
};

/// ISO 8601 representation in UTC, e.g. "2025-01-01T00:00:00.000Z"
[[nodiscard]] inline std::string to_iso_string(TimePoint tp) {
  return detail::to_iso_string(tp.time_since_epoch().count());
}

class RunRange;
class Job;

/// A schedule: a cron pattern, or a single date, in a time zone.
///
/// Immutable and cheap to copy (copies share the parsed pattern). All member functions are
/// const and safe to call from several threads at once.
///
///     croner::Cron cron("0 30 9 * * MON-FRI");
///     auto next = cron.next_run();
class Cron {
 public:
  /// @throws CronError if the pattern or an option is invalid
  explicit Cron(std::string_view pattern, CronOptions options = {}) {
    auto d = make_data(std::move(options));
    detail::PatternOptions po;
    po.mode = d->options.mode;
    po.alternative_weekdays = d->options.alternative_weekdays;
    po.sloppy_ranges = d->options.sloppy_ranges;
    d->pattern.emplace(pattern, po);
    d->search.has_nearest_weekdays = d->pattern->nearest_weekdays.any();
    d_ = std::move(d);
  }

  /// A schedule with a single run, at `at`
  /// @throws CronError if the date or an option is invalid
  [[nodiscard]] static Cron once(const DateRef& at, CronOptions options = {}) {
    Cron c;
    auto d = make_data(std::move(options));
    d->once = to_cron_date(*d, at).get_time();
    c.d_ = std::move(d);
    return c;
  }

  /// The next run after `after` (default now). With an interval, `after` counts as a previous
  /// run.
  [[nodiscard]] std::optional<TimePoint> next_run(
      const std::optional<DateRef>& after = std::nullopt) const {
    return to_time_point(next_target(ref_ms(after), now_ms(), std::nullopt, false));
  }

  /// The next n runs after `after` (default now)
  [[nodiscard]] std::vector<TimePoint> next_runs(
      std::size_t n, const std::optional<DateRef>& after = std::nullopt) const {
    return enumerate_runs(n, ref_ms(after), 1);
  }

  /// The last run before `before` (default now)
  [[nodiscard]] std::optional<TimePoint> previous_run(
      const std::optional<DateRef>& before = std::nullopt) const {
    const auto runs = enumerate_runs(1, ref_ms(before), -1);
    if (runs.empty()) return std::nullopt;
    return runs.front();
  }

  /// The last n runs before `before` (default now), most recent first
  [[nodiscard]] std::vector<TimePoint> previous_runs(
      std::size_t n, const std::optional<DateRef>& before = std::nullopt) const {
    return enumerate_runs(n, ref_ms(before), -1);
  }

  /// Time from `after` (default now) until the next run
  [[nodiscard]] std::optional<std::chrono::milliseconds> time_to_next(
      const std::optional<DateRef>& after = std::nullopt) const {
    const detail::Millis now = now_ms();
    const auto reference = ref_ms(after);
    const auto next = next_target(reference, now, std::nullopt, false);
    if (!next) return std::nullopt;
    return std::chrono::milliseconds{*next - (reference ? *reference : now)};
  }

  /// Whether a date is a run of this schedule, at second precision
  [[nodiscard]] bool match(const DateRef& date) const {
    detail::CronDate check = to_cron_date(*d_, date);
    check.ms = 0;
    if (d_->once) {
      return check.get_time() == detail::floor_div(*d_->once, 1000) * 1000;
    }
    return check.match(*d_->pattern, d_->search);
  }

  /// Upcoming runs after `after` (default now), for use with range-for. Infinite for most
  /// patterns, so break out of the loop.
  [[nodiscard]] RunRange runs(const std::optional<DateRef>& after = std::nullopt) const;

  /// The pattern, with nicknames such as "@daily" expanded. Empty for a single-run schedule.
  [[nodiscard]] const std::string& pattern() const {
    static const std::string empty;
    return d_->pattern ? d_->pattern->pattern : empty;
  }

  /// The run of a single-run schedule, nullopt for patterns
  [[nodiscard]] std::optional<TimePoint> once_at() const { return to_time_point(d_->once); }

  [[nodiscard]] const CronOptions& options() const noexcept { return d_->options; }
  [[nodiscard]] const TimeZonePtr& zone() const noexcept { return d_->tz; }

 private:
  friend class RunRange;
  friend class Job;
  using Millis = detail::Millis;
  using CronDate = detail::CronDate;

  struct Data {
    CronOptions options;
    TimeZonePtr tz;
    detail::SearchOptions search;
    std::optional<detail::CronPattern> pattern;
    std::optional<Millis> once;
    std::optional<Millis> start_at;
    std::optional<Millis> stop_at;
    Millis start_at_rounding_ms = 0;
  };
  std::shared_ptr<const Data> d_;

  Cron() = default;

  static std::shared_ptr<Data> make_data(CronOptions options) {
    auto d = std::make_shared<Data>();
    d->options = std::move(options);
    const CronOptions& o = d->options;
    if (o.interval.count() < 0) {
      throw CronError(CronError::Kind::Option, "interval must not be negative");
    }
    d->tz = o.zone ? o.zone : local_zone();
    d->search.dom_and_dow = o.dom_and_dow;
    d->search.interval = static_cast<std::int64_t>(o.interval.count());

    // Runs are on whole seconds, so a time between two seconds counts as the next second
    const auto to_whole_second = [](Millis ms) {
      return detail::floor_mod(ms, 1000) != 0 ? detail::floor_div(ms, 1000) * 1000 + 1000 : ms;
    };
    if (o.start_at) {
      const Millis raw = to_cron_date(*d, *o.start_at).get_time();
      d->start_at = to_whole_second(raw);
      d->start_at_rounding_ms = *d->start_at - raw;
    }
    if (o.stop_at) d->stop_at = to_whole_second(to_cron_date(*d, *o.stop_at).get_time());
    return d;
  }

  static Millis now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
        .count();
  }

  static std::optional<TimePoint> to_time_point(const std::optional<Millis>& ms) {
    if (!ms) return std::nullopt;
    return TimePoint{std::chrono::milliseconds{*ms}};
  }

  static CronDate to_cron_date(const Data& d, const DateRef& ref) {
    if (const auto* ms = std::get_if<Millis>(&ref.value_)) return CronDate(*ms, d.tz);
    return CronDate(std::string_view(std::get<std::string>(ref.value_)), d.tz);
  }

  std::optional<Millis> ref_ms(const std::optional<DateRef>& ref) const {
    if (!ref) return std::nullopt;
    if (const auto* ms = std::get_if<Millis>(&ref->value_)) return *ms;
    return to_cron_date(*d_, *ref).get_time();
  }

  Millis offset_ms() const {
    return static_cast<Millis>(d_->options.day_offset) * detail::MS_PER_DAY;
  }

  std::optional<CronDate> shift(const std::optional<Millis>& ms) const {
    if (!ms) return std::nullopt;
    return CronDate(*ms - offset_ms(), d_->tz);
  }

  /// The next run after `reference`, or after `now` if there is no reference.
  /// `last_run` is the last run of a job using this schedule, and `allow_past` lets a single run
  /// in the past be returned (both for Job).
  std::optional<Millis> next_target(const std::optional<Millis>& reference, Millis now,
                                    const std::optional<Millis>& last_run, bool allow_past) const {
    const auto next = next_(shift(reference), now - offset_ms(), shift(d_->start_at),
                            shift(d_->stop_at), last_run, allow_past);
    if (!next) return std::nullopt;
    return next->get_time() + offset_ms();
  }

  std::optional<Millis> previous_target(const std::optional<Millis>& reference, Millis now) const {
    const auto previous =
        previous_(*shift(reference ? *reference : now), shift(d_->start_at), shift(d_->stop_at));
    if (!previous) return std::nullopt;
    return previous->get_time() + offset_ms();
  }

  std::vector<TimePoint> enumerate_runs(std::size_t n, std::optional<Millis> current,
                                        int direction) const {
    std::vector<TimePoint> out;
    const Millis now = now_ms();
    while (n-- > 0) {
      const auto run = direction == 1 ? next_target(current, now, std::nullopt, false)
                                      : previous_target(current, now);
      if (!run) break;
      out.push_back(*to_time_point(run));
      current = run;
    }
    return out;
  }

  std::optional<CronDate> next_(std::optional<CronDate> previous_run, Millis now,
                                const std::optional<CronDate>& start_at,
                                const std::optional<CronDate>& stop_at,
                                const std::optional<Millis>& last_run, bool allow_past) const {
    const bool is_enumerating = previous_run.has_value();
    bool has_previous_run = previous_run.has_value() || last_run.has_value();

    // With start_at and an interval, and no previous run: when the last run should have been
    bool start_at_in_future_with_interval = false;
    if (!previous_run && start_at && d_->search.interval) {
      previous_run = calculate_previous_run(*start_at, now);
      if (previous_run) has_previous_run = true;
      start_at_in_future_with_interval = !previous_run;
    }

    // Search from the exact reference instant
    CronDate prev = previous_run ? *previous_run : CronDate(now, d_->tz);
    Millis reference_ms = prev.get_time();

    // Never search from before start_at
    if (start_at && reference_ms < start_at->get_time()) {
      prev = start_at_in_future_with_interval ? *start_at
                                              : CronDate(start_at->get_time() - 1000, d_->tz);
      reference_ms = prev.get_time();
    }

    std::optional<CronDate> next_run;
    if (d_->once) {
      const Millis once_ms = *d_->once;
      const Millis past_ms = reference_ms - once_ms;
      if ((start_at && once_ms < start_at->get_time() - d_->start_at_rounding_ms) ||
          (last_run && once_ms + offset_ms() <= *last_run) ||
          (is_enumerating ? past_ms >= 0 : (past_ms > 1000 && !allow_past))) {
        return std::nullopt;
      }
      next_run = CronDate(once_ms, d_->tz);
    } else if (start_at_in_future_with_interval) {
      next_run = prev;
    } else {
      next_run = find_occurrence(prev, reference_ms, 1, has_previous_run);
    }

    if (!next_run || (stop_at && next_run->get_time() >= stop_at->get_time())) {
      return std::nullopt;
    }
    return next_run;
  }

  std::optional<CronDate> previous_(const CronDate& reference,
                                    const std::optional<CronDate>& start_at,
                                    const std::optional<CronDate>& stop_at) const {
    Millis reference_ms = reference.get_time();
    if (stop_at && reference_ms > stop_at->get_time()) reference_ms = stop_at->get_time();

    if (d_->once) {
      if (*d_->once < reference_ms) return CronDate(*d_->once, d_->tz);
      return std::nullopt;
    }

    const auto previous = find_occurrence(CronDate(reference_ms, d_->tz), reference_ms, -1, false);
    if (!previous || (start_at && previous->get_time() < start_at->get_time())) {
      return std::nullopt;
    }
    return previous;
  }

  /// The run after (direction 1) or before (-1) reference_ms, found by walking the pattern's
  /// wall clock times from start. Searches both passes of a repeated local time when the
  /// reference is on one of them.
  std::optional<CronDate> find_occurrence(const CronDate& start, Millis reference_ms, int direction,
                                          bool has_previous_run) const {
    const auto& tz = *d_->tz;
    const auto found = walk(start, reference_ms, direction, has_previous_run, false);
    // A fixed-time job never runs on the later pass, and a fixed UTC offset has no DST
    if ((d_->pattern->is_fixed_time && direction == 1) || tz.is_fixed()) return found;
    if (detail::fold_of(reference_ms, tz) !=
        (direction == 1 ? detail::Fold::Earlier : detail::Fold::Later)) {
      return found;
    }
    // The other pass starts when the clock goes back, and ends just before it
    const Millis ms = detail::floor_div(reference_ms, 1000) * 1000;
    const Millis edge = direction == 1
                            ? detail::find_offset_change(ms, ms + detail::MS_PER_DAY, tz)
                            : detail::find_offset_change(ms - detail::MS_PER_DAY, ms, tz) - 1000;
    // A match before the edge beats anything on the other pass
    if (found && (edge - found->get_time()) * direction > 0) return found;
    const auto other = walk(CronDate(edge, d_->tz), edge - direction, direction, false, true);
    if (!found || !other) return found ? found : other;
    return (found->get_time() - other->get_time()) * direction > 0 ? other : found;
  }

  std::optional<CronDate> walk(const CronDate& start, Millis reference_ms, int direction,
                               bool has_previous_run, bool inclusive) const {
    const auto& pattern = *d_->pattern;
    const auto step = [&](CronDate& d, bool has_previous) {
      return direction == 1 ? d.increment(pattern, d_->search, has_previous)
                            : d.decrement(pattern, d_->search);
    };
    CronDate candidate = start;
    bool ok = true;
    if (!inclusive || !candidate.match(pattern, d_->search)) {
      ok = step(candidate, has_previous_run);
    }
    while (ok) {
      if (const auto run_at = run_at_(candidate, reference_ms, direction)) {
        return CronDate(*run_at, d_->tz);
      }
      ok = step(candidate, false);
    }
    return std::nullopt;
  }

  /// When a candidate wall clock time runs, if it is ahead of the reference
  std::optional<Millis> run_at_(const CronDate& candidate, Millis reference_ms,
                                int direction) const {
    const auto& tz = *d_->tz;
    const auto is_ahead = [&](Millis d) { return (d - reference_ms) * direction > 0; };
    if (tz.is_fixed()) {
      const Millis d = candidate.get_time();
      if (is_ahead(d)) return d;
      return std::nullopt;
    }
    detail::DateTime local;
    local.year = candidate.year;
    local.month = candidate.month + 1;
    local.day = candidate.day;
    local.hour = candidate.hour;
    local.minute = candidate.minute;
    local.second = candidate.second;
    const auto r = detail::resolve_local(local, tz);
    const bool fixed_time = d_->pattern->is_fixed_time;
    if (r.kind == detail::LocalResolution::Single) {
      if (is_ahead(r.a)) return r.a;
      return std::nullopt;
    }
    if (r.kind == detail::LocalResolution::Ambiguous) {
      if (fixed_time) {
        if (is_ahead(r.a)) return r.a;
        return std::nullopt;
      }
      const Millis first = direction == 1 ? r.a : r.b;
      const Millis second = direction == 1 ? r.b : r.a;
      if (is_ahead(first)) return first;
      if (is_ahead(second)) return second;
      return std::nullopt;
    }
    // In a DST gap: fixed-time jobs run when the gap ends, on the same day
    if (!fixed_time) return std::nullopt;
    const Millis gap_end = detail::find_offset_change(r.a, r.b, tz);
    const detail::DateTime gl = detail::to_tz(gap_end, tz);
    const bool same_day = gl.year == local.year && gl.month == local.month && gl.day == local.day;
    if (same_day && is_ahead(gap_end)) return gap_end;
    return std::nullopt;
  }

  std::optional<CronDate> calculate_previous_run(const CronDate& start_at, Millis now) const {
    if (start_at.get_time() > now) return std::nullopt;
    const Millis interval_ms = d_->search.interval * 1000;
    CronDate previous = start_at;
    while (previous.get_time() + interval_ms <= now) {
      CronDate next = previous;
      if (!next.increment(*d_->pattern, d_->search, true)) break;
      previous = next;
    }
    return previous;
  }
};

/// Upcoming runs of a schedule. Usable with range-for:
///
///     for (auto run : cron.runs()) { ... }
///
/// Holds a copy of the schedule, so it may outlive the Cron it came from.
class RunRange {
 public:
  RunRange(Cron cron, const std::optional<DateRef>& after)
      : cron_(std::move(cron)), cursor_(cron_.ref_ms(after)), now_(Cron::now_ms()) {}

  /// The next run, nullopt when there are no more
  std::optional<TimePoint> next() {
    if (done_) return std::nullopt;
    const auto next_date = cron_.next_target(cursor_, now_, std::nullopt, false);
    if (!next_date) {
      done_ = true;
      return std::nullopt;
    }
    cursor_ = next_date;
    return Cron::to_time_point(next_date);
  }

  /// The next run without advancing
  [[nodiscard]] std::optional<TimePoint> peek() const {
    if (done_) return std::nullopt;
    return Cron::to_time_point(cron_.next_target(cursor_, now_, std::nullopt, false));
  }

  class iterator {
   public:
    using iterator_category = std::input_iterator_tag;
    using value_type = TimePoint;
    using difference_type = std::ptrdiff_t;
    using pointer = const TimePoint*;
    using reference = const TimePoint&;

    iterator() = default;
    explicit iterator(RunRange* range) : range_(range) { advance(); }
    reference operator*() const { return *value_; }
    pointer operator->() const { return &*value_; }
    iterator& operator++() {
      advance();
      return *this;
    }
    void operator++(int) { advance(); }
    friend bool operator==(const iterator& a, const iterator& b) {
      return a.value_.has_value() == b.value_.has_value() && (!a.value_ || a.range_ == b.range_);
    }
    friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }

   private:
    void advance() { value_ = range_ ? range_->next() : std::nullopt; }
    RunRange* range_ = nullptr;
    std::optional<TimePoint> value_;
  };

  iterator begin() { return iterator(this); }
  iterator end() { return iterator(); }

 private:
  Cron cron_;
  std::optional<detail::Millis> cursor_;
  detail::Millis now_;
  bool done_ = false;
};

inline RunRange Cron::runs(const std::optional<DateRef>& after) const {
  return RunRange(*this, after);
}

}  // namespace croner

#endif  // CRONER_CRON_HPP
