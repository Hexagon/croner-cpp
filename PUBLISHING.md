# Where to publish Croner for C++

## Summary

1. Done: the port has its own repository, `hexagon/croner-cpp`, like `hexagon/croner-rust`. It was
   split out of `cpp/` in hexagon/croner with its history. The differential test compares with
   the `croner` version pinned in `tests/differential/package.json`.
2. Tag `v0.1.0` and publish a GitHub release. Users can then consume it right away with CMake
   `FetchContent`, CPM.cmake, or by copying the headers.
3. Submit a **vcpkg** port and a **Conan Center** recipe, both under the package name `croner`
   (not `croner-cpp`), which is free in both (checked 2026-11-07). The CMake package and target
   are `croner` / `croner::croner`, and the Conan recipe is already named `croner`. Start with
   vcpkg, which reviews faster.
4. Add a "C++" page to croner.56k.guru, and link it from the READMEs of the JavaScript and
   Rust versions.
5. Later, if users ask for them: Meson WrapDB, xmake-repo, build2 (cppget.org), and Linux
   distribution packages.

## Why a separate repository

- vcpkg and Conan expect a release tarball with the library at the root.
- Independent semver (`0.x` until the API settles), separate from croner.js `11.x`.
- Separate issues and CI, consistent with croner-rust.
- When croner.js changes, bump the `croner` version in `tests/differential/package.json`; the
  differential test then shows whether the port needs the same change.

## Distribution channels

| Channel | Effort | Reach | Notes |
| --- | --- | --- | --- |
| GitHub releases + FetchContent / CPM.cmake | None beyond tagging | High among CMake users | Works the day the repository exists. The CMake package (`find_package(croner)`, target `croner::croner`) is already set up. |
| **vcpkg** (microsoft/vcpkg) | Low: `ports/croner/portfile.cmake` + `vcpkg.json`, about 20 lines for a header-only library using the existing install rules | High, especially on Windows and in CMake projects | Needs a tagged release tarball. Accepts 0.x versions. Review usually takes days. |
| **Conan Center** (conan-io/conan-center-index) | Medium: `recipes/croner/all/conanfile.py` + `test_package`, plus compiler and C++17 checks per CCI conventions | High in enterprise and embedded | `conanfile.py` works locally (`conan create`) and is a starting point. Its stricter review and hooks can take weeks. |
| Meson WrapDB | Low | Small | Only if Meson users ask for it |
| xmake-repo, build2/cppget.org, Spack | Low each | Small | Only on demand |
| Single-header amalgamation | Low (concatenate the five headers in a release workflow) | Copy-paste users, embedded | Optional release asset |
| Debian, Fedora, Homebrew | Done by distribution packagers | | Header-only libraries are rarely packaged before they are in vcpkg or Conan |

Existing C++ cron libraries include `croncpp`, which is in both vcpkg and Conan Center. Croner's
differentiators are OCPS compliance (`L`, `W`, `#`, year field, `+`), DST handling defined and
shared with croner-rust and croner.js, IANA time zones with no dependencies, and a built-in
scheduler with overrun protection.

## Checklist before 1.0 and the package manager submissions

- [x] CI green on Linux (GCC, Clang), macOS and Windows (MSVC C++20, `std::chrono::tzdb`).
- [x] OCPS conformance test vectors, and differential tests against croner.js and croner-rust.
- [ ] Fix the bugs found in croner.js (day rollover, `@weekly` with alternative weekdays, years
      1-99) and croner-rust (`*/2` year parity, stopping early over Samoa's skipped day), so the
      implementations agree again.
- [ ] Decide on API names that are hard to change later: `croner::Cron`, `croner::Options`,
      snake_case methods, and `croner::TimePoint` with millisecond precision.
- [ ] Decide whether job functions keep running on a thread per run, or whether to offer a
      pluggable executor, for example to run on an existing event loop or thread pool.
- [ ] Generated API docs (Doxygen), or a docs page on croner.56k.guru.
- [ ] `CHANGELOG.md`, and a statement of which croner.js version and OCPS version the port
      tracks (currently croner.js 11.0.0, OCPS 1.4 with the documented DST deviation).
