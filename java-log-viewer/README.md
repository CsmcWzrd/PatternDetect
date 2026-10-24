# PatternDetect Live Operation Log Viewer

A **standalone Java Swing** application using JDK/OpenJDK **11 or later**. It
follows PatternDetect's `.script.log` stream and renders **the corresponding
human-readable five-field records** in a hierarchical tabbed browser. It does
not read or interfere with the log-detection engine. It is **not built** by the
parent PatternDetect CMake build.

## Build (Linux, macOS, Windows)

**Apache Ant** (recommended for NetBeans):

```sh
cd java-log-viewer
ant clean jar test
java -jar dist/PatternDetectLogViewer.jar --log /path/to/patterndetect.operations.12345.script.log
```

**GNU Make** on Unix, macOS or MSYS2/MinGW on Windows:

```sh
make all
make test
java -jar dist/PatternDetectLogViewer.jar --log /path/to/file.script.log
```

**Native Windows Command Prompt** (JDK 11+ on PATH):

```bat
cd java-log-viewer
build.bat
java -jar dist\PatternDetectLogViewer.jar --log "C:\path\to\file.script.log"
```

The portable jar uses only the standard Java library (no Maven/Gradle
plugins, external dependencies, JavaFX, or native OS libraries). It runs on
Oracle JDK and OpenJDK. GUI requires a desktop graphics environment.

## Apache NetBeans 16 (2022)

Select **File > Open Project** and choose `java-log-viewer/`. This is an Ant
**freeform NetBeans project**: `nbproject/project.xml`, `build.xml`, the source
packages and tests are included. Use the IDE's **Build**, **Run**, and **Test**
actions. For **Run**, set Ant property `run.args=--log /path/to/file.script.log`
(or open the log from the application's **File > Open operation log...** menu).

NetBeans 16, released November 30, 2022, was the final NetBeans release of
calendar year 2022. A Java 11 toolchain is sufficient; JDK/OpenJDK 17 or newer
also works. It was not the last release of NetBeans overall.

## Using the live viewer

1. Run PatternDetect normally (generates human and script logs by default).
   Supply `--operation-log-prefix /shared/logs/session` to set a stable filename.
2. Start the viewer with `--log /shared/logs/session.script.log`. It can start
   **before** the script file exists and will follow appended records.
3. The main tab lists events belonging to the root application level.
4. Click an event to open its immediate children in a new tab. Click a child
   to drill down recursively. Opened tabs continue to receive new events.
5. Selecting an event shows its full five-field human-readable representation
   in the lower pane. Use the File menu to open another log or pause following.

The viewer reads the **script** file for lossless parent/child linkage and
renders the **human** view. Therefore use default dual-output mode or
`--script-readable-only` when using the viewer; a human-only file does not
retain the canonical single-line event framing needed for this live parser.

## Technical behavior

- Nonblocking I/O on a scheduled daemon worker, 250 ms poll interval.
- EDT-safe Swing updates, live sorting/scrolling and UTF-8 handling.
- Delimiter-aware parsing and decoding of `\\xHH`, escaped backslashes and
  control characters. Binary content remains safely displayed as Base64.
- Stable monotonically increasing event IDs; parent IDs form the drilldown
  hierarchy. Each file represents one PatternDetect process invocation.
- Handles file appearance after startup and truncation; partial trailing
  lines are buffered until newline. Overlong malformed records are rejected.
- The viewer stores received records in memory, so very long sessions can
  require significant RAM. For archive-scale investigations, split sessions
  into process-specific operation log files.

## Validation

```sh
ant test
# On Linux with xvfb-run installed, optional live Swing GUI integration:
javac --release 11 -cp build/classes -d build/testclasses tests/ViewerGuiSmokeTest.java
xvfb-run -a java -ea -cp build/classes:build/testclasses ViewerGuiSmokeTest
```

`tests/ViewerParserTest.java` also accepts a generated PatternDetect `.script.log`
file as an argument to verify real native C++/Java interoperability.
