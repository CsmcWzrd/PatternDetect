import patterndetect.viewer.OperationRecord;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.List;

/** Unit tests and native C++ log interoperability verifier. */
public class ViewerParserTest {
    public static void main(String[] args) throws Exception {
        String sep = OperationRecord.HUMAN_SEP;
        String first = "0000000000000002" + sep + "host@127.0.0.1" + sep + "2026-10-24T08:11:00.123456Z"
                + sep + "node@::1" + sep + "2026-10-24T08:11:00.123457Z";
        String process = "PatternDetect:123:456" + sep + "event=3;parent=2";
        String line = String.join(OperationRecord.SCRIPT_SEP,
                first, "REPORT", process, "Start" + sep + "Match", "REPORT: message \\x20\\x20\\x29\\x29\\x29\\x29\\x29\\x20\\x20 end");
        OperationRecord r = OperationRecord.parse(line);
        require(r.id == 3 && r.parentId == 2 && r.depth == 2, "id/depth/parent");
        require(r.message.contains(OperationRecord.HUMAN_SEP), "decoded delimiter");
        require(r.summary().equals("Match"), "summary");
        require(r.renderHuman().endsWith("\n\n\n"), "human record terminator");
        require(allLinesWithin128(OperationRecord.wrap(repeat("X", 400))), "wrap width");
        require(OperationRecord.unescape("a\\\\b").equals("a\\b"), "backslash escape");
        expectFailure("not a log");
        expectFailure(line + OperationRecord.SCRIPT_SEP + "sixth");
        if (args.length == 1) {
            List<String> records = Files.readAllLines(Paths.get(args[0]), StandardCharsets.UTF_8);
            require(!records.isEmpty(), "native interoperability logs empty");
            long oldId = 0;
            for (String record : records) {
                OperationRecord p = OperationRecord.parse(record);
                require(p.id > oldId, "event ids strictly increasing");
                require(p.parentId < p.id, "parent must precede child");
                require(allLinesWithin128(p.renderHuman()), "native human width");
                oldId = p.id;
            }
            System.out.println("Verified " + records.size() + " native operation log events");
        }
        System.out.println("Viewer parser tests passed");
    }

    private static String repeat(String value, int count) {
        StringBuilder b = new StringBuilder(count * value.length());
        for (int i = 0; i < count; i++) b.append(value);
        return b.toString();
    }
    private static boolean allLinesWithin128(String text) {
        for (String line : text.split("\\n", -1)) if (line.length() > 128) return false;
        return true;
    }
    private static void require(boolean yes, String msg) { if (!yes) throw new AssertionError(msg); }
    private static void expectFailure(String line) {
        try { OperationRecord.parse(line); throw new AssertionError("invalid input accepted"); }
        catch (IllegalArgumentException good) { /* expected */ }
    }
}
