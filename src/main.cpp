#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#ifdef PATTERNDETECT_HAS_ZLIB
#include <zlib.h>
#endif

namespace pd {

static const char *kVersion = "1.1.0";

static inline std::string ltrim(std::string s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    return s;
}

static inline std::string rtrim(std::string s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    return s;
}

static inline std::string trim(const std::string &s) { return rtrim(ltrim(s)); }

static inline bool startsWith(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

static inline bool endsWith(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() && std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

static inline std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static inline std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

static std::string stripComments(const std::string &line) {
    bool inQuote = false;
    bool escape = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escape) {
            escape = false;
            continue;
        }
        if (c == '\\') {
            escape = true;
            continue;
        }
        if (c == '"') {
            inQuote = !inQuote;
            continue;
        }
        if (!inQuote && c == '#') {
            return rtrim(line.substr(0, i));
        }
        if (!inQuote && c == '/' && i + 1 < line.size() && line[i + 1] == '/') {
            return rtrim(line.substr(0, i));
        }
    }
    return rtrim(line);
}

static std::string unquote(const std::string &s) {
    std::string t = trim(s);
    if (t.size() >= 2 && t.front() == '"' && t.back() == '"') {
        std::string out;
        bool escape = false;
        for (std::size_t i = 1; i + 1 < t.size(); ++i) {
            char c = t[i];
            if (escape) {
                switch (c) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                default: out.push_back(c); break;
                }
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else {
                out.push_back(c);
            }
        }
        return out;
    }
    return t;
}

static bool extractFirstQuoted(const std::string &s, std::string &quoted, std::size_t *endPos = nullptr) {
    bool escape = false;
    std::size_t start = std::string::npos;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '"') { start = i; break; }
    }
    if (start == std::string::npos) return false;
    std::string out;
    for (std::size_t i = start + 1; i < s.size(); ++i) {
        char c = s[i];
        if (escape) {
            switch (c) {
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            default: out.push_back(c); break;
            }
            escape = false;
        } else if (c == '\\') {
            escape = true;
        } else if (c == '"') {
            quoted = out;
            if (endPos) *endPos = i + 1;
            return true;
        } else {
            out.push_back(c);
        }
    }
    return false;
}

static std::vector<std::string> splitSimple(const std::string &s, const std::string &sep) {
    std::vector<std::string> out;
    if (sep.empty() || sep == " ") {
        std::istringstream iss(s);
        std::string tok;
        while (iss >> tok) out.push_back(tok);
        return out;
    }
    std::size_t pos = 0;
    while (pos <= s.size()) {
        std::size_t next = s.find(sep, pos);
        if (next == std::string::npos) {
            out.push_back(s.substr(pos));
            break;
        }
        out.push_back(s.substr(pos, next - pos));
        pos = next + sep.size();
    }
    return out;
}

static std::vector<std::string> splitCommaOutsideQuotes(const std::string &s) {
    std::vector<std::string> out;
    bool inQuote = false;
    bool escape = false;
    std::size_t start = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (escape) { escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote && c == ',') {
            out.push_back(trim(s.substr(start, i - start)));
            start = i + 1;
        }
    }
    out.push_back(trim(s.substr(start)));
    return out;
}

struct Value {
    enum class Type { Null, Number, String, Array, Bool } type = Type::Null;
    double number = 0.0;
    bool boolean = false;
    std::string text;
    std::vector<Value> array;

    Value() = default;
    explicit Value(double n) : type(Type::Number), number(n) {}
    explicit Value(bool b) : type(Type::Bool), boolean(b) {}
    explicit Value(std::string s) : type(Type::String), text(std::move(s)) {}
    explicit Value(std::vector<Value> a) : type(Type::Array), array(std::move(a)) {}

    bool isNumberLike() const {
        if (type == Type::Number || type == Type::Bool) return true;
        if (type != Type::String) return false;
        char *end = nullptr;
        errno = 0;
        std::strtod(text.c_str(), &end);
        return errno == 0 && end && *end == '\0' && end != text.c_str();
    }

    double asNumber() const {
        if (type == Type::Number) return number;
        if (type == Type::Bool) return boolean ? 1.0 : 0.0;
        if (type == Type::String) {
            char *end = nullptr;
            double v = std::strtod(text.c_str(), &end);
            if (end && *end == '\0' && end != text.c_str()) return v;
        }
        return 0.0;
    }

    bool asBool() const {
        if (type == Type::Bool) return boolean;
        if (type == Type::Number) return number != 0.0;
        if (type == Type::String) {
            std::string l = toLower(trim(text));
            return !(l.empty() || l == "0" || l == "false" || l == "no" || l == "null");
        }
        if (type == Type::Array) return !array.empty();
        return false;
    }

    std::string asString() const {
        std::ostringstream oss;
        switch (type) {
        case Type::Null: return "";
        case Type::Bool: return boolean ? "true" : "false";
        case Type::Number:
            oss << std::setprecision(15) << number;
            return oss.str();
        case Type::String: return text;
        case Type::Array:
            for (std::size_t i = 0; i < array.size(); ++i) {
                if (i) oss << ' ';
                oss << array[i].asString();
            }
            return oss.str();
        }
        return "";
    }

    std::string debugString() const {
        if (type != Type::Array) return asString();
        std::ostringstream oss;
        oss << '[';
        for (std::size_t i = 0; i < array.size(); ++i) {
            if (i) oss << ", ";
            oss << '"' << array[i].asString() << '"';
        }
        oss << ']';
        return oss.str();
    }
};

using Context = std::unordered_map<std::string, Value>;

class ExpressionEvaluator {
public:
    explicit ExpressionEvaluator(const Context &ctx) : ctx_(ctx) {}

    Value eval(const std::string &input) {
        s_ = input;
        pos_ = 0;
        Value v = parseExpression();
        skipWs();
        if (pos_ != s_.size()) {
            throw std::runtime_error("expression parse stopped before end near: " + s_.substr(pos_));
        }
        return v;
    }

private:
    const Context &ctx_;
    std::string s_;
    std::size_t pos_ = 0;

