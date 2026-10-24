# PatternDetect v1.2 build and test verification — 2026-10-24

Environment: Linux x86-64; CMake, GCC and Clang toolchains; OpenJDK 21;
Apache Ant; Xvfb virtual display. Tests were actually executed here.
The cross-platform project files are included; no physical Windows 10+
or macOS build/test was run in this environment.

## C++ native build + native operation log unit test

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DPATTERNDETECT_BUILD_TESTS=ON
cmake --build build -j4
cd build && ctest --output-on-failure
1/1 operation_log_test: Passed
```

Unit test verifies escaped 9-byte human and script separators, output
formatting and 128-column width, Base64 binary payload, depth and parent
metadata, and human-only/script-only file creation.

## Existing matcher regression tests

```text
./tests/run_smoke_tests.sh ./build/patterndetect
PatternDetect smoke tests passed

python3 tests/run_large_suite.py --binary ./build/patterndetect \
  --suite tests/large_suite --jobs 8 --timeout 30 \
  --report tests/LARGE_SUITE_VALIDATION_2026_10_24.md
validated 100/1000 cases
...
validated 1000/1000 cases
PatternDetect large suite: 1000 cases, failures=0, elapsed=3.289s
```

The regression suite still validates all expected reports from the generated
fake log files, *and now additionally checks* the event/log formats for every
case, rotating the CLI modes among default (both), human-only, script-only.

Manifest coverage:

- 1,000 configurations, 100 reusable fake log sets and 100 component names
- 300–10,000 log lines per set, rotation count 0–10
- 5–10 patterns per configuration, 1–10 log-message statements per pattern
- 1–10 levels in valid on-hit chains, fanout up to 9

## Java viewer

```text
cd java-log-viewer
ant clean jar test
BUILD SUCCESSFUL
Viewer parser tests passed

make test
Viewer parser tests passed

# GUI integration with real Swing components:
xvfb-run -a java -ea -cp build/classes:build/testclasses ViewerGuiSmokeTest
Live Swing GUI smoke passed: two nested tabs received real-time updates
```

The native matcher was also run against `examples/patterndetect.conf` and its
actual `.script.log` was passed to the Java parser interoperability test:

```text
Verified 8 native operation log events
Viewer parser tests passed
```

The GUI smoke test starts a real Swing window, appends records while running,
selects a root row, opens a child tab, appends a child, opens a grandchild tab,
appends its child, then verifies both opened tabs update.

## Second compiler and integration checks

The project and native tests also built and ran using Clang++ on Linux.
The application passed its original smoke tests under that compiler.
An additional integration check verified that `use processing-folder` places
both operation logs in the configured folder (including a path with spaces),
mutually exclusive output modes are rejected, and the alternate spelling
`--script-readable--only` is accepted.

XML parsing was successful for the NetBeans metadata and Ant build file.

## Remaining platform verification

- Windows 10+/11 (Visual Studio 2022, native JDK/OpenJDK): not executed here.
- macOS Intel/Apple Silicon builds, Java JDK/OpenJDK: not executed here.
- Apache NetBeans 16 IDE actions: project metadata provided, not opened here.
- Run targets on the actual OSes for release qualification.

## Java 8 compatibility repair — 2026-10-24

The earlier Java viewer release compiled `LiveLogViewer` for class-file major
version **55** (Java 11); Java 8 JRE accepts only up to **52**. The Java 8
release fixes the Ant, GNU Make, Windows `build.bat`, and NetBeans project
source level to **1.8**, uses Java 8-compatible `jar cfe` packaging syntax,
and replaces Java 11-only `String.repeat`/`String.lines` calls in tests.
All production Java sources and tests now compile at Java 8 source/target.

Verified under Linux using OpenJDK **21** as the compiler and test runtime:

```text
$ ant clean jar test
Viewer parser tests passed
Java 8 JAR compatibility check passed: 5 classes, all major version <= 52
BUILD SUCCESSFUL

$ make clean && make test
Viewer parser tests passed
Java 8 JAR compatibility check passed: 5 classes, all major version <= 52

$ javap -verbose -classpath dist/PatternDetectLogViewer.jar patterndetect.viewer.LiveLogViewer
major version: 52

$ xvfb-run -a java -ea -cp build/classes:build/testclasses ViewerGuiSmokeTest
Live Swing GUI smoke passed: two nested tabs received real-time updates

$ java -cp build/classes:build/testclasses ViewerParserTest <native-script-log>
Verified 8 native operation log events
Viewer parser tests passed

$ ctest --output-on-failure
100% tests passed, 0 tests failed out of 1

$ ./tests/run_smoke_tests.sh ./build/patterndetect
PatternDetect smoke tests passed

$ python3 tests/run_large_suite.py --binary ./build/patterndetect --jobs 8 --report tests/LARGE_SUITE_VALIDATION_JAVA8_UPDATE.md
PatternDetect large suite: 1000 cases, failures=0, elapsed=2.874s
```

A no-argument launch using `xvfb-run` opened the new GUI process without
stderr errors; it was intentionally terminated by the test timeout.

**Environment limitation:** Java 8 JRE on Windows 11 was not available in this
Linux build environment. The bytecode version is validated exhaustively, and
the source uses Java 8 APIs, but a physical Windows 11 / Java 8 runtime test
remains to be performed by the user or a Windows CI runner.
