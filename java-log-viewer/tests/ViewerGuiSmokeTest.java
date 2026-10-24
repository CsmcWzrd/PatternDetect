import patterndetect.viewer.LiveLogViewer;
import patterndetect.viewer.OperationRecord;
import java.awt.Component;
import java.awt.Container;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import javax.swing.JTabbedPane;
import javax.swing.JTable;
import javax.swing.SwingUtilities;

/** Run with xvfb-run on a headless Linux CI host; verifies live nested drilldown. */
public class ViewerGuiSmokeTest {
    private static JTabbedPane tabs;
    private static LiveLogViewer viewer;
    private static JTable findTable(Container parent) {
        for (Component c : parent.getComponents()) {
            if (c instanceof JTable) return (JTable)c;
            if (c instanceof Container) {
                JTable found = findTable((Container)c);
                if (found != null) return found;
            }
        }
        return null;
    }
    private static void assertRows(int tab, int rows) {
        JTable table = findTable((Container)tabs.getComponentAt(tab));
        if (table == null || table.getRowCount() != rows)
            throw new AssertionError("tab " + tab + " expected " + rows + " rows, got "
                + (table == null ? "missing" : table.getRowCount()));
    }
    private static String event(int id, int parent, int depth, String step) {
        String h = String.format("%016d", depth) + OperationRecord.HUMAN_SEP + "host@127.0.0.1" +
                OperationRecord.HUMAN_SEP + "2026-10-24T12:00:00.000001Z" +
                OperationRecord.HUMAN_SEP + "host@127.0.0.1" +
                OperationRecord.HUMAN_SEP + "2026-10-24T12:00:00.000002Z";
        return String.join(OperationRecord.SCRIPT_SEP, h, "INFO",
                "PatternDetect:123:456" + OperationRecord.HUMAN_SEP + "event=" + id + ";parent=" + parent,
                "scan" + OperationRecord.HUMAN_SEP + step, "processed " + step) + "\n";
    }
    private static void require(boolean okay, String msg) { if (!okay) throw new AssertionError(msg); }
    private static void awaitRows(int tab, int rows) throws Exception {
        for (int attempt = 0; attempt < 30; ++attempt) {
            try {
                SwingUtilities.invokeAndWait(() -> assertRows(tab, rows));
                return;
            } catch (java.lang.reflect.InvocationTargetException ex) {
                Thread.sleep(120);
            }
        }
        SwingUtilities.invokeAndWait(() -> assertRows(tab, rows));
    }
    public static void main(String[] args) throws Exception {
        Path file = Files.createTempFile("patterndetect_viewer_gui_", ".script.log");
        try {
            SwingUtilities.invokeAndWait(() -> {
                viewer = new LiveLogViewer(file);
                viewer.setVisible(true);
                tabs = (JTabbedPane)viewer.getContentPane().getComponent(0);
            });
            Files.write(file, event(1, 0, 0, "started").getBytes(StandardCharsets.UTF_8));
            awaitRows(0, 1);
            SwingUtilities.invokeAndWait(() -> findTable((Container)tabs.getComponentAt(0)).setRowSelectionInterval(0, 0));
            require(tabs.getTabCount() == 2, "first click did not open child tab");
            Files.write(file, event(2, 1, 1, "matched").getBytes(StandardCharsets.UTF_8), java.nio.file.StandardOpenOption.APPEND);
            awaitRows(1, 1); // tab updates in real time after being opened
            SwingUtilities.invokeAndWait(() -> findTable((Container)tabs.getComponentAt(1)).setRowSelectionInterval(0, 0));
            require(tabs.getTabCount() == 3, "second click did not open next level");
            Files.write(file, event(3, 2, 2, "report").getBytes(StandardCharsets.UTF_8), java.nio.file.StandardOpenOption.APPEND);
            awaitRows(2, 1);
            System.out.println("Live Swing GUI smoke passed: two nested tabs received real-time updates");
        } finally {
            if (viewer != null) SwingUtilities.invokeAndWait(() -> viewer.dispose());
            Files.deleteIfExists(file);
        }
    }
}
