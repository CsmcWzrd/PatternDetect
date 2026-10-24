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
  - terminal and TCP report destinations; TLS is not yet a secure working transport (see note at end).
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

OpenSSL build linkage can be enabled, but **TLS report transport is not implemented securely**:

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

## Operation logs (v1.2)

PatternDetect writes **two** operation logs by default, both flushed as events
occur. Each invocation has a unique per-process output prefix:

```text
patterndetect.operations.<pid>.human.log
patterndetect.operations.<pid>.script.log
```

If the DSL includes `use processing-folder <path>`, the default log files are
placed in that folder (created automatically). The output prefix may be
overridden explicitly:

```sh
./build/patterndetect --config examples/patterndetect.conf --log examples/sample.log \
  --operation-log-prefix /tmp/myscan
# /tmp/myscan.human.log and /tmp/myscan.script.log

./build/patterndetect --config examples/patterndetect.conf --log examples/sample.log \
  --operation-log-prefix /tmp/humanscan --human-readable-only

./build/patterndetect --config examples/patterndetect.conf --log examples/sample.log \
  --operation-log-prefix /tmp/scriptscan --script-readable-only
```

Other options:

- `--filter-host <hostname+IP>`: optionally override the recorded identity of
  the host on which the input logs were filtered; defaults to the running host.
- `--script-readable--only`: also accepted as an alias for
  `--script-readable-only`, for compatibility with the requested spelling.
- The two `--*-only` options are mutually exclusive.

The existing `REPORT:`, `PATTERNDEBUG:` and `PATTERNWARN:` terminal/network
output remains unchanged. Operation logging is a separate audit stream.

### Human-readable format

Every record contains five **logical fields**; long fields can occupy more
than one physical line. Each physical line starts with four spaces, and wraps
at 128 display columns (including indent). Two empty lines follow each record.

1. A zero-padded **16-character depth**, host+IP of the running process,
   ISO-8601 UTC timestamp with six microsecond digits, host+IP of the filtering
   machine, and its UTC timestamp. Header items use `  )))))  `.
2. Severity (`INFO`, `DEBUG`, `REPORT`, `WARN`, `ERROR`).
3. `PatternDetect:<process-id>:<thread-id>  )))))  event=<id>;parent=<id>`.
   The latter metadata allows reliable cross-process/viewer correlation.
4. Summary trail from the top-level operation to the current event, separated
   using `  )))))  `.
5. The PatternDetect application's actual event message (report, debug output,
   matched pattern, scanned file count, validation result, and so on).

### Machine-readable format

The same five fields are emitted on **one line**, separated by exactly
`  -----  `. Fields 1, 3 and 4 retain their internal `  )))))  ` list delimiter.
Within any payload text, either reserved separator is escaped **character by
character** to `\\xHH` (including both surrounding spaces); newlines become
`\\n`, tabs `\\t`, carriage returns `\\r`, and backslashes `\\\\`.
Nontext/binary payloads are printed as `base64:<RFC4648-encoded-bytes>`.

Both logs are emitted with the same event IDs and hierarchy. A single root
operation has `parent=0`; matches, reports, and chained patterns are children
of the operation that produced them.

See [docs/OPERATION_LOG_FORMAT.md](docs/OPERATION_LOG_FORMAT.md) for an example,
parser contract, and details.

## Optional live Java Swing viewer

`java-log-viewer/` is a standalone Ant/Make/NetBeans (2022 NetBeans 16) Java
Swing project, **not built by CMake**. Build with JDK/OpenJDK 11+:

```sh
cd java-log-viewer
ant jar test
java -jar dist/PatternDetectLogViewer.jar --log /tmp/myscan.script.log
```

Open a root-level operation, then click each child event to open the next level
in a new, live-updating tab. See
[java-log-viewer/README.md](java-log-viewer/README.md).

## Test status (October 24, 2026)

- Native C++ build: GCC/CMake on Linux: **pass**.
- Native operation logger CTest: **pass**.
- Existing smoke test: **pass**.
- Deterministic 1,000-case generated log suite: **1,000 passed**, 0 failed.
  Covers 100 log sets, 100 component names, 300–10,000 log lines, 0–10
  rotations, 5–10 patterns, 1–10 statements per pattern, and 1–10 chain depth.
  Each case also validates the enabled operation log format(s).
- Java Swing Ant/Make build and parser tests: **pass** using OpenJDK 21
  targeting Java 11 class files.
- Live GUI smoke with an Xvfb display: **pass**, including 2 levels of
  realtime drilldown and updates to already opened tabs.

Linux build/tests are verified. macOS, Windows/Visual Studio, and NetBeans IDE
builds are supplied but **have not been executed** in this environment.

**Note on existing transport behavior:** The earlier code's TLS report
transport is not a completed authenticated TLS implementation. The
`PATTERNDETECT_ENABLE_OPENSSL` option alone does not make that path secure;
use terminal or plain TCP only in trusted networks until the transport is
implemented and security-tested. Operation logs are local files.