    void skipWs() {
        while (pos_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[pos_]))) ++pos_;
    }

    bool consume(char c) {
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == c) { ++pos_; return true; }
        return false;
    }

    Value parseExpression() {
        skipWs();
        if (pos_ >= s_.size()) return Value();
        if (s_[pos_] == '"') return parseString();
        if (s_[pos_] == '-' || std::isdigit(static_cast<unsigned char>(s_[pos_]))) {
            if (isNumberStart()) return parseNumber();
        }
        std::string ident = parseIdentifierOrToken();
        skipWs();
        if (consume('(')) {
            std::vector<Value> args;
            skipWs();
            if (!consume(')')) {
                while (true) {
                    args.push_back(parseExpression());
                    skipWs();
                    if (consume(')')) break;
                    if (!consume(',')) throw std::runtime_error("expected ',' or ')' in function call: " + ident);
                }
            }
            return call(ident, args);
        }
        auto it = ctx_.find(ident);
        if (it != ctx_.end()) return it->second;
        return Value(ident);
    }

    bool isNumberStart() const {
        std::size_t p = pos_;
        if (p < s_.size() && s_[p] == '-') ++p;
        if (p >= s_.size()) return false;
        return std::isdigit(static_cast<unsigned char>(s_[p])) || s_[p] == '.';
    }

    Value parseNumber() {
        std::size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            ++pos_;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            while (pos_ < s_.size() && std::isdigit(static_cast<unsigned char>(s_[pos_]))) ++pos_;
        }
        return Value(std::strtod(s_.substr(start, pos_ - start).c_str(), nullptr));
    }

    Value parseString() {
        if (s_[pos_] != '"') throw std::runtime_error("expected string");
        ++pos_;
        std::string out;
        bool escape = false;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (escape) {
                switch (c) {
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                default: out.push_back(c); break;
                }
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else if (c == '"') {
                return Value(out);
            } else {
                out.push_back(c);
            }
        }
        throw std::runtime_error("unterminated string literal");
    }

    std::string parseIdentifierOrToken() {
        skipWs();
        std::size_t start = pos_;
        while (pos_ < s_.size()) {
            char c = s_[pos_];
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == ')' || c == '(') break;
            ++pos_;
        }
        if (start == pos_) throw std::runtime_error("expected token near: " + s_.substr(pos_));
        return s_.substr(start, pos_ - start);
    }

    static void need(const std::string &name, const std::vector<Value> &args, std::size_t n) {
        if (args.size() < n) throw std::runtime_error(name + " needs at least " + std::to_string(n) + " arguments");
    }

    static int cmpValues(const Value &a, const Value &b) {
        if (a.isNumberLike() && b.isNumberLike()) {
            double x = a.asNumber(), y = b.asNumber();
            if (x < y) return -1;
            if (x > y) return 1;
            return 0;
        }
        std::string x = a.asString(), y = b.asString();
        if (x < y) return -1;
        if (x > y) return 1;
        return 0;
    }

    Value call(std::string name, const std::vector<Value> &args) {
        name = toLower(name);
        if (name == "add" || name == "sum") { double r = 0; for (const auto &a : args) r += a.asNumber(); return Value(r); }
        if (name == "sub" || name == "subtract") { need(name, args, 1); double r = args[0].asNumber(); for (std::size_t i = 1; i < args.size(); ++i) r -= args[i].asNumber(); return Value(r); }
        if (name == "mul" || name == "multiply") { double r = 1; for (const auto &a : args) r *= a.asNumber(); return Value(r); }
        if (name == "div" || name == "divide") { need(name, args, 2); double r = args[0].asNumber(); for (std::size_t i = 1; i < args.size(); ++i) r /= args[i].asNumber(); return Value(r); }
        if (name == "mod") { need(name, args, 2); return Value(std::fmod(args[0].asNumber(), args[1].asNumber())); }
        if (name == "pow") { need(name, args, 2); return Value(std::pow(args[0].asNumber(), args[1].asNumber())); }
        if (name == "sqrt") { need(name, args, 1); return Value(std::sqrt(args[0].asNumber())); }
        if (name == "abs") { need(name, args, 1); return Value(std::fabs(args[0].asNumber())); }
        if (name == "min") { need(name, args, 1); double r = args[0].asNumber(); for (const auto &a : args) r = std::min(r, a.asNumber()); return Value(r); }
        if (name == "max") { need(name, args, 1); double r = args[0].asNumber(); for (const auto &a : args) r = std::max(r, a.asNumber()); return Value(r); }
        if (name == "clamp") { need(name, args, 3); return Value(std::max(args[1].asNumber(), std::min(args[0].asNumber(), args[2].asNumber()))); }
        if (name == "round") { need(name, args, 1); return Value(std::round(args[0].asNumber())); }
        if (name == "floor") { need(name, args, 1); return Value(std::floor(args[0].asNumber())); }
        if (name == "ceil") { need(name, args, 1); return Value(std::ceil(args[0].asNumber())); }
        if (name == "sin") { need(name, args, 1); return Value(std::sin(args[0].asNumber())); }
        if (name == "cos") { need(name, args, 1); return Value(std::cos(args[0].asNumber())); }
        if (name == "tan") { need(name, args, 1); return Value(std::tan(args[0].asNumber())); }
        if (name == "asin") { need(name, args, 1); return Value(std::asin(args[0].asNumber())); }
        if (name == "acos") { need(name, args, 1); return Value(std::acos(args[0].asNumber())); }
        if (name == "atan") { need(name, args, 1); return Value(std::atan(args[0].asNumber())); }
        if (name == "atan2") { need(name, args, 2); return Value(std::atan2(args[0].asNumber(), args[1].asNumber())); }
        if (name == "deg2rad") { need(name, args, 1); return Value(args[0].asNumber() * 3.14159265358979323846 / 180.0); }
        if (name == "rad2deg") { need(name, args, 1); return Value(args[0].asNumber() * 180.0 / 3.14159265358979323846); }
        if (name == "log") { need(name, args, 1); return Value(std::log(args[0].asNumber())); }
        if (name == "log10") { need(name, args, 1); return Value(std::log10(args[0].asNumber())); }
        if (name == "exp") { need(name, args, 1); return Value(std::exp(args[0].asNumber())); }
        if (name == "concat") { std::string r; for (const auto &a : args) r += a.asString(); return Value(r); }
        if (name == "upper") { need(name, args, 1); return Value(toUpper(args[0].asString())); }
        if (name == "lower") { need(name, args, 1); return Value(toLower(args[0].asString())); }
        if (name == "trim") { need(name, args, 1); return Value(pd::trim(args[0].asString())); }
        if (name == "length" || name == "len") { need(name, args, 1); if (args[0].type == Value::Type::Array) return Value(static_cast<double>(args[0].array.size())); return Value(static_cast<double>(args[0].asString().size())); }
        if (name == "contains") { need(name, args, 2); return Value(args[0].asString().find(args[1].asString()) != std::string::npos); }
        if (name == "startswith") { need(name, args, 2); return Value(startsWith(args[0].asString(), args[1].asString())); }
        if (name == "endswith") { need(name, args, 2); return Value(endsWith(args[0].asString(), args[1].asString())); }
        if (name == "substr") { need(name, args, 2); std::string x = args[0].asString(); std::size_t p = static_cast<std::size_t>(std::max(0.0, args[1].asNumber())); std::size_t n = args.size() >= 3 ? static_cast<std::size_t>(std::max(0.0, args[2].asNumber())) : std::string::npos; if (p > x.size()) return Value(std::string()); return Value(x.substr(p, n)); }
        if (name == "replace") { need(name, args, 3); std::string x = args[0].asString(), from = args[1].asString(), to = args[2].asString(); if (from.empty()) return Value(x); std::size_t pos = 0; while ((pos = x.find(from, pos)) != std::string::npos) { x.replace(pos, from.size(), to); pos += to.size(); } return Value(x); }
        if (name == "split") { need(name, args, 1); std::string sep = args.size() >= 2 ? args[1].asString() : " "; std::vector<Value> arr; for (const auto &t : splitSimple(args[0].asString(), sep)) arr.emplace_back(t); return Value(arr); }
        if (name == "join") { need(name, args, 1); std::string sep = args.size() >= 2 ? args[1].asString() : " "; std::ostringstream oss; if (args[0].type == Value::Type::Array) { for (std::size_t i = 0; i < args[0].array.size(); ++i) { if (i) oss << sep; oss << args[0].array[i].asString(); } } else { oss << args[0].asString(); } return Value(oss.str()); }
        if (name == "eq" || name == "equals") { need(name, args, 2); return Value(cmpValues(args[0], args[1]) == 0); }
        if (name == "ne" || name == "notequals") { need(name, args, 2); return Value(cmpValues(args[0], args[1]) != 0); }
        if (name == "lt") { need(name, args, 2); return Value(cmpValues(args[0], args[1]) < 0); }
        if (name == "lte") { need(name, args, 2); return Value(cmpValues(args[0], args[1]) <= 0); }
        if (name == "gt") { need(name, args, 2); return Value(cmpValues(args[0], args[1]) > 0); }
        if (name == "gte") { need(name, args, 2); return Value(cmpValues(args[0], args[1]) >= 0); }
        if (name == "and") { for (const auto &a : args) if (!a.asBool()) return Value(false); return Value(true); }
        if (name == "or") { for (const auto &a : args) if (a.asBool()) return Value(true); return Value(false); }
        if (name == "not") { need(name, args, 1); return Value(!args[0].asBool()); }
        if (name == "if") { need(name, args, 3); return args[0].asBool() ? args[1] : args[2]; }
        throw std::runtime_error("unknown function: " + name);
    }
};

