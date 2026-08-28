# PatternDetect DSL Specification

This document describes the PatternDetect configuration implemented in this package.

## File structure

```text
{
    use chronological-order
    use report-destination terminal

    PatternName: [
        title "Human readable title"
        log message fragment separator " "
        grab timestamp:1a, value:1b, value:1c@
        check value:1b "expected" equals
        report "message"
        debug "message" value:1b
        on-hit: NextPattern
    ]
}
```

Pattern names must start with a letter and may contain letters and digits. Underscore is intentionally not required by the DSL style.

## Global directives

| Directive | Meaning |
|---|---|
| `use chronological-order` | Scan loaded log lines oldest-to-newest. |
| `use reverse-chronological-order` | Reverse the loaded log lines before scanning. |
| `use date-after <timestamp>` | Skip parsed log lines before this timestamp. |
| `use date-before <timestamp>` | Skip parsed log lines after this timestamp. |
| `use reboot-timestamp-from-message "text"` | Identify reboot lines by message text. |
| `use stop-on-reboot <count>` | Stop after count reboot messages. Defaults to 1 when present without count. |
| `use log-rotated` | Also try `file.1` through `file.10`, `.gz`, and numbered `.gz` variants. |
| `use auto-uncompress` | `.gz` reading is automatic when built with zlib. |
| `use processing-folder <path>` | Reserved path for generated processing files. |
| `use detail-pattern-matched-log <file>` | Append emitted report/debug/warn messages to a detail log. |
| `use report-destination terminal` | Emit to terminal. |
| `use report-destination host:tcp:port` | Send reports/debug over TCP. IPv6 form: `[::1]:tcp:9000`. |
| `use report-destination host:tcp:ssl:port` | TLS destination. Requires OpenSSL build option. |

## Pattern statements

| Statement | Meaning |
|---|---|
| `title "..."` | Human-readable pattern title. |
| `within timestamp:1a + 60` | Match this pattern only within 60 seconds after a previous timestamp variable. |
| `log <file>` | Pattern-specific log file path. CLI `--log` can also provide files. |
| `component <name>` | Metadata for the log source/component. |
| `log message <fragment> separator " "` | Add a sequential log fragment match step and set separator. |
| `grab ...` | Capture values from the text after the fragment. |
| `check ...` | Validate captured values. |
| `report "..." [vars...]` | Emit a `REPORT: ` message. |
| `debug "..." [vars...]` | Emit a `PATTERNDEBUG: ` message. |
| `on-hit: PatternName` | Run another pattern after this pattern matches. |

## Timestamps

Captured timestamp values are normalized to UTC epoch microseconds. The implementation recognizes:

- Unix epoch seconds, milliseconds, or microseconds.
- `YYYY-MM-DD HH:MM:SS`.
- `YYYY-MM-DDTHH:MM:SS`.
- Optional fractional seconds.
- `Z`, `UTC`, `GMT`, `IST`, and numeric `+HHMM` or `+HH:MM` offsets.
- Syslog-style `Mon DD HH:MM:SS` using the current UTC year.

## Recursive expression examples

```text
add(multiply(2,3),4)                 -> 10
sin(deg2rad(90))                     -> 1
upper(concat("kernel", " panic"))   -> KERNEL PANIC
contains(lower("Invalid Date"), "date") -> true
if(gt(10,5), "yes", "no")          -> yes
```

Expression values are computed recursively and returned immediately. They do not require storage.
