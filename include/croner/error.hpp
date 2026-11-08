// Croner for C++ - MIT License - Hexagon <github.com/Hexagon>

#ifndef CRONER_ERROR_HPP
#define CRONER_ERROR_HPP

#include <stdexcept>
#include <string>

namespace croner {

/// Thrown for invalid patterns, dates, time zones and options.
class CronError : public std::runtime_error {
 public:
  /// What was invalid
  enum class Kind {
    Pattern,   ///< The cron pattern
    Date,      ///< An ISO 8601 date string
    TimeZone,  ///< A time zone name, zoneinfo file or offset
    Option,    ///< A value in CronOptions or JobOptions, or an invalid call
  };

  CronError(Kind kind, const std::string& message) : std::runtime_error(message), kind_(kind) {}

  [[nodiscard]] Kind kind() const noexcept { return kind_; }

 private:
  Kind kind_;
};

}  // namespace croner

#endif  // CRONER_ERROR_HPP