static time_t portableTimegm(std::tm *tmv) {
#ifdef _WIN32
    return _mkgmtime(tmv);
#else
    return timegm(tmv);
#endif
}

static int timezoneOffsetSeconds(const std::string &tz, bool &known) {
    known = true;
    std::string z = toUpper(trim(tz));
    if (z.empty() || z == "Z" || z == "UTC" || z == "GMT") return 0;
    if (z == "IST") return 5 * 3600 + 30 * 60;
    std::smatch m;
    if (std::regex_match(z, m, std::regex(R"(([+-])(\d{2}):?(\d{2})?)"))) {
        int sign = m[1].str() == "-" ? -1 : 1;
        int hh = std::stoi(m[2].str());
        int mm = m[3].matched ? std::stoi(m[3].str()) : 0;
        return sign * (hh * 3600 + mm * 60);
    }
    known = false;
    return 0;
}

static bool parseEpochMicros(const std::string &input, long long &epochMicros) {
    std::string s = trim(input);
    if (s.empty()) return false;
    s = unquote(s);

    std::smatch m;
    static const std::regex numericRe(R"(^[+-]?\d+(\.\d+)?$)");
    if (std::regex_match(s, m, numericRe)) {
        long double v = std::strtold(s.c_str(), nullptr);
        std::string whole = s;
        std::size_t dot = whole.find('.');
        if (dot != std::string::npos) whole = whole.substr(0, dot);
        std::size_t digits = (whole[0] == '-' || whole[0] == '+') ? whole.size() - 1 : whole.size();
        if (digits >= 16) epochMicros = static_cast<long long>(v);
        else if (digits >= 13) epochMicros = static_cast<long long>(v * 1000.0L);
        else epochMicros = static_cast<long long>(v * 1000000.0L);
        return true;
    }

    static const std::regex isoRe(R"((\d{4})-(\d{2})-(\d{2})[ T](\d{2}):(\d{2}):(\d{2})(?:\.(\d{1,6}))?\s*([A-Za-z]{1,4}|[+-]\d{2}:?\d{0,2}|Z)?)");
    if (std::regex_search(s, m, isoRe)) {
        std::tm tmv{};
        tmv.tm_year = std::stoi(m[1].str()) - 1900;
        tmv.tm_mon = std::stoi(m[2].str()) - 1;
        tmv.tm_mday = std::stoi(m[3].str());
        tmv.tm_hour = std::stoi(m[4].str());
        tmv.tm_min = std::stoi(m[5].str());
        tmv.tm_sec = std::stoi(m[6].str());
        int micros = 0;
        if (m[7].matched) {
            std::string frac = m[7].str();
            while (frac.size() < 6) frac.push_back('0');
            micros = std::stoi(frac.substr(0, 6));
        }
        bool known = false;
        int offset = m[8].matched ? timezoneOffsetSeconds(m[8].str(), known) : 0;
        time_t t = portableTimegm(&tmv);
        if (t == static_cast<time_t>(-1)) return false;
        if (m[8].matched && known) t -= offset;
        epochMicros = static_cast<long long>(t) * 1000000LL + micros;
        return true;
    }

    static const std::map<std::string, int> months{{"Jan",0},{"Feb",1},{"Mar",2},{"Apr",3},{"May",4},{"Jun",5},{"Jul",6},{"Aug",7},{"Sep",8},{"Oct",9},{"Nov",10},{"Dec",11}};
    static const std::regex syslogRe(R"((Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec)\s+(\d{1,2})\s+(\d{2}):(\d{2}):(\d{2}))");
    if (std::regex_search(s, m, syslogRe)) {
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm *gmt = std::gmtime(&now);
        std::tm tmv{};
        tmv.tm_year = gmt ? gmt->tm_year : 126;
        tmv.tm_mon = months.at(m[1].str());
        tmv.tm_mday = std::stoi(m[2].str());
        tmv.tm_hour = std::stoi(m[3].str());
        tmv.tm_min = std::stoi(m[4].str());
        tmv.tm_sec = std::stoi(m[5].str());
        time_t t = portableTimegm(&tmv);
        if (t == static_cast<time_t>(-1)) return false;
        epochMicros = static_cast<long long>(t) * 1000000LL;
        return true;
    }
    return false;
}

