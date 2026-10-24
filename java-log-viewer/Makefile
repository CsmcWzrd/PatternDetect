# Standalone optional Java Swing viewer (never built by the parent CMake project).
# JDK/OpenJDK 11+ and GNU make, including MinGW/MSYS on Windows.
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
	$(JAVAC) --release 11 -encoding UTF-8 -d $(CLASSES) $(SOURCES)
	$(JAR) --create --file $(JARFILE) --main-class patterndetect.viewer.LiveLogViewer -C $(CLASSES) .
run: all
	$(JAVA) -jar $(JARFILE) $(ARGS)
test: all
	mkdir -p build/testclasses
	$(JAVAC) --release 11 -encoding UTF-8 -cp $(CLASSES) -d build/testclasses tests/ViewerParserTest.java
	$(JAVA) -ea -cp "$(CLASSES)$(CP_SEP)build/testclasses" ViewerParserTest
clean:
	rm -rf build dist
