// Reads OCPS conformance checks from stdin, one per line, tab separated:
//   parse <tab> pattern                  -> "accept" or "reject"
//   match <tab> pattern <tab> datetime   -> "match", "nomatch" or "reject"
// Datetimes are "YYYY-MM-DD HH:mm:ss", evaluated in UTC. Used by ocps.ts.
#include <croner/croner.hpp>

#include <iostream>
#include <string>

int main() {
  std::string line;
  while (std::getline(std::cin, line)) {
    const auto t1 = line.find('\t');
    const std::string kind = line.substr(0, t1);
    const auto t2 = line.find('\t', t1 + 1);
    const std::string pattern =
        line.substr(t1 + 1, t2 == std::string::npos ? std::string::npos : t2 - t1 - 1);
    try {
      croner::CronOptions o;
      o.zone = croner::utc_zone();
      const croner::Cron job(pattern, o);
      if (kind == "parse") {
        std::cout << "accept\n";
      } else {
        std::string when = line.substr(t2 + 1);
        if (when.size() > 10 && when[10] == ' ') when[10] = 'T';
        std::cout << (job.match(when) ? "match" : "nomatch") << "\n";
      }
    } catch (const croner::CronError&) {
      std::cout << "reject\n";
    }
  }
}