struct CaptureSpec { std::string name; };
struct LogStep {
    std::string fragment;
    std::vector<CaptureSpec> grabs;
};

enum class CompareOp { Equals, NotEquals, Less, Greater, LessEq, GreaterEq };
enum class Logic { And, Or, NotAnd, NotOr, AndNot, OrNot };

struct CheckSpec {
    Logic logic = Logic::And;
    std::string lhs;
    std::string rhs;
    CompareOp op = CompareOp::Equals;
};

struct MessageSpec {
    std::string message;
    std::vector<std::string> args;
};

struct Pattern {
    std::string id;
    std::string title;
    std::string within;
    std::string logFile;
    std::string component;
    std::string separator = " ";
    std::vector<LogStep> steps;
    std::vector<CheckSpec> checks;
    std::vector<MessageSpec> reports;
    std::vector<MessageSpec> debugs;
    std::vector<std::string> onHits;
    int sourceLine = 0;
};

struct Config {
    bool reverseOrder = false;
    bool useRotated = false;
    bool autoUncompress = true;
    std::string processingFolder;
    std::string rebootMessage;
    int stopOnRebootCount = -1;
    long long dateAfterMicros = std::numeric_limits<long long>::min();
    long long dateBeforeMicros = std::numeric_limits<long long>::max();
    std::string detailLog;
    std::vector<std::string> destinations{"terminal"};
    std::vector<Pattern> patterns;
    std::vector<std::string> warnings;
};

static bool parseCompareOp(const std::string &s, CompareOp &op) {
    std::string x = toLower(trim(s));
    if (x == "equals" || x == "==") { op = CompareOp::Equals; return true; }
    if (x == "not-equals" || x == "!=" || x == "notequals") { op = CompareOp::NotEquals; return true; }
    if (x == "less-than" || x == "<") { op = CompareOp::Less; return true; }
    if (x == "greater-than" || x == ">") { op = CompareOp::Greater; return true; }
    if (x == "less-than-equal-to" || x == "<=") { op = CompareOp::LessEq; return true; }
    if (x == "greater-than-equal-to" || x == ">=") { op = CompareOp::GreaterEq; return true; }
    return false;
}

