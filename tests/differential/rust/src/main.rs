// Same input format as croner-cpp tests/differential/driver.cpp; prints runs found by croner-rust.
use chrono::{DateTime, NaiveDateTime, TimeZone, Utc};
use chrono_tz::Tz;
use croner::parser::CronParser;
use std::io::{self, BufRead, Write};

fn parse_ref(s: &str, tz: Tz) -> Option<DateTime<Tz>> {
    if s.ends_with('Z') {
        let d = DateTime::parse_from_rfc3339(s).ok()?;
        return Some(d.with_timezone(&tz));
    }
    let n = NaiveDateTime::parse_from_str(s, "%Y-%m-%dT%H:%M:%S").ok()?;
    match tz.from_local_datetime(&n) {
        chrono::LocalResult::Single(d) => Some(d),
        chrono::LocalResult::Ambiguous(a, _) => Some(a),
        chrono::LocalResult::None => None,
    }
}

fn main() {
    let stdin = io::stdin();
    let mut out = io::BufWriter::new(io::stdout());
    for line in stdin.lock().lines() {
        let line = line.unwrap();
        let f: Vec<&str> = line.split('\t').collect();
        let res = (|| -> Option<String> {
            let tz: Tz = if f[1].is_empty() { "Etc/UTC" } else { f[1] }.parse().ok()?;
            let parser = CronParser::builder()
                .dom_and_dow(f[3] == "1")
                .alternative_weekdays(f[4] == "1")
                .sloppy_ranges(f[5] == "1")
                .build();
            let cron = match parser.parse(f[0]) {
                Ok(c) => c,
                Err(_) => return Some("ERR".into()),
            };
            let mut t = parse_ref(f[11], tz)?;
            let n: usize = f[13].parse().ok()?;
            let mut runs = vec![];
            for _ in 0..n {
                let r = if f[12] == "p" {
                    cron.find_previous_occurrence(&t, false)
                } else {
                    cron.find_next_occurrence(&t, false)
                };
                match r {
                    Ok(d) => {
                        runs.push(d.with_timezone(&Utc).format("%Y-%m-%dT%H:%M:%S%.3fZ").to_string());
                        t = d;
                    }
                    Err(_) => break,
                }
            }
            Some(if runs.is_empty() { "-".into() } else { runs.join(",") })
        })();
        writeln!(out, "{}", res.unwrap_or_else(|| "SKIP".into())).unwrap();
    }
}
