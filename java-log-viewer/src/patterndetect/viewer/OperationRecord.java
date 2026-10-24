package patterndetect.viewer;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Collections;
import java.util.List;
import java.util.regex.Pattern;

/** Immutable, independently parseable event for PatternDetect v1.2 operation logs. */
public final class OperationRecord {
    public static final String HUMAN_SEP = "  )))))  ";
    public static final String SCRIPT_SEP = "  -----  ";
    public final long id, parentId;
    public final int depth;
    public final String severity, timestamp, process, message, sourceHost, filterHost;
    public final List<String> trail;
    public final List<String> rawFields;

    private OperationRecord(long id, long parentId, int depth, String severity, String timestamp,
                            String process, String message, String sourceHost, String filterHost,
                            List<String> trail, List<String> rawFields) {
        this.id = id; this.parentId = parentId; this.depth = depth;
        this.severity = severity; this.timestamp = timestamp;
        this.process = process; this.message = message;
        this.sourceHost = sourceHost; this.filterHost = filterHost;
        this.trail = Collections.unmodifiableList(trail);
        this.rawFields = Collections.unmodifiableList(rawFields);
    }

    public static OperationRecord parse(String line) {
        String[] fields = line.split(Pattern.quote(SCRIPT_SEP), -1);
        if (fields.length != 5) throw new IllegalArgumentException("Expected exactly five log fields");
        String[] header = fields[0].split(Pattern.quote(HUMAN_SEP), -1);
        if (header.length != 5) throw new IllegalArgumentException("Expected five header parts");
        if (!header[0].matches("[0-9]{16}")) throw new IllegalArgumentException("Invalid 16-digit depth");
        int depth = Integer.parseInt(header[0]);
        if (!header[2].matches("\\d{4}-\\d\\d-\\d\\dT\\d\\d:\\d\\d:\\d\\d\\.\\d{6}Z"))
            throw new IllegalArgumentException("Invalid UTC microsecond timestamp");
        String[] processFields = fields[2].split(Pattern.quote(HUMAN_SEP), -1);
        if (processFields.length < 2 || !processFields[1].matches("event=[0-9]+;parent=[0-9]+"))
            throw new IllegalArgumentException("Missing event and parent IDs");
        String[] nums = processFields[1].split("[=;]");
        long id = Long.parseLong(nums[1]);
        long parent = Long.parseLong(nums[3]);
        List<String> trail = new ArrayList<>();
        for (String s : fields[3].split(Pattern.quote(HUMAN_SEP), -1)) trail.add(unescape(s));
        return new OperationRecord(id, parent, depth, unescape(fields[1]), header[2],
                unescape(processFields[0]), unescape(fields[4]), unescape(header[1]),
                unescape(header[3]), trail, Arrays.asList(fields));
    }

    public String summary() { return trail.isEmpty() ? "" : trail.get(trail.size() - 1); }

    public String renderHuman() {
        StringBuilder b = new StringBuilder();
        for (String field : rawFields) {
            b.append(wrap(field)).append('\n');
        }
        return b.append("\n\n").toString();
    }

    public static String wrap(String text) {
        StringBuilder b = new StringBuilder();
        int pos = 0;
        do {
            if (pos > 0) b.append('\n');
            b.append("    ");
            int start = pos, cursor = pos, lastSpace = -1, units = 0;
            while (cursor < text.length() && units < 124) {
                char c = text.charAt(cursor);
                int n = Character.charCount(text.codePointAt(cursor));
                if (c == '\\' && cursor + 3 < text.length() && text.charAt(cursor + 1) == 'x'
                        && isHex(text.charAt(cursor + 2)) && isHex(text.charAt(cursor + 3))) n = 4;
                else if (c == ' ') lastSpace = cursor;
                cursor += n;
                units++;
            }
            if (cursor < text.length() && lastSpace > start) cursor = lastSpace + 1;
            b.append(text, start, cursor);
            pos = cursor;
        } while (pos < text.length());
        return b.toString();
    }

    private static boolean isHex(char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    }

    public static String unescape(String input) {
        // Binary is deliberately not rendered as raw non-printable bytes in a GUI.
        if (input.startsWith("base64:")) {
            try { return "[binary " + Base64.getDecoder().decode(input.substring(7)).length + " bytes] " + input; }
            catch (IllegalArgumentException e) { return input; }
        }
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < input.length(); i++) {
            char c = input.charAt(i);
            if (c != '\\' || i + 1 >= input.length()) { b.append(c); continue; }
            char next = input.charAt(++i);
            if (next == 'x' && i + 2 < input.length() && isHex(input.charAt(i + 1)) && isHex(input.charAt(i + 2))) {
                int v = Integer.parseInt(input.substring(i + 1, i + 3), 16);
                b.append((char)v); i += 2;
            } else if (next == 'n') b.append('\n');
            else if (next == 'r') b.append('\r');
            else if (next == 't') b.append('\t');
            else if (next == '\\') b.append('\\');
            else { b.append('\\'); b.append(next); }
        }
        return b.toString();
    }
}