static std::vector<std::string> shellWords(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    bool inQuote = false, escape = false;
    for (char c : s) {
        if (escape) { cur.push_back(c); escape = false; continue; }
        if (c == '\\') { escape = true; continue; }
        if (c == '"') { inQuote = !inQuote; continue; }
        if (!inQuote && std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else cur.push_back(c);
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

class ConfigParser {
public:
    Config parseFile(const std::string &path) {
        std::ifstream in(path.c_str());
        if (!in) throw std::runtime_error("cannot open config file: " + path);
        Config cfg;
        Pattern *cur = nullptr;
        Logic pendingLogic = Logic::And;
        std::string raw;
        int lineNo = 0;
        while (std::getline(in, raw)) {
            ++lineNo;
            std::string line = trim(stripComments(raw));
            if (line.empty() || line == "{" || line == "}") continue;

            std::smatch m;
            if (!cur && std::regex_match(line, m, std::regex(R"(([A-Za-z][A-Za-z0-9]*)\s*:\s*\[)"))) {
                cfg.patterns.push_back(Pattern());
                cur = &cfg.patterns.back();
                cur->id = m[1].str();
                cur->sourceLine = lineNo;
                pendingLogic = Logic::And;
                continue;
            }
            if (cur && line == "]") { cur = nullptr; continue; }

            if (!cur) {
                parseUse(line, cfg, lineNo);
            } else {
                parsePatternLine(line, *cur, cfg, lineNo, pendingLogic);
            }
        }
        if (cur) cfg.warnings.push_back("configuration ended before pattern '" + cur->id + "' was closed with ]");
        return cfg;
    }

private:
    static std::string afterPrefix(const std::string &line, const std::string &prefix) {
        return trim(line.substr(prefix.size()));
    }

    void parseUse(const std::string &line, Config &cfg, int lineNo) {
        if (!startsWith(line, "use ")) return;
        std::string rest = trim(line.substr(4));
        std::string lower = toLower(rest);
        if (lower == "reverse-chronological-order") cfg.reverseOrder = true;
        else if (lower == "chronological-order") cfg.reverseOrder = false;
        else if (startsWith(lower, "date-after ")) {
            long long v; if (parseEpochMicros(rest.substr(11), v)) cfg.dateAfterMicros = v; else cfg.warnings.push_back("line " + std::to_string(lineNo) + ": date-after could not be parsed");
        } else if (startsWith(lower, "date-before ")) {
            long long v; if (parseEpochMicros(rest.substr(12), v)) cfg.dateBeforeMicros = v; else cfg.warnings.push_back("line " + std::to_string(lineNo) + ": date-before could not be parsed");
        } else if (startsWith(lower, "reboot-timestamp-from-message")) {
            std::string q; if (extractFirstQuoted(rest, q)) cfg.rebootMessage = q; else cfg.rebootMessage = trim(rest.substr(std::string("reboot-timestamp-from-message").size()));
        } else if (startsWith(lower, "stop-on-reboot")) {
            auto words = shellWords(rest);
            cfg.stopOnRebootCount = words.size() >= 2 ? std::atoi(words[1].c_str()) : 1;
        } else if (lower == "log-rotated") cfg.useRotated = true;
        else if (lower == "auto-uncompress") cfg.autoUncompress = true;
        else if (startsWith(lower, "processing-folder ")) cfg.processingFolder = trim(rest.substr(18));
        else if (startsWith(lower, "detail-pattern-matched-log ")) cfg.detailLog = trim(rest.substr(27));
        else if (startsWith(lower, "report-destination ")) {
            std::string d = trim(rest.substr(19));
            if (cfg.destinations.size() == 1 && cfg.destinations[0] == "terminal") cfg.destinations.clear();
            cfg.destinations.push_back(d);
        } else if (startsWith(lower, "convert-to-")) {
            cfg.warnings.push_back("line " + std::to_string(lineNo) + ": timezone conversion directive is parsed but detailed timezone database conversion is not required for UTC epoch matching");
        } else if (rest.find('/') != std::string::npos || rest.find('<') != std::string::npos) {
            cfg.warnings.push_back("line " + std::to_string(lineNo) + ": looks like specification text, not an active directive: " + rest);
        }
    }

    void parsePatternLine(const std::string &line, Pattern &p, Config &cfg, int lineNo, Logic &pendingLogic) {
        std::string lower = toLower(line);
        if (startsWith(lower, "title ")) {
            std::string q; p.title = extractFirstQuoted(line, q) ? q : afterPrefix(line, "title");
        } else if (startsWith(lower, "within ")) {
            p.within = afterPrefix(line, "within");
        } else if (startsWith(lower, "log ")) {
            parseLogLine(line, p);
        } else if (startsWith(lower, "component ")) {
            p.component = afterPrefix(line, "component");
        } else if (startsWith(lower, "grab ")) {
            parseGrabLine(afterPrefix(line, "grab"), p);
        } else if (startsWith(lower, "report ")) {
            p.reports.push_back(parseMessage(line, "report"));
        } else if (startsWith(lower, "debug ")) {
            p.debugs.push_back(parseMessage(line, "debug"));
        } else if (startsWith(lower, "check ")) {
            CheckSpec c;
            c.logic = pendingLogic;
            pendingLogic = Logic::And;
            if (parseCheck(afterPrefix(line, "check"), c)) p.checks.push_back(c);
            else cfg.warnings.push_back("line " + std::to_string(lineNo) + ": check statement ignored because it could not be parsed: " + line);
        } else if (lower == "check-or") pendingLogic = Logic::Or;
        else if (lower == "check-and") pendingLogic = Logic::And;
        else if (lower == "check-not-or") pendingLogic = Logic::NotOr;
        else if (lower == "check-not-and") pendingLogic = Logic::NotAnd;
        else if (lower == "check-and-not") pendingLogic = Logic::AndNot;
        else if (lower == "check-or-not") pendingLogic = Logic::OrNot;
        else if (startsWith(lower, "on-hit:")) {
            std::string target = trim(line.substr(line.find(':') + 1));
            if (!target.empty()) p.onHits.push_back(target);
        } else if (line.find('/') != std::string::npos && line.find("check-") != std::string::npos) {
            cfg.warnings.push_back("line " + std::to_string(lineNo) + ": looks like check operator specification text, not an active check connector");
        } else {
            if (!p.steps.empty()) p.steps.back().fragment = line;
            else p.steps.push_back(LogStep{line, {}});
        }
    }

    static MessageSpec parseMessage(const std::string &line, const std::string &keyword) {
        MessageSpec m;
        std::size_t endPos = 0;
        std::string q;
        if (extractFirstQuoted(line, q, &endPos)) {
            m.message = q;
            std::string rest = trim(line.substr(endPos));
            m.args = shellWords(rest);
        } else {
            m.message = trim(line.substr(keyword.size()));
        }
        return m;
    }

    static bool parseCheck(const std::string &body, CheckSpec &c) {
        auto words = shellWords(body);
        if (words.size() < 3) return false;
        if (!parseCompareOp(words.back(), c.op)) return false;
        c.lhs = words[0];
        words.pop_back();
        words.erase(words.begin());
        std::ostringstream rhs;
        for (std::size_t i = 0; i < words.size(); ++i) { if (i) rhs << ' '; rhs << words[i]; }
        c.rhs = rhs.str();
        return true;
    }

    static void parseLogLine(const std::string &line, Pattern &p) {
        std::string body = trim(line.substr(4));
        std::string lower = toLower(body);
        if (startsWith(lower, "message")) {
            std::string frag = trim(body.substr(7));
            std::size_t sepPos = toLower(frag).find(" separator ");
            if (sepPos != std::string::npos) {
                std::string sepSpec = trim(frag.substr(sepPos + 11));
                std::string q;
                p.separator = extractFirstQuoted(sepSpec, q) ? q : sepSpec;
                frag = trim(frag.substr(0, sepPos));
            }
            if (toLower(frag) == "fragment") frag.clear();
            p.steps.push_back(LogStep{frag, {}});
        } else {
            p.logFile = body;
        }
    }

    static void parseGrabLine(const std::string &body, Pattern &p) {
        if (p.steps.empty()) p.steps.push_back(LogStep{});
        auto parts = splitCommaOutsideQuotes(body);
        for (auto item : parts) {
            item = trim(item);
            if (item.empty() || item == "..." || item.find("...") != std::string::npos) continue;
            p.steps.back().grabs.push_back(CaptureSpec{item});
        }
    }
};

class TcpClient {
public:
    TcpClient() {
#ifdef _WIN32
        static bool initialized = false;
        if (!initialized) {
            WSADATA wsa;
            if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) initialized = true;
        }
#endif
    }
    bool sendText(const std::string &host, const std::string &port, const std::string &text, std::string &err) {
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *res = nullptr;
        int rc = getaddrinfo(host.c_str(), port.c_str(), &hints, &res);
        if (rc != 0 || !res) { err = "getaddrinfo failed"; return false; }
        bool ok = false;
        for (auto *p = res; p; p = p->ai_next) {
#ifdef _WIN32
            SOCKET fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd == INVALID_SOCKET) continue;
            if (connect(fd, p->ai_addr, static_cast<int>(p->ai_addrlen)) == 0) {
                int sent = ::send(fd, text.c_str(), static_cast<int>(text.size()), 0);
                ok = sent == static_cast<int>(text.size());
            }
            closesocket(fd);
#else
            int fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
            if (fd < 0) continue;
            if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) {
                ssize_t sent = ::send(fd, text.c_str(), text.size(), 0);
                ok = sent == static_cast<ssize_t>(text.size());
            }
            close(fd);
#endif
            if (ok) break;
        }
        freeaddrinfo(res);
        if (!ok) err = "could not connect/send to " + host + ":" + port;
        return ok;
    }
};

class Reporter {
public:
    explicit Reporter(const Config &cfg) : destinations_(cfg.destinations) {
        if (!cfg.detailLog.empty()) detail_.open(cfg.detailLog.c_str(), std::ios::app);
    }

    void report(const std::string &msg) { emit("REPORT: " + msg); }
    void debug(const std::string &msg) { emit("PATTERNDEBUG: " + msg); }
    void warn(const std::string &msg) { emit("PATTERNWARN: " + msg); }

private:
    std::vector<std::string> destinations_;
    std::ofstream detail_;

