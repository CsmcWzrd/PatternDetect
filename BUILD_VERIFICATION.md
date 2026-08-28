# Build Verification

Verified in the sandbox with GCC/CMake:

```text
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
./build/patterndetect --expr 'add(multiply(2,3),4)'
# 10
./tests/run_smoke_tests.sh ./build/patterndetect
# PatternDetect smoke tests passed
```

Large generated suite verification:

```text
./tests/generate_large_suite.py --output tests/large_suite --cases 1000 --logsets 100 --force
./tests/run_large_suite.py --binary ./build/patterndetect --suite tests/large_suite --timeout 15 --jobs 8 --report tests/LARGE_SUITE_VALIDATION.md
```

Result from the sandbox run:

```text
validated 100/1000 cases
validated 200/1000 cases
validated 300/1000 cases
validated 400/1000 cases
validated 500/1000 cases
validated 600/1000 cases
validated 700/1000 cases
validated 800/1000 cases
validated 900/1000 cases
validated 1000/1000 cases
PatternDetect large suite: 1000 cases, failures=0, elapsed=8.153s
Validation report: /mnt/data/PatternDetect/tests/LARGE_SUITE_VALIDATION.md
```

Coverage from the generated-suite manifest:

```text
pattern_count_range: [5, 10]
chain_depth_range: [1, 10]
fanout_range: [0, 9]
pattern_step_range: [1, 10]
rotation_count_range: [0, 10]
log_line_count_range: [300, 10000]
```

Windows and macOS use the same CMake project. On Windows 10+, use `build.bat` from a Visual Studio 2022 Developer Command Prompt.
