# PatternDetect operation log format, version 1.2

**Authoritative delimiters** (ASCII, each is exactly 9 bytes):

- Human list delimiter: `  )))))  ` (two spaces, five `)`, two spaces)
- Script field delimiter: `  -----  ` (two spaces, five `-`, two spaces)

Each event has exactly five **logical** fields, in order:

1. `depth16  )))))  runningHostname@IP  )))))  localUTC  )))))  filteringHostname@IP  )))))  filterUTC`
2. `severity`
3. `PatternDetect:PID:threadID  )))))  event=eventId;parent=parentEventId`
4. `root summary  )))))  child summary  )))))  ...`
5. `PatternDetect application event message`

The logical depth is a zero-padded 16-digit unsigned decimal number. A root
event has depth 0 and parent 0; a child is one level deeper than its parent.
Event IDs are strictly increasing within one process log, starting at 1.

**Human-readable files:** every logical field is wrapped as one or more
physical lines, each starting with four ASCII spaces, maximum 128 displayed
characters including indentation. Longer lines wrap at word boundaries, except
for long unbroken tokens. All characters are retained. After the fifth field's
last physical line, exactly **two empty lines** are emitted. Human record
content may be reconstructed losslessly from the script log.

**Script-readable files:** concatenate the five logical fields using
`  -----  `, then end with one line-feed. Raw payloads cannot contain any
unescaped reserved delimiters. Fields 1, 3 and 4 contain their own human-list
delimiters. Parse the top-level five script fields first, then split fields
1, 3, and 4 on the human-list delimiter.

**Escaping:** every byte of either *complete* separator sequence appearing
inside a printable field payload is replaced by ASCII `\\xHH`; thus the entire
nine-byte sequence is encoded as nine successive `\\xHH` escapes. Literal
backslashes become `\\\\`, newlines `\\n`, tabs `\\t`, carriage returns
`\\r`. A literal `base64:` prefix begins with `\\x62` to disambiguate it
from binary encoding. Nontext binary payload becomes `base64:` followed by
RFC 4648 Base64, with `=` padding as required.

A script-record example (shown on one line):

```text
0000000000000002  )))))  host@192.0.2.10  )))))  2026-10-24T08:00:00.123456Z  )))))  host@192.0.2.10  )))))  2026-10-24T08:00:00.123457Z  -----  REPORT  -----  PatternDetect:1234:99  )))))  event=9;parent=8  -----  PatternDetect execution  )))))  Matched A  )))))  Report emitted  -----  REPORT: initial issue found
```

Example of a **literal separator inside a message**:

```text
original: a  )))))  b
escaped:  a\\x20\\x20\\x29\\x29\\x29\\x29\\x29\\x20\\x20b
```

Operation output defaults to the current working directory using the prefix
`patterndetect.operations.<pid>`. If `use processing-folder` is specified,
its directory becomes the default, or pass an explicit prefix using
`--operation-log-prefix`. Every process invocation uses unique files by
default, avoiding cross-process mixed lines. Both output streams flush each
event and use the same event identifiers.

A viewer should parse `.script.log` for hierarchy then render a human version
for display. The companion Swing viewer follows appends every 250 ms and
keeps opened descendant tabs live as new children are recorded.