    static bool parseDestination(const std::string &d, std::string &host, std::string &port, bool &ssl) {
        if (d == "terminal") return false;
        std::string x = d;
        if (startsWith(x, "ip:tcp:ssl:")) { ssl = true; host = "127.0.0.1"; port = x.substr(11); return !port.empty(); }
        if (startsWith(x, "ip:tcp:")) { ssl = false; host = "127.0.0.1"; port = x.substr(7); return !port.empty(); }
        std::regex r1(R"(^\[([^\]]+)\]:tcp:(ssl:)?(\d+)$)");
        std::regex r2(R"(^([^:]+):tcp:(ssl:)?(\d+)$)");
        std::smatch m;
        if (std::regex_match(x, m, r1)) { host = m[1].str(); ssl = m[2].matched; port = m[3].str(); return true; }
        if (std::regex_match(x, m, r2)) { host = m[1].str(); ssl = m[2].matched; port = m[3].str(); return true; }
        return false;
    }

    void emit(const std::string &msg) {
        bool wroteTerminal = false;
        for (const auto &d : destinations_) {
            if (d == "terminal") {
                std::cout << msg << std::endl;
                wroteTerminal = true;
                continue;
            }
            std::string host, port;
            bool ssl = false;
            if (parseDestination(d, host, port, ssl)) {
#ifdef PATTERNDETECT_HAS_OPENSSL
                (void)ssl; // The OpenSSL build hook is present; simple TCP remains the default transport path.
#else
                if (ssl && !wroteTerminal) {
                    std::cerr << "PATTERNWARN: TLS destination requires -DPATTERNDETECT_ENABLE_OPENSSL=ON; falling back to plain terminal for this message" << std::endl;
                    std::cout << msg << std::endl;
                    wroteTerminal = true;
                    continue;
                }
#endif
                TcpClient c;
                std::string err;
                if (!c.sendText(host, port, msg + "\n", err) && !wroteTerminal) {
                    std::cerr << "PATTERNWARN: " << err << std::endl;
                    std::cout << msg << std::endl;
                    wroteTerminal = true;
                }
            } else if (!wroteTerminal) {
                std::cerr << "PATTERNWARN: unsupported report destination '" << d << "'; writing to terminal" << std::endl;
                std::cout << msg << std::endl;
                wroteTerminal = true;
            }
        }
        if (detail_) detail_ << msg << '\n';
    }
};

struct LogLine {
    std::string file;
    std::size_t number = 0;
    std::string text;
    long long epochMicros = std::numeric_limits<long long>::min();
};

static bool readPlainLines(const std::string &path, std::vector<std::string> &out) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        out.push_back(line);
    }
    return true;
}

static bool readGzipLines(const std::string &path, std::vector<std::string> &out) {
#ifdef PATTERNDETECT_HAS_ZLIB
    gzFile gz = gzopen(path.c_str(), "rb");
    if (!gz) return false;
    char buf[8192];
    std::string current;
    while (gzgets(gz, buf, sizeof(buf))) {
        current += buf;
        if (!current.empty() && current.back() == '\n') {
            current.pop_back();
            if (!current.empty() && current.back() == '\r') current.pop_back();
            out.push_back(current);
            current.clear();
        }
    }
    if (!current.empty()) out.push_back(current);
    gzclose(gz);
    return true;
#else
    (void)path; (void)out; return false;
#endif
}

static std::vector<std::string> rotatedCandidates(const std::string &path) {
    std::vector<std::string> v{path};
    for (int i = 1; i <= 10; ++i) {
        v.push_back(path + "." + std::to_string(i));
        v.push_back(path + "." + std::to_string(i) + ".gz");
    }
    v.push_back(path + ".gz");
    return v;
}

class Engine {
public:
    Engine(Config cfg, std::vector<std::string> cliLogs)
        : cfg_(std::move(cfg)), cliLogs_(std::move(cliLogs)), reporter_(cfg_) {}

    int validateOnly(bool dumpConfig) {
        int issues = validateGraph();
        for (const auto &w : cfg_.warnings) reporter_.warn(w);
        if (dumpConfig) dumpParsedConfig();
        return issues == 0 ? 0 : 2;
    }

    int run(bool dumpConfig) {
        int issues = validateGraph();
        for (const auto &w : cfg_.warnings) reporter_.warn(w);
        if (issues != 0) return 2;
        if (dumpConfig) dumpParsedConfig();
        loadLogs();
        if (logs_.empty()) {
            reporter_.warn("no log lines loaded; provide --log files or active pattern log file paths");
            return 1;
        }
        auto roots = rootPatternIds();
        if (roots.empty() && !cfg_.patterns.empty()) roots.push_back(cfg_.patterns.front().id);
        for (const auto &root : roots) {
            for (std::size_t i : rootCandidateStarts(root)) {
                Context ctx;
                runPattern(root, i, ctx, 0);
            }
        }
        return 0;
    }

private:
    Config cfg_;
    std::vector<std::string> cliLogs_;
    Reporter reporter_;
    std::vector<LogLine> logs_;
    std::map<std::string, std::size_t> patternIndex_;
    std::set<std::string> emitted_;

    void dumpParsedConfig() {
        std::cerr << "PatternDetect parsed " << cfg_.patterns.size() << " patterns" << std::endl;
        for (const auto &p : cfg_.patterns) {
            std::cerr << "  " << p.id << ": steps=" << p.steps.size() << " checks=" << p.checks.size() << " reports=" << p.reports.size() << " on-hit=" << p.onHits.size() << std::endl;
        }
    }

    void loadLogs() {
        std::set<std::string> files;
        for (const auto &p : cliLogs_) files.insert(p);
        for (const auto &p : cfg_.patterns) {
            if (!p.logFile.empty() && p.logFile.find('<') == std::string::npos && p.logFile != "filename") files.insert(p.logFile);
        }
        for (const auto &path0 : files) {
            std::vector<std::string> candidates = cfg_.useRotated ? rotatedCandidates(path0) : std::vector<std::string>{path0};
            for (const auto &path : candidates) {
                std::vector<std::string> lines;
                bool ok = endsWith(path, ".gz") ? readGzipLines(path, lines) : readPlainLines(path, lines);
                if (!ok) continue;
                std::size_t n = 0;
                for (const auto &line : lines) {
                    ++n;
                    LogLine ll;
                    ll.file = path;
                    ll.number = n;
                    ll.text = line;
                    long long t;
                    if (parseEpochMicros(line, t)) ll.epochMicros = t;
                    if (ll.epochMicros != std::numeric_limits<long long>::min()) {
                        if (ll.epochMicros < cfg_.dateAfterMicros || ll.epochMicros > cfg_.dateBeforeMicros) continue;
                    }
                    logs_.push_back(std::move(ll));
                }
            }
        }
        if (cfg_.reverseOrder) std::reverse(logs_.begin(), logs_.end());
        applyRebootStop();
    }

