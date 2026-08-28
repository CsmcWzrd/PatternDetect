# PatternDetect

PatternDetect is a cross-platform command-line application written in C++17 for Linux, macOS, and Windows 10+. It scans one or more log files using a small pattern-detection DSL, captures values from matching log messages, validates checks, follows `on-hit` chains, emits reports/debug output, and evaluates recursive math/string expressions without persistent expression storage.

Example expression:

```sh
patterndetect --expr "add(multiply(2,3),4)"
# 10
```

## Features

- C++17, single executable, CMake build.
- Linux, macOS, Windows 10+ support.
- Pattern DSL with:
  - `use` directives.
  - chronological or reverse-chronological scan order.
  - date range filtering.
  - reboot message detection and stop-on-reboot.
  - rotated log file discovery.
  - optional `.gz` log reading when zlib is available.
  - terminal and TCP report destinations; TLS destinations are recognized and can be enabled with OpenSSL builds.
  - named patterns such as `A: [ ... ]`.
  - pattern title, log file, component, separator, fragments, captures, checks, report, debug, and `on-hit` chaining.
  - `on-hit` graph validation to reject cycles/backtracking before scan execution.
- Recursive expression engine:
  - math: `add`, `subtract`, `multiply`, `divide`, `mod`, `pow`, `sqrt`, `abs`, `min`, `max`, `clamp`, `round`, `floor`, `ceil`.
  - trigonometry: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `deg2rad`, `rad2deg`.
  - numeric: `log`, `log10`, `exp`.
  - strings: `concat`, `upper`, `lower`, `trim`, `length`, `contains`, `startsWith`, `endsWith`, `substr`, `replace`, `split`, `join`.
  - logic/comparison: `eq`, `ne`, `lt`, `lte`, `gt`, `gte`, `and`, `or`, `not`, `if`.

## Build

### Linux / macOS

```sh
./build.sh
```

Manual form:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The binary will be at:

```text
build/patterndetect
```

### Windows 10+ / Visual Studio 2022

From a Developer Command Prompt:

```bat
build.bat
```

Manual form:

```bat
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

The binary will be at:

```text
build\Release\patterndetect.exe
```

### Optional features

If zlib is installed, `.gz` files are enabled automatically by default:

```sh
cmake -S . -B build -DPATTERNDETECT_ENABLE_ZLIB=ON
```

TLS report destinations can be enabled with OpenSSL:

```sh
cmake -S . -B build -DPATTERNDETECT_ENABLE_OPENSSL=ON
```

## CLI

```text
patterndetect --config file.pdconf --log app.log [--log old.log] [options]
patterndetect --expr 'add(multiply(2,3),4)'
```

Options:

```text
--config <file>       PatternDetect configuration/DSL file
--log <file>          Log file to scan; can be repeated
--expr <expr>         Evaluate recursive expression and exit
--validate-only       Parse and validate config/on-hit graph only
--dump-config         Print parsed pattern summary to stderr
--version             Print version
-h, --help            Show help
```

## Quick demo

```sh
./build/patterndetect --expr 'add(multiply(2,3),4)'
./build/patterndetect --config examples/patterndetect.conf --log examples/sample.log --dump-config
```

Expected report output from the sample:

```text
PATTERNDEBUG: Boot source timestamp:1a=... value:1a=from value:1b@=["fedora", "core", "linux"]
REPORT: Initial problem1 found
REPORT: Problem1 issue marker 2 observed
```

## DSL summary

A configuration can contain global directives and named patterns:

```text
{
    use chronological-order
    use report-destination terminal

    A: [
        title "Scan for boot start"
        log message Linux is booting, boot time is separator " "
        grab timestamp:1a, value:1a, value:1b@
        report "Initial problem1 found"
        debug "Boot source" timestamp:1a value:1a value:1b@
        on-hit: B
    ]

    B: [
        within timestamp:1a + 60
        log message service warning separator " "
        grab value:2a@
        check value:2a@1 "date" equals
        report "Problem1 issue marker 2 observed"
    ]
}
```

Capture behavior:

- `timestamp:1a` captures a timestamp and stores UTC epoch microseconds.
- `timestamp:1a:text` stores the original timestamp text.
- `value:1a` captures one token.
- `value:1b@` captures the remaining tokens as an array.
- `value:1b@0`, `value:1b@1`, etc. access array elements.
- `value:@` captures the full matched line split with the separator.
- `value:@0`, `value:@1`, etc. access full-line tokens.

Checks:

```text
check value:1a "expected" equals
check value:1n 100 greater-than
check-or
check value:1b@2 "invalid date" equals
```

Supported comparison operators:

```text
equals
not-equals
less-than
greater-than
less-than-equal-to
greater-than-equal-to
```

Supported check connectors:

```text
check-and
check-or
check-not-and
check-not-or
check-and-not
check-or-not
```

## Notes

- The parser accepts `#` and `//` comments outside quoted strings.
- Placeholder specification lines such as `use date-after/date-before <date>` are treated as inactive spec text and reported as warnings if present in a runnable config.
- For production deployments, prefer concrete directives such as `use date-after 2026-05-01 00:00:00 UTC`.
- The included `docs/original-pattern-matching-log-detection.log` is the original uploaded specification used to design this package.

## Large generated validation suite

This package includes a deterministic generated validation suite under `tests/large_suite`.

Coverage included in the generated suite:

- 1000 PatternDetect configuration test cases.
- 100 reusable fake log sets.
- 100 fake component names: `Component000` through `Component099`.
- Fake log sizes ranging from 300 to 10000 total lines per log set.
- Rotation counts from 0 to 10 using the same filename prefix, for example `app.log`, `app.log.1`, ... `app.log.10`.
- Pattern counts from 5 to 10 per test case.
- Pattern-recognition sections containing 1 to 10 `log message` statements.
- `on-hit` fanout from 0 to 9 and chain depth from 1 to 10.
- Reused logs across test cases, with unique markers and expected report strings for each case.

Regenerate the suite:

```sh
python3 tests/generate_large_suite.py --output tests/large_suite --cases 1000 --logsets 100 --force
```

Run the full suite after building:

```sh
python3 tests/run_large_suite.py --binary ./build/patterndetect --suite tests/large_suite --jobs 8
```

The latest sandbox validation report is stored at:

```text
tests/LARGE_SUITE_VALIDATION.md
```
