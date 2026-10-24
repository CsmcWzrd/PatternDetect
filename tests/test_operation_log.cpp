#include "operation_log.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <cstdio>

static std::string readAll(const std::string &p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

int main() {
    const std::string prefix = "operation_logging_unit";
    {
        pd::OperationLog logger(prefix, pd::OperationLog::Mode::Both, "worker@::1");
        auto root = logger.write("INFO", 0, {"root"}, "Starting", 0);
        assert(root == 1);
        auto child = logger.write("REPORT", root, {"root", "step 1", "step 2"},
                  "quoted " + std::string(pd::OperationLog::HUMAN_SEPARATOR) +
                  " and " + pd::OperationLog::SCRIPT_SEPARATOR + " \\ escaped", 2);
        assert(child == 2);
        logger.write("DEBUG", child, {"root", "binary"}, std::string("abc\0xyz", 7), 3);
        logger.write("WARN", root, {"root", std::string(400, 'x')}, std::string(400, 'a'), 1);
        logger.write("INFO", root, {"literal"}, "base64:not-encoded", 1);
    }
    auto human = readAll(prefix + ".human.log");
    auto script = readAll(prefix + ".script.log");
    assert(!human.empty() && !script.empty());
    assert(script.find("base64:YWJjAHh5eg==") != std::string::npos);
    assert(script.find("\\x20\\x20\\x29\\x29\\x29\\x29\\x29\\x20\\x20") != std::string::npos);
    assert(script.find("\\x20\\x20\\x2D\\x2D\\x2D\\x2D\\x2D\\x20\\x20") != std::string::npos);
    assert(script.find("event=2;parent=1") != std::string::npos);
    assert(script.find("\\x62ase64:not-encoded") != std::string::npos);
    std::istringstream in(human);
    std::string line;
    while (std::getline(in, line)) {
        assert(line.size() <= 128);
        if (!line.empty()) assert(line.compare(0, 4, "    ") == 0);
    }
    assert(human.find("0000000000000003") != std::string::npos);
    std::remove((prefix + ".human.log").c_str());
    std::remove((prefix + ".script.log").c_str());
    {
        pd::OperationLog logger(prefix, pd::OperationLog::Mode::HumanOnly);
        logger.write("INFO", 0, {"test"}, "human only", 0);
    }
    assert(!readAll(prefix + ".human.log").empty());
    assert(readAll(prefix + ".script.log").empty());
    std::remove((prefix + ".human.log").c_str());
    {
        pd::OperationLog logger(prefix, pd::OperationLog::Mode::ScriptOnly);
        logger.write("INFO", 0, {"test"}, "script only", 0);
    }
    assert(!readAll(prefix + ".script.log").empty());
    assert(readAll(prefix + ".human.log").empty());
    std::remove((prefix + ".script.log").c_str());
    return 0;
}
