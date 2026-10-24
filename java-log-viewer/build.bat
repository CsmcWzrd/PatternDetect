@echo off
setlocal
if not exist build\classes mkdir build\classes
if not exist dist mkdir dist
javac --release 11 -encoding UTF-8 -d build\classes src\patterndetect\viewer\*.java
if errorlevel 1 exit /b 1
jar --create --file dist\PatternDetectLogViewer.jar --main-class patterndetect.viewer.LiveLogViewer -C build\classes .
if errorlevel 1 exit /b 1
echo Created dist\PatternDetectLogViewer.jar
