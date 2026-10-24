# Standalone optional Java Swing viewer (never built by the parent CMake project).
# JDK/OpenJDK 8+ and GNU make, including MinGW/MSYS on Windows.
JAVAC ?= javac
JAVA ?= java
JAR ?= jar
SOURCES := $(wildcard src/patterndetect/viewer/*.java)
ifeq ($(OS),Windows_NT)
CP_SEP := ;
else
CP_SEP := :
endif
CLASSES := build/classes
JARFILE := dist/PatternDetectLogViewer.jar

.PHONY: all run clean test
all: $(JARFILE)
$(JARFILE): $(SOURCES)
	mkdir -p $(CLASSES) dist
	$(JAVAC) -source 8 -target 8 -encoding UTF-8 -d $(CLASSES) $(SOURCES)
	$(JAR) cfe $(JARFILE) patterndetect.viewer.LiveLogViewer -C $(CLASSES) .
run: all
	$(JAVA) -jar $(JARFILE) $(ARGS)
test: all
	mkdir -p build/testclasses
	$(JAVAC) -source 8 -target 8 -encoding UTF-8 -cp $(CLASSES) -d build/testclasses tests/*.java
	$(JAVA) -ea -cp "$(CLASSES)$(CP_SEP)build/testclasses" ViewerParserTest
	$(JAVA) -ea -cp "$(CLASSES)$(CP_SEP)build/testclasses" Java8JarCompatibilityTest $(JARFILE)
clean:
	rm -rf build dist
