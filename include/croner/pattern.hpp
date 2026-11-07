// Croner for C++ - MIT License - Hexagon <github.com/Hexagon>
//
// Cron pattern parser. Turns a pattern into one set of allowed values per field.

#ifndef CRONER_PATTERN_HPP
#define CRONER_PATTERN_HPP

#include <array>
#include <bitset>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error.hpp"

namespace croner {

/// Which pattern lengths are accepted.
enum class Mode {
  Auto,             ///< 5, 6 or 7 fields
  FivePart,         ///< 5 fields only. Seconds are always 0 and any year matches.
  SixPart,          ///< 6 fields only. Any year matches.
  SevenPart,        ///< 7 fields only
  FiveOrSixParts,   ///< 5 or 6 fields. Any year matches.
  SixOrSevenParts,  ///< 6 or 7 fields
};

namespace detail {

/// Bitmasks for the nth weekday of the month (FRI#2) and the last one (FRIL)
inline constexpr std::uint8_t LAST_OCCURRENCE = 0b100000;
inline constexpr std::uint8_t ANY_OCCURRENCE = 0b111111;
inline constexpr std::uint8_t OCCURRENCE_BITMASKS[5] = {0b00001, 0b00010, 0b00100, 0b01000,
                                                        0b10000};

inline constexpr int MAX_YEAR = 9999;

/// Options affecting how a pattern is parsed
struct PatternOptions {
  Mode mode = Mode::Auto;
  /// Quartz weekday numbering, 1 = Sunday ... 7 = Saturday
  bool alternative_weekdays = false;
  /// Allow non-standard stepping like "/10" and "5/5"
  bool sloppy_ranges = false;
};

/// A parsed cron pattern.
///
/// Each field is a set of the values that match. Days of month and months are stored with
/// index 0 being the 1st and January. day_of_week holds, per weekday, a bitmask of which
/// occurrences in the month match (ANY_OCCURRENCE for all).
class CronPattern {
 public:
  /// The pattern, with nicknames such as "@daily" expanded
  std::string pattern;

  std::bitset<60> second;
  std::bitset<60> minute;
  std::bitset<24> hour;
  std::bitset<31> day;
  std::bitset<12> month;
  std::array<std::uint8_t, 7> day_of_week{};
  std::bitset<MAX_YEAR + 1> year;  // index = year
  std::bitset<31> nearest_weekdays;

  bool last_day_of_month = false;
  bool last_weekday = false;
  bool star_dom = false;
  bool star_dow = false;
  bool star_year = false;
  /// "+" in day-of-week: day-of-month AND day-of-week
  bool use_and_logic = false;
  /// Seconds, minutes and hours each have a single value, without wildcard or step
  bool is_fixed_time = false;

  /// @throws CronError on invalid patterns
  explicit CronPattern(std::string_view pattern_in, PatternOptions options = {})
      : pattern(pattern_in), options_(options) {
    parse();
  }

 private:
  PatternOptions options_;

  enum class Part { Second, Minute, Hour, Day, Month, DayOfWeek, NearestWeekdays, Year };

  static int length(Part p) {
    switch (p) {
      case Part::Second:
      case Part::Minute: return 60;
      case Part::Hour: return 24;
      case Part::Day:
      case Part::NearestWeekdays: return 31;
      case Part::Month: return 12;
      case Part::DayOfWeek: return 7;
      case Part::Year: return MAX_YEAR + 1;
    }
    return 0;
  }

  void set_bit(Part p, std::size_t i) {
    switch (p) {
      case Part::Second: second.set(i); break;
      case Part::Minute: minute.set(i); break;
      case Part::Hour: hour.set(i); break;
      case Part::Day: day.set(i); break;
      case Part::Month: month.set(i); break;
      case Part::NearestWeekdays: nearest_weekdays.set(i); break;
      case Part::Year: year.set(i); break;
      case Part::DayOfWeek: day_of_week[i] = ANY_OCCURRENCE; break;
    }
  }

  static const char* part_name(Part p) {
    switch (p) {
      case Part::Second: return "second";
      case Part::Minute: return "minute";
      case Part::Hour: return "hour";
      case Part::Day: return "day of month";
      case Part::Month: return "month";
      case Part::DayOfWeek: return "day of week";
      case Part::NearestWeekdays: return "nearest weekday";
      case Part::Year: return "year";
    }
    return "";
  }

