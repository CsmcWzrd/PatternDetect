package patterndetect.viewer;

import java.awt.BorderLayout;
import java.awt.Font;
import java.awt.event.WindowAdapter;
import java.awt.event.WindowEvent;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.RandomAccessFile;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;
import javax.swing.JFileChooser;
import javax.swing.JFrame;
import javax.swing.JMenu;
import javax.swing.JMenuBar;
import javax.swing.JMenuItem;
import javax.swing.JOptionPane;
import javax.swing.JScrollPane;
import javax.swing.JSplitPane;
import javax.swing.JTabbedPane;
import javax.swing.JTable;
import javax.swing.JTextArea;
import javax.swing.SwingUtilities;
import javax.swing.WindowConstants;
import javax.swing.event.ListSelectionEvent;
import javax.swing.table.AbstractTableModel;

/** Follows script-readable logs and renders the human-readable records live. */
public final class LiveLogViewer extends JFrame {
    private static final long serialVersionUID = 1L;
    private static final int MAX_RECORD_BYTES = 2 * 1024 * 1024;
    private final JTabbedPane tabs = new JTabbedPane();
    private final Map<Long, OperationRecord> byId = new HashMap<>();
    private final Map<Long, List<OperationRecord>> children = new HashMap<>();
    private final Map<Long, RecordPane> panes = new HashMap<>();
    private final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t = new Thread(r, "PatternDetect-log-tail"); t.setDaemon(true); return t;
    });
    private Path path;
    private long offset = 0;
    private boolean paused = false;
    private final ByteArrayOutputStream pending = new ByteArrayOutputStream();

    public LiveLogViewer(Path scriptFile) {
        super("PatternDetect Live Operation Log Viewer");
        setDefaultCloseOperation(WindowConstants.DISPOSE_ON_CLOSE);
        setSize(1100, 720);
        setLocationByPlatform(true);
        setLayout(new BorderLayout());
        add(tabs, BorderLayout.CENTER);
        JMenuBar bar = new JMenuBar();
        JMenu menu = new JMenu("File");
        JMenuItem open = new JMenuItem("Open operation log...");
        open.addActionListener(e -> openFile());
        JMenuItem pause = new JMenuItem("Pause / Resume");
        pause.addActionListener(e -> paused = !paused);
        JMenuItem exit = new JMenuItem("Exit");
        exit.addActionListener(e -> dispose());
        menu.add(open); menu.add(pause); menu.add(exit); bar.add(menu); setJMenuBar(bar);
        this.path = scriptFile;
        newPane(0, "Main application thread");
        worker.scheduleWithFixedDelay(this::pollSafely, 0, 250, TimeUnit.MILLISECONDS);
        addWindowListener(new WindowAdapter() {
            @Override public void windowClosed(WindowEvent e) { worker.shutdownNow(); }
        });
    }

    private void openFile() {
        JFileChooser chooser = new JFileChooser();
        if (path != null && path.getParent() != null) chooser.setCurrentDirectory(path.getParent().toFile());
        if (chooser.showOpenDialog(this) == JFileChooser.APPROVE_OPTION) {
            Path next = chooser.getSelectedFile().toPath();
            synchronized (pending) { path = next; offset = 0; pending.reset(); }
            clearAll();
        }
    }

    private void clearAll() {
        byId.clear(); children.clear(); panes.clear(); tabs.removeAll();
        newPane(0, "Main application thread");
    }

    private void pollSafely() {
        if (paused || path == null) return;
        try { poll(); }
        catch (Exception e) {
            SwingUtilities.invokeLater(() -> setTitle("PatternDetect Live Viewer (read warning: " + e.getMessage() + ")"));
        }
    }

    private void poll() throws IOException {
        Path current = path;
        if (!Files.exists(current)) return; // Log file may be created after the viewer starts.
        List<OperationRecord> received = new ArrayList<>();
        boolean truncated = false;
        synchronized (pending) {
            try (RandomAccessFile f = new RandomAccessFile(current.toFile(), "r")) {
                if (f.length() < offset) { offset = 0; pending.reset(); truncated = true; }
                f.seek(offset);
                // Cap reading per poll to keep the GUI responsive during large appends.
                long available = Math.min(f.length() - offset, 2 * 1024 * 1024);
                byte[] chunk = new byte[8192];
                while (available > 0) {
                    int n = f.read(chunk, 0, (int)Math.min(chunk.length, available));
                    if (n < 0) break;
                    offset += n; available -= n;
                    for (int i = 0; i < n; i++) {
                        if (chunk[i] == '\n') {
                            String line = new String(pending.toByteArray(), StandardCharsets.UTF_8);
                            pending.reset();
                            if (line.endsWith("\r")) line = line.substring(0, line.length() - 1);
                            if (!line.isEmpty()) {
                                try { received.add(OperationRecord.parse(line)); }
                                catch (IllegalArgumentException invalid) { /* bad line: skip, keep following */ }
                            }
                        } else if (pending.size() < MAX_RECORD_BYTES) pending.write(chunk[i]);
                        else pending.reset(); // reject overlong malformed records
                    }
                }
            }
        }
        final boolean reset = truncated;
        if (reset || !received.isEmpty()) SwingUtilities.invokeLater(() -> {
            if (reset) clearAll();
            for (OperationRecord r : received) {
                if (byId.containsKey(r.id)) continue;
                byId.put(r.id, r);
                children.computeIfAbsent(r.parentId, k -> new ArrayList<>()).add(r);
            }
            for (RecordPane p : panes.values()) p.model.fireTableDataChanged();
            setTitle("PatternDetect Live Viewer - " + current + " (" + byId.size() + " events)");
        });
    }

    private void newPane(long parent, String title) {
        RecordPane pane = new RecordPane(parent);
        panes.put(parent, pane);
        tabs.addTab(title, pane.panel);
        tabs.setSelectedComponent(pane.panel);
    }

    private final class RecordPane {
        final long parent;
        final javax.swing.JPanel panel = new javax.swing.JPanel(new BorderLayout());
        final EventTableModel model = new EventTableModel();
        final JTable table = new JTable(model);
        final JTextArea detail = new JTextArea();

        RecordPane(long p) {
            parent = p;
            table.setAutoCreateRowSorter(true);
            table.setSelectionMode(javax.swing.ListSelectionModel.SINGLE_SELECTION);
            table.setRowHeight(25);
            detail.setEditable(false);
            detail.setFont(new Font(Font.MONOSPACED, Font.PLAIN, 12));
            detail.setLineWrap(false);
            JSplitPane split = new JSplitPane(JSplitPane.VERTICAL_SPLIT, new JScrollPane(table), new JScrollPane(detail));
            split.setResizeWeight(0.65);
            panel.add(split, BorderLayout.CENTER);
            table.getSelectionModel().addListSelectionListener(this::selected);
        }

        private void selected(ListSelectionEvent event) {
            if (event.getValueIsAdjusting() || table.getSelectedRow() < 0) return;
            int index = table.convertRowIndexToModel(table.getSelectedRow());
            List<OperationRecord> rows = children.get(parent);
            if (rows == null || index >= rows.size()) return;
            OperationRecord record = rows.get(index);
            detail.setText(record.renderHuman());
            detail.setCaretPosition(0);
            if (!panes.containsKey(record.id)) newPane(record.id, "[" + record.depth + "] " + record.summary());
            else tabs.setSelectedComponent(panes.get(record.id).panel);
        }

        final class EventTableModel extends AbstractTableModel {
            private static final long serialVersionUID = 1L;
            private final String[] columns = {"UTC", "Depth", "Severity", "Operation summary", "Message"};
            @Override public int getColumnCount() { return columns.length; }
            @Override public String getColumnName(int c) { return columns[c]; }
            @Override public int getRowCount() { return children.getOrDefault(parent, java.util.Collections.emptyList()).size(); }
            @Override public Object getValueAt(int row, int col) {
                OperationRecord r = children.get(parent).get(row);
                switch (col) {
                    case 0: return r.timestamp;
                    case 1: return r.depth;
                    case 2: return r.severity;
                    case 3: return r.summary();
                    default: return r.message;
                }
            }
        }
    }

    public static void main(String[] args) {
        final Path log;
        if (args.length == 2 && args[0].equals("--log")) log = Paths.get(args[1]);
        else if (args.length == 1 && !args[0].startsWith("--")) log = Paths.get(args[0]);
        else if (args.length == 0) log = null;
        else {
            System.err.println("Usage: java -jar PatternDetectLogViewer.jar [--log <prefix>.script.log]");
            System.exit(2); return;
        }
        SwingUtilities.invokeLater(() -> new LiveLogViewer(log).setVisible(true));
    }
}