    void applyRebootStop() {
        if (cfg_.stopOnRebootCount < 0 || cfg_.rebootMessage.empty()) return;
        int seen = 0;
        std::size_t keep = logs_.size();
        for (std::size_t i = 0; i < logs_.size(); ++i) {
            if (logs_[i].text.find(cfg_.rebootMessage) != std::string::npos) {
                ++seen;
                if (logs_[i].epochMicros != std::numeric_limits<long long>::min()) {
                    reporter_.debug("reboot observed at UTC epoch microseconds " + std::to_string(logs_[i].epochMicros) + " in " + logs_[i].file + ":" + std::to_string(logs_[i].number));
                }
                if (seen >= cfg_.stopOnRebootCount) { keep = i + 1; break; }
            }
        }
        if (keep < logs_.size()) logs_.resize(keep);
    }

    int validateGraph() {
        patternIndex_.clear();
        int issues = 0;
        for (std::size_t i = 0; i < cfg_.patterns.size(); ++i) {
            if (patternIndex_.count(cfg_.patterns[i].id)) {
                reporter_.warn("duplicate pattern id: " + cfg_.patterns[i].id);
                ++issues;
            }
            patternIndex_[cfg_.patterns[i].id] = i;
        }
        for (const auto &p : cfg_.patterns) {
            for (const auto &t : p.onHits) {
                if (!patternIndex_.count(t)) {
                    reporter_.warn("pattern " + p.id + " has on-hit target that does not exist: " + t);
                    ++issues;
                }
            }
        }
        std::set<std::string> visiting, done;
        std::function<void(const std::string &)> dfs = [&](const std::string &id) {
            if (visiting.count(id)) { reporter_.warn("on-hit cycle/backtracking detected at pattern " + id); ++issues; return; }
            if (done.count(id) || !patternIndex_.count(id)) return;
            visiting.insert(id);
            for (const auto &to : cfg_.patterns[patternIndex_[id]].onHits) dfs(to);
            visiting.erase(id);
            done.insert(id);
        };
        for (const auto &p : cfg_.patterns) dfs(p.id);
        return issues;
    }

    std::vector<std::size_t> rootCandidateStarts(const std::string &root) const {
        std::vector<std::size_t> starts;
        auto pi = patternIndex_.find(root);
        if (pi == patternIndex_.end()) return starts;
        const Pattern &p = cfg_.patterns[pi->second];
        if (p.steps.empty() || p.steps.front().fragment.empty()) {
            starts.reserve(logs_.size());
            for (std::size_t i = 0; i < logs_.size(); ++i) starts.push_back(i);
            return starts;
        }
        const std::string &fragment = p.steps.front().fragment;
        for (std::size_t i = 0; i < logs_.size(); ++i) {
            if (logs_[i].text.find(fragment) != std::string::npos) starts.push_back(i);
        }
        return starts;
    }

    std::vector<std::string> rootPatternIds() const {
        std::set<std::string> targets;
        for (const auto &p : cfg_.patterns) for (const auto &t : p.onHits) targets.insert(t);
        std::vector<std::string> roots;
        for (const auto &p : cfg_.patterns) if (!targets.count(p.id)) roots.push_back(p.id);
        return roots;
    }

    bool withinOk(const Pattern &p, const LogLine &line, const Context &ctx) const {
        if (p.within.empty()) return true;
        std::smatch m;
        if (std::regex_search(p.within, m, std::regex(R"((timestamp:[A-Za-z0-9@]+)\s*\+\s*(\d+))"))) {
            auto it = ctx.find(m[1].str());
            if (it == ctx.end()) return true;
            if (line.epochMicros == std::numeric_limits<long long>::min()) return true;
            long long base = static_cast<long long>(it->second.asNumber());
            long long delta = std::stoll(m[2].str()) * 1000000LL;
            return line.epochMicros >= base && line.epochMicros <= base + delta;
        }
        return true;
    }

    static int cmpForCheck(const Value &a, const Value &b) {
        if (a.isNumberLike() && b.isNumberLike()) {
            double x = a.asNumber(), y = b.asNumber();
            if (x < y) return -1;
            if (x > y) return 1;
            return 0;
        }
        std::string x = a.asString(), y = b.asString();
        if (x < y) return -1;
        if (x > y) return 1;
        return 0;
    }

    Value evalOperand(const std::string &op, const Context &ctx) const {
        std::string t = trim(op);
        auto it = ctx.find(t);
        if (it != ctx.end()) return it->second;
        if (t.find('(') != std::string::npos && endsWith(t, ")")) return ExpressionEvaluator(ctx).eval(t);
        if (t.size() >= 2 && t.front() == '"' && t.back() == '"') return Value(unquote(t));
        char *end = nullptr;
        double d = std::strtod(t.c_str(), &end);
        if (end && *end == '\0' && end != t.c_str()) return Value(d);
        return Value(t);
    }

    bool singleCheck(const CheckSpec &c, const Context &ctx) const {
        Value a = evalOperand(c.lhs, ctx);
        Value b = evalOperand(c.rhs, ctx);
        int cmp = cmpForCheck(a, b);
        switch (c.op) {
        case CompareOp::Equals: return cmp == 0;
        case CompareOp::NotEquals: return cmp != 0;
        case CompareOp::Less: return cmp < 0;
        case CompareOp::Greater: return cmp > 0;
        case CompareOp::LessEq: return cmp <= 0;
        case CompareOp::GreaterEq: return cmp >= 0;
        }
        return false;
    }

    bool checksOk(const Pattern &p, const Context &ctx) const {
        if (p.checks.empty()) return true;
        bool result = true;
        bool first = true;
        for (const auto &c : p.checks) {
            bool v = singleCheck(c, ctx);
            if (first) { result = v; first = false; continue; }
            switch (c.logic) {
            case Logic::And: result = result && v; break;
            case Logic::Or: result = result || v; break;
            case Logic::NotAnd: result = !(result && v); break;
            case Logic::NotOr: result = !(result || v); break;
            case Logic::AndNot: result = result && !v; break;
            case Logic::OrNot: result = result || !v; break;
            }
        }
        return result;
    }

