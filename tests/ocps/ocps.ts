// Runs the OCPS conformance test vectors (data/conformance-tests.json in
// https://github.com/open-source-cron/ocps) against Croner for C++.
//
// The vectors are licensed CC BY-SA 4.0 and are not copied into this repository; pass the path
// of an OCPS checkout:
//   npx tsx tests/ocps/ocps.ts build/croner_ocps_driver path/to/ocps
import { spawnSync } from "node:child_process";
import { readFileSync } from "node:fs";
import * as path from "node:path";

const [driver, ocpsDir] = process.argv.slice(2);
if (!driver || !ocpsDir) {
  console.error("usage: ocps.ts <driver> <ocps checkout>");
  process.exit(2);
}

// Deliberate deviations, shared with croner.js and croner-rust
const deviations: Record<string, string> = {
  "ocps-1.1-nicknames-are-case-sensitive":
    "Nicknames are case-insensitive (@Daily is accepted), as in croner.js and croner-rust.",
  "ocps-1.1-reboot-parse-and-runtime-handling":
    "@reboot is rejected when the pattern is parsed, with a clear error, as in croner.js and croner-rust.",
  "ocps-1.4-question-mark-invalid-outside-day-fields":
    "? is accepted as an alias for * in every field, as in croner.js and croner-rust.",
};

type Test = {
  id: string;
  type: string;
  pattern: string;
  expectations: {
    mustMatch?: string[];
    mustNotMatch?: string[];
    parse?: { result: "accept" | "reject" };
  };
};

const data = JSON.parse(readFileSync(path.join(ocpsDir, "data", "conformance-tests.json"), "utf8"));
const tests: Test[] = Object.values(data.versions).flatMap((v) => (v as { tests: Test[] }).tests);

// Build the checks, run them in one go
type Check = { test: Test; line: string; want: string };
const checks: Check[] = [];
for (const test of tests) {
  const e = test.expectations;
  if (e.parse) checks.push({ test, line: `parse\t${test.pattern}`, want: e.parse.result });
  for (const d of e.mustMatch ?? []) checks.push({ test, line: `match\t${test.pattern}\t${d}`, want: "match" });
  for (const d of e.mustNotMatch ?? []) {
    checks.push({ test, line: `match\t${test.pattern}\t${d}`, want: "nomatch" });
  }
}
const res = spawnSync(driver, [], { input: checks.map((c) => c.line).join("\n") + "\n" });
if (res.status !== 0) {
  console.error("driver failed", res.stderr?.toString());
  process.exit(2);
}
const got = res.stdout.toString().trimEnd().split("\n");

const failedTests = new Map<string, string[]>();
checks.forEach((c, i) => {
  if (got[i] !== c.want) {
    const list = failedTests.get(c.test.id) ?? [];
    list.push(`${c.line.replaceAll("\t", " | ")}: got ${got[i]}, want ${c.want}`);
    failedTests.set(c.test.id, list);
  }
});

let failures = 0, deviating = 0, passed = 0, skipped = 0;
for (const test of tests) {
  const hasChecks = checks.some((c) => c.test === test);
  if (!hasChecks) {
    skipped++;
    console.log(`SKIP ${test.id} (${test.type}, not machine-checkable)`);
    continue;
  }
  const failed = failedTests.get(test.id);
  if (deviations[test.id]) {
    if (failed) {
      deviating++;
      console.log(`DEVIATION ${test.id}: ${deviations[test.id]}`);
    } else {
      failures++;
      console.log(`FAIL ${test.id}: listed as a deviation, but passes. Remove it from the list.`);
    }
  } else if (failed) {
    failures++;
    console.log(`FAIL ${test.id}\n  ${failed.join("\n  ")}`);
  } else {
    passed++;
  }
}
console.log(
  `${tests.length} OCPS tests: ${passed} passed, ${deviating} documented deviations, ` +
    `${skipped} skipped, ${failures} failed`,
);
process.exit(failures ? 1 : 0);