  [[noreturn]] void fail(const std::string& msg) const {
    throw CronError(CronError::Kind::Pattern, "invalid cron pattern '" + pattern + "': " + msg);
  }

  static std::string to_upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
  }
  static std::string to_lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
  }
  static bool contains(std::string_view s, char c) { return s.find(c) != std::string_view::npos; }

  static std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
  }

  static std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
      const std::size_t p = s.find(sep, start);
      if (p == std::string_view::npos) {
        out.emplace_back(s.substr(start));
        return out;
      }
      out.emplace_back(s.substr(start, p - start));
      start = p + 1;
    }
  }

  /// Leading whitespace, an optional sign, then digits up to the first non-digit. nullopt if
  /// there are no digits. Saturates instead of overflowing.
  static std::optional<std::int64_t> parse_int(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
      neg = s[i] == '-';
      ++i;
    }
    const std::size_t start = i;
    std::int64_t v = 0;
    while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
      if (v < 1000000000000LL) v = v * 10 + (s[i] - '0');
      ++i;
    }
    if (i == start) return std::nullopt;
    return neg ? -v : v;
  }

  /// Case-insensitive replace of all occurrences
  static std::string replace_all_ci(const std::string& s, std::string_view from,
                                    std::string_view to) {
    std::string out;
    const std::string upper = to_upper(s);
    const std::string from_upper = to_upper(std::string(from));
    std::size_t pos = 0;
    for (;;) {
      const std::size_t p = upper.find(from_upper, pos);
      if (p == std::string::npos) break;
      out.append(s, pos, p - pos);
      out.append(to);
      pos = p + from.size();
    }
    out.append(s, pos, std::string::npos);
    return out;
  }

  template <std::size_t N>
  static std::string replace_alpha(std::string conf,
                                   const std::array<std::pair<const char*, const char*>, N>& map) {
    for (const auto& [name, value] : map) conf = replace_all_ci(conf, name, value);
    return conf;
  }

  void parse() {
    // @yearly, @monthly etc
    if (contains(pattern, '@')) pattern = trim(expand_nickname(pattern));

    // Split on any whitespace
    std::vector<std::string> parts;
    {
      std::size_t i = 0;
      while (i < pattern.size()) {
        while (i < pattern.size() && std::isspace(static_cast<unsigned char>(pattern[i]))) ++i;
        const std::size_t start = i;
        while (i < pattern.size() && !std::isspace(static_cast<unsigned char>(pattern[i]))) ++i;
        if (i > start) parts.push_back(pattern.substr(start, i - start));
      }
      if (parts.empty()) parts.emplace_back("");
    }
    const std::size_t original_count = parts.size();

    if (parts.size() < 5 || parts.size() > 7) {
      fail("expected 5, 6 or 7 fields separated by whitespace, got " +
           std::to_string(original_count));
    }

    const Mode mode = options_.mode;
    if (mode != Mode::Auto) {
      bool valid = false;
      const char* expected = "";
      switch (mode) {
        case Mode::FivePart:
          valid = original_count == 5;
          expected = "5";
          break;
        case Mode::SixPart:
          valid = original_count == 6;
          expected = "6";
          break;
        case Mode::SevenPart:
          valid = original_count == 7;
          expected = "7";
          break;
        case Mode::FiveOrSixParts:
          valid = original_count == 5 || original_count == 6;
          expected = "5 or 6";
          break;
        case Mode::SixOrSevenParts:
          valid = original_count == 6 || original_count == 7;
          expected = "6 or 7";
          break;
        case Mode::Auto: break;
      }
      if (!valid) {
        fail(std::string("mode requires ") + expected + " fields, got " +
             std::to_string(original_count));
      }
    }

    // Without seconds, seconds are 0. Without a year, any year matches.
    if (parts.size() == 5) parts.insert(parts.begin(), "0");
    if (parts.size() == 6) parts.emplace_back("*");

    // '?' is an alias for '*'
    if (contains(pattern, '?')) {
      for (auto& p : parts)
        for (auto& c : p)
          if (c == '?') c = '*';
    }

    // L and LW in day-of-month
    if (to_upper(parts[3]) == "LW") {
      last_weekday = true;
      parts[3] = "";
    } else if (contains(to_upper(parts[3]), 'L')) {
      std::string stripped;
      for (char c : parts[3])
        if (c != 'L' && c != 'l') stripped += c;
      parts[3] = stripped;
      last_day_of_month = true;
    }

    if (parts[3] == "*") star_dom = true;
    if (parts[6] == "*") star_year = true;

    // Names of months and weekdays
    static const std::array<std::pair<const char*, const char*>, 12> month_names = {{
        {"jan", "1"},
        {"feb", "2"},
        {"mar", "3"},
        {"apr", "4"},
        {"may", "5"},
        {"jun", "6"},
        {"jul", "7"},
        {"aug", "8"},
        {"sep", "9"},
        {"oct", "10"},
        {"nov", "11"},
        {"dec", "12"},
    }};
    // "-sun" must come before "sun": Sunday as the upper value of a range is 7
    static const std::array<std::pair<const char*, const char*>, 8> day_names = {{
        {"-sun", "-7"},
        {"sun", "0"},
        {"mon", "1"},
        {"tue", "2"},
        {"wed", "3"},
        {"thu", "4"},
        {"fri", "5"},
        {"sat", "6"},
    }};
    static const std::array<std::pair<const char*, const char*>, 7> day_names_quartz = {{
        {"sun", "1"},
        {"mon", "2"},
        {"tue", "3"},
        {"wed", "4"},
        {"thu", "5"},
        {"fri", "6"},
        {"sat", "7"},
    }};
    if (parts[4].size() >= 3) parts[4] = replace_alpha(parts[4], month_names);
    if (parts[5].size() >= 3) {
      parts[5] = options_.alternative_weekdays ? replace_alpha(parts[5], day_names_quartz)
                                               : replace_alpha(parts[5], day_names);
    }

    // "+" in day-of-week: AND with day-of-month
    if (!parts[5].empty() && parts[5][0] == '+') {
      use_and_logic = true;
      parts[5] = parts[5].substr(1);
      if (parts[5].empty()) fail("the day of week field is empty after '+'");
    }

    if (parts[5] == "*") star_dow = true;

    switch (mode) {
      case Mode::FivePart:
        parts[0] = "0";
        parts[6] = "*";
        break;
      case Mode::SixPart:
      case Mode::FiveOrSixParts: parts[6] = "*"; break;
      default: break;
    }

    check_characters(parts);

    part_to_set(Part::Second, parts[0], 0);
    part_to_set(Part::Minute, parts[1], 0);
    part_to_set(Part::Hour, parts[2], 0);
    part_to_set(Part::Day, parts[3], -1);
    part_to_set(Part::Month, parts[4], -1);
    part_to_set(Part::DayOfWeek, parts[5], options_.alternative_weekdays ? -1 : 0);
    part_to_set(Part::Year, parts[6], 0);

    const auto is_fixed = [](const std::string& part, std::size_t count) {
      return !contains(part, '*') && !contains(part, '/') && count == 1;
    };
    is_fixed_time = is_fixed(parts[0], second.count()) && is_fixed(parts[1], minute.count()) &&
                    is_fixed(parts[2], hour.count());
  }

  void part_to_set(Part type, const std::string& conf, int offset) {
    if (conf.empty() && !(type == Part::Day && (last_day_of_month || last_weekday))) {
      fail(std::string("the ") + part_name(type) + " field is empty");
    }

    if (conf == "*") {
      for (int i = 0; i < length(type); ++i) set_bit(type, static_cast<std::size_t>(i));
      return;
    }

    const auto items = split(conf, ',');
    if (items.size() > 1) {
      for (const auto& item : items) part_to_set(type, item, offset);
    } else if (contains(conf, '-') && contains(conf, '/')) {
      handle_range_with_stepping(conf, type, offset);
    } else if (contains(conf, '-')) {
      handle_range(conf, type, offset);
    } else if (contains(conf, '/')) {
      handle_stepping(conf, type, offset);
    } else if (!conf.empty()) {
      handle_number(conf, type, offset);
    }
  }

  void check_characters(const std::vector<std::string>& parts) const {
    static const char* const names[] = {"second", "minute",      "hour", "day of month",
                                        "month",  "day of week", "year"};
    for (std::size_t i = 0; i < parts.size(); ++i) {
      for (char c : parts[i]) {
        bool ok = c == '/' || c == '*' || (c >= '0' && c <= '9') || c == ',' || c == '-';
        if (i == 3) ok = ok || c == 'W' || c == 'w' || c == 'L' || c == 'l';
        if (i == 5) ok = ok || c == '#' || c == 'L' || c == 'l';
        if (!ok) {
          fail(std::string("unexpected character '") + c + "' in the " + names[i] + " field");
        }
      }
    }
  }

  // The nth modifier of a weekday: nullopt for none, otherwise "L" or a number
  using Nth = std::optional<std::string>;

  void handle_number(const std::string& conf, Part type, int offset) {
    const auto [rest, nth] = extract_nth(conf, type);
    const bool nearest = contains(to_upper(conf), 'W');
    if (type != Part::Day && nearest) fail("W is only allowed in the day of month field");
    if (nearest) type = Part::NearestWeekdays;
    const auto v = parse_int(rest);
    if (!v)
      fail(std::string("'") + conf + "' in the " + part_name(type) + " field is not a number");
    set_part(type, *v + offset, nth);
  }

  void set_part(Part part, std::int64_t index, const Nth& nth) {
    if (part == Part::DayOfWeek) {
      if (index == 7) index = 0;
      if (index < 0 || index > 6) {
        fail("day of week " + std::to_string(index - (options_.alternative_weekdays ? -1 : 0)) +
             " is out of range");
      }
      set_nth_weekday_of_month(static_cast<std::size_t>(index), nth);
      return;
    }
    const std::int64_t lo = part == Part::Year ? 1 : 0;
    if (index < lo || index >= length(part)) {
      const int display_offset =
          part == Part::Day || part == Part::NearestWeekdays || part == Part::Month ? 1 : 0;
      fail(std::string(part_name(part)) + " " + std::to_string(index + display_offset) +
           " is out of range" + (part == Part::Year ? " (1-9999)" : ""));
    }
    set_bit(part, static_cast<std::size_t>(index));
  }

  void validate_range(std::int64_t lower, std::int64_t upper, std::optional<std::int64_t> steps,
                      Part type, const std::string& conf) const {
    if (lower > upper) fail("the range '" + conf + "' starts after it ends");
    if (steps) {
      if (*steps == 0) fail("a step of 0 in '" + conf + "'");
      if (*steps > length(type)) {
        fail("the step in '" + conf + "' is larger than the " + part_name(type) + " field (" +
             std::to_string(length(type)) + ")");
      }
    }
  }

  static bool is_digits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
      if (c < '0' || c > '9') return false;
    return true;
  }

  void handle_range_with_stepping(const std::string& conf, Part type, int offset) {
    if (contains(to_upper(conf), 'W')) fail("W is not allowed in a range with a step");
    const auto [rest, nth] = extract_nth(conf, type);

    // ^(\d+)-(\d+)\/(\d+)$
    const std::size_t dash = rest.find('-');
    const std::size_t slash = rest.find('/');
    if (dash == std::string::npos || slash == std::string::npos || slash < dash ||
        !is_digits(std::string_view(rest).substr(0, dash)) ||
        !is_digits(std::string_view(rest).substr(dash + 1, slash - dash - 1)) ||
        !is_digits(std::string_view(rest).substr(slash + 1))) {
      fail("'" + conf + "' is not a valid range with a step (from-to/step)");
    }
    const std::int64_t lower = *parse_int(rest.substr(0, dash)) + offset;
    const std::int64_t upper = *parse_int(rest.substr(dash + 1, slash - dash - 1)) + offset;
    const std::int64_t steps = *parse_int(rest.substr(slash + 1));

    validate_range(lower, upper, steps, type, conf);
    for (std::int64_t i = lower; i <= upper; i += steps) set_part(type, i, nth);
  }

  /// Split off the nth weekday (#n) or last occurrence (L), only allowed in day-of-week
  std::pair<std::string, Nth> extract_nth(const std::string& conf, Part type) const {
    if (contains(conf, '#')) {
      if (type != Part::DayOfWeek) fail("# is only allowed in the day of week field");
      const auto pieces = split(conf, '#');
      if (pieces.size() != 2 || pieces[1].empty()) {
        fail("'" + conf + "' is not a valid nth weekday (weekday#n)");
      }
      return {pieces[0], pieces[1]};
    }
    if (!conf.empty() && (conf.back() == 'L' || conf.back() == 'l')) {
      if (type != Part::DayOfWeek) {
        fail(
            "L after a value is only allowed in the day of week field (use L alone for the "
            "last day of the month)");
      }
      return {conf.substr(0, conf.size() - 1), std::string("L")};
    }
    return {conf, std::nullopt};
  }

  void handle_range(const std::string& conf, Part type, int offset) {
    if (contains(to_upper(conf), 'W')) fail("W is not allowed in a range");
    const auto [rest, nth] = extract_nth(conf, type);
    const auto pieces = split(rest, '-');
    if (pieces.size() != 2) fail("'" + conf + "' is not a valid range (from-to)");
    const auto lo = parse_int(pieces[0]);
    const auto hi = parse_int(pieces[1]);
    if (!lo || !hi) fail("'" + conf + "' is not a valid range (from-to)");
    const std::int64_t lower = *lo + offset, upper = *hi + offset;
    validate_range(lower, upper, std::nullopt, type, conf);
    for (std::int64_t i = lower; i <= upper; ++i) set_part(type, i, nth);
  }

  void handle_stepping(const std::string& conf, Part type, int offset) {
    if (contains(to_upper(conf), 'W')) fail("W is not allowed with a step");
    const auto [rest, nth] = extract_nth(conf, type);
    auto pieces = split(rest, '/');
    if (pieces.size() != 2) fail("'" + conf + "' is not a valid step (*/step)");

    if (!options_.sloppy_ranges) {
      if (pieces[0] != "*") {
        fail("'" + conf + "' steps from " + (pieces[0].empty() ? "nothing" : "a single value") +
             ", use */step or from-to/step (or enable sloppy_ranges)");
      }
    } else if (pieces[0].empty()) {
      pieces[0] = "*";
    }

    std::int64_t start = 0;
    if (pieces[0] != "*") {
      const auto s = parse_int(pieces[0]);
      start = s ? *s + offset : length(type);  // not a number: no values
    }

    const auto steps = parse_int(pieces[1]);
    if (!steps) fail("'" + conf + "' is not a valid step (*/step)");
    const int len = length(type);
    validate_range(0, len - 1, *steps, type, conf);

    for (std::int64_t i = start; i < len; i += *steps) {
      if (type == Part::Year && i == 0) continue;  // There is no year 0
      set_part(type, i, nth);
    }
  }

  std::string expand_nickname(const std::string& p) const {
    const std::string clean = to_lower(trim(p));
    if (clean == "@reboot") {
      fail("@reboot is not supported, there is no startup event to run on");
    }
    static const std::array<std::pair<const char*, const char*>, 7> nicknames = {{
        {"@yearly", "0 0 1 1 *"},
        {"@annually", "0 0 1 1 *"},
        {"@monthly", "0 0 1 * *"},
        {"@weekly", "0 0 * * 0"},
        {"@daily", "0 0 * * *"},
        {"@midnight", "0 0 * * *"},
        {"@hourly", "0 * * * *"},
    }};
    // Sunday is 1 with Quartz weekday numbering
    if (clean == "@weekly" && options_.alternative_weekdays) return "0 0 * * 1";
    for (const auto& [name, value] : nicknames)
      if (clean == name) return value;
    return p;
  }

  void set_nth_weekday_of_month(std::size_t index, const Nth& nth) {
    auto& v = day_of_week[index];
    if (!nth) {
      v = ANY_OCCURRENCE;
      return;
    }
    if (to_upper(*nth) == "L") {
      v = static_cast<std::uint8_t>(v | LAST_OCCURRENCE);
      return;
    }
    // A number 1-5
    std::int64_t n = 0;
    const bool numeric = is_digits(*nth);
    if (numeric) {
      for (char c : *nth)
        if (n < 1000) n = n * 10 + (c - '0');
    }
    if (numeric && n > 0 && n < 6) {
      v = static_cast<std::uint8_t>(v | OCCURRENCE_BITMASKS[n - 1]);
      return;
    }
    fail("nth weekday '" + *nth + "' is out of range, expected 1-5 or L");
  }
};

}  // namespace detail
}  // namespace croner

#endif  // CRONER_PATTERN_HPP