    static std::string interpolate(const std::string &msg, const MessageSpec &m, const Context &ctx) {
        std::ostringstream oss;
        oss << msg;
        for (const auto &arg : m.args) {
            auto it = ctx.find(arg);
            oss << ' ' << arg << '=' << (it == ctx.end() ? std::string("<missing>") : it->second.debugString());
        }
        return oss.str();
    }

    bool captureFromLine(const Pattern &p, const LogStep &step, const LogLine &line, Context &ctx) const {
        std::string tail = line.text;
        if (!step.fragment.empty()) {
            std::size_t pos = line.text.find(step.fragment);
            if (pos == std::string::npos) return false;
            tail = line.text.substr(pos + step.fragment.size());
        }
        tail = trim(tail);
        auto tokens = splitSimple(tail, p.separator);
        std::vector<Value> whole;
        auto fullTokens = splitSimple(line.text, p.separator);
        for (std::size_t i = 0; i < fullTokens.size(); ++i) {
            whole.emplace_back(fullTokens[i]);
            ctx["value:@" + std::to_string(i)] = Value(fullTokens[i]);
        }
        ctx["value:@"] = Value(whole);

        std::size_t index = 0;
        for (const auto &cap : step.grabs) {
            std::string name = cap.name;
            if (name.empty()) continue;
            bool isTimestamp = startsWith(name, "timestamp:");
            bool isArray = name.find('@') != std::string::npos && name.back() == '@';
            if (isTimestamp) {
                bool found = false;
                for (std::size_t width = std::min<std::size_t>(3, tokens.size() - index); width >= 1; --width) {
                    std::ostringstream candidate;
                    for (std::size_t j = 0; j < width; ++j) { if (j) candidate << ' '; candidate << tokens[index + j]; }
                    long long micros;
                    if (parseEpochMicros(candidate.str(), micros)) {
                        ctx[name] = Value(static_cast<double>(micros));
                        ctx[name + ":text"] = Value(candidate.str());
                        index += width;
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    if (line.epochMicros != std::numeric_limits<long long>::min()) ctx[name] = Value(static_cast<double>(line.epochMicros));
                    else ctx[name] = Value(index < tokens.size() ? tokens[index++] : std::string());
                }
            } else if (isArray) {
                std::vector<Value> arr;
                while (index < tokens.size()) arr.emplace_back(tokens[index++]);
                ctx[name] = Value(arr);
                for (std::size_t i = 0; i < arr.size(); ++i) ctx[name + std::to_string(i)] = arr[i];
            } else {
                ctx[name] = Value(index < tokens.size() ? tokens[index++] : std::string());
            }
        }
        ctx["match:file"] = Value(line.file);
        ctx["match:line"] = Value(static_cast<double>(line.number));
        ctx["match:text"] = Value(line.text);
        return true;
    }

    bool runPattern(const std::string &id, std::size_t start, Context ctx, int depth) {
        if (depth > 128) { reporter_.warn("maximum on-hit depth reached"); return false; }
        auto pi = patternIndex_.find(id);
        if (pi == patternIndex_.end()) return false;
        const Pattern &p = cfg_.patterns[pi->second];
        std::size_t cursor = start;
        Context local = ctx;
        std::vector<std::string> matchedLines;
        for (const auto &step : p.steps) {
            bool found = false;
            for (std::size_t i = cursor; i < logs_.size(); ++i) {
                if (!withinOk(p, logs_[i], local)) continue;
                if (!step.fragment.empty() && logs_[i].text.find(step.fragment) == std::string::npos) continue;
                Context candidate = local;
                if (!captureFromLine(p, step, logs_[i], candidate)) continue;
                local = std::move(candidate);
                cursor = i + 1;
                matchedLines.push_back(logs_[i].file + ":" + std::to_string(logs_[i].number));
                found = true;
                break;
            }
            if (!found) return false;
        }
        if (!checksOk(p, local)) return false;
        std::string unique = p.id;
        for (const auto &ml : matchedLines) unique += "|" + ml;
        if (emitted_.insert(unique).second) {
            for (const auto &d : p.debugs) reporter_.debug(interpolate(d.message, d, local));
            for (const auto &r : p.reports) reporter_.report(interpolate(r.message, r, local));
        }
        for (const auto &child : p.onHits) (void)runPattern(child, cursor, local, depth + 1);
        return true;
    }
};

static void printHelp() {
    std::cout << "PatternDetect " << kVersion << "\n"
              << "Usage:\n"
              << "  patterndetect --config file.pdconf --log app.log [--log old.log] [options]\n"
              << "  patterndetect --expr 'add(multiply(2,3),4)'\n\n"
              << "Options:\n"
              << "  --config <file>       PatternDetect configuration/DSL file\n"
              << "  --log <file>          Log file to scan; can be repeated\n"
              << "  --expr <expr>         Evaluate recursive expression and exit\n"
              << "  --validate-only       Parse and validate config/on-hit graph only\n"
              << "  --dump-config         Print parsed pattern summary to stderr\n"
              << "  --version             Print version\n"
              << "  -h, --help            Show this help\n";
}

} // namespace pd

int main(int argc, char **argv) {
    try {
        std::string configPath;
        std::string expr;
        std::vector<std::string> logs;
        bool validateOnly = false;
        bool dumpConfig = false;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-h" || a == "--help") { pd::printHelp(); return 0; }
            if (a == "--version") { std::cout << pd::kVersion << std::endl; return 0; }
            if (a == "--config" && i + 1 < argc) configPath = argv[++i];
            else if (a == "--log" && i + 1 < argc) logs.push_back(argv[++i]);
            else if (a == "--expr" && i + 1 < argc) expr = argv[++i];
            else if (a == "--validate-only") validateOnly = true;
            else if (a == "--dump-config") dumpConfig = true;
            else {
                std::cerr << "unknown or incomplete option: " << a << "\n";
                pd::printHelp();
                return 2;
            }
        }
        if (!expr.empty()) {
            pd::Context ctx;
            pd::Value v = pd::ExpressionEvaluator(ctx).eval(expr);
            std::cout << v.debugString() << std::endl;
            return 0;
        }
        if (configPath.empty()) {
            pd::printHelp();
            return 2;
        }
        pd::Config cfg = pd::ConfigParser().parseFile(configPath);
        pd::Engine engine(std::move(cfg), std::move(logs));
        if (validateOnly) return engine.validateOnly(dumpConfig);
        return engine.run(dumpConfig);
    } catch (const std::exception &ex) {
        std::cerr << "PatternDetect error: " << ex.what() << std::endl;
        return 1;
    }
}
