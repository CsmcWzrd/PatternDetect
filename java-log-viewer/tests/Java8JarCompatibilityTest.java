import java.io.DataInputStream;
import java.io.InputStream;
import java.util.Enumeration;
import java.util.jar.JarEntry;
import java.util.jar.JarFile;

/** Runnable on Java 8; verify that EVERY class in the distributable JAR is Java 8 bytecode. */
public final class Java8JarCompatibilityTest {
    public static void main(String[] args) throws Exception {
        if (args.length != 1) throw new IllegalArgumentException("Usage: Java8JarCompatibilityTest <viewer.jar>");
        int count = 0;
        try (JarFile jar = new JarFile(args[0])) {
            if (jar.getManifest() == null || !"patterndetect.viewer.LiveLogViewer".equals(
                    jar.getManifest().getMainAttributes().getValue("Main-Class"))) {
                throw new AssertionError("Missing or incorrect executable JAR Main-Class");
            }
            for (Enumeration<JarEntry> entries = jar.entries(); entries.hasMoreElements(); ) {
                JarEntry entry = entries.nextElement();
                if (!entry.getName().endsWith(".class")) continue;
                try (InputStream stream = jar.getInputStream(entry);
                     DataInputStream in = new DataInputStream(stream)) {
                    if (in.readInt() != 0xCAFEBABE) throw new AssertionError("Not Java bytecode: " + entry.getName());
                    in.readUnsignedShort(); // minor version
                    int major = in.readUnsignedShort();
                    if (major > 52) throw new AssertionError("Java 8 incompatible bytecode " + major + ": " + entry.getName());
                    count++;
                }
            }
        }
        if (count == 0) throw new AssertionError("No Java class files in JAR");
        System.out.println("Java 8 JAR compatibility check passed: " + count + " classes, all major version <= 52");
    }
}
