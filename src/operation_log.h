#pragma once

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <process.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pd {

// The five *logical* fields of each record are preserved by the script format.
// The human format wraps long logical lines (with four-space continuation prefixes).
class OperationLog {
public:
    static constexpr const char *HUMAN_SEPARATOR = "  )))))  ";
    static constexpr const char *SCRIPT_SEPARATOR = "  -----  ";
    enum class Mode { Both, HumanOnly, ScriptOnly };

    OperationLog(const std::string &prefix, Mode mode, const std::string &filterHost = "")
        : pid_(processId()), host_(hostnameAndAddress()),
          filterHost_(filterHost.empty() ? host_ : filterHost) {
        std::string base = prefix.empty() ? "patterndetect.operations." + std::to_string(pid_) : prefix;
        if (mode != Mode::ScriptOnly) {
            human_.open(base + ".human.log", std::ios::out | std::ios::binary | std::ios::trunc);
            if (!human_) throw std::runtime_error("cannot create human operation log: " + base + ".human.log");
        }
        if (mode != Mode::HumanOnly) {
            script_.open(base + ".script.log", std::ios::out | std::ios::binary | std::ios::trunc);
            if (!script_) throw std::runtime_error("cannot create script operation log: " + base + ".script.log");
        }
    }

    static unsigned long currentProcessId() { return processId(); }

    // parent = 0 denotes a main-thread/root entry; all child records include
    // an event id and parent id in the process-list metadata for live viewers.
    std::uint64_t write(const std::string &severity, std::uint64_t parent,
                        const std::vector<std::string> &trail, const std::string &message,
                        std::size_t depth) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::uint64_t id = ++nextId_;
        const std::string stamp = utcNow();
        const std::string filterStamp = utcNow();
        std::ostringstream thread;
        thread << std::this_thread::get_id();
        const std::string first = padDepth(depth) + HUMAN_SEPARATOR + escape(host_) + HUMAN_SEPARATOR + stamp
            + HUMAN_SEPARATOR + escape(filterHost_) + HUMAN_SEPARATOR + filterStamp;
        const std::string second = escape(severity);
        const std::string third = escape("PatternDetect:" + std::to_string(pid_) + ":" + thread.str())
            + HUMAN_SEPARATOR + "event=" + std::to_string(id) + ";parent=" + std::to_string(parent);
        std::string fourth;
        for (const auto &item : trail) {
            if (!fourth.empty()) fourth += HUMAN_SEPARATOR;
            fourth += escape(item);
        }
        if (fourth.empty()) fourth = "operation";
        const std::string fifth = escape(message);
        const std::vector<std::string> fields{first, second, third, fourth, fifth};
        if (human_) {
            for (const auto &field : fields) human_ << wrap(field) << '\n';
            human_ << "\n\n";
            human_.flush();
        }
        if (script_) {
            for (std::size_t i = 0; i < fields.size(); ++i) {
                if (i) script_ << SCRIPT_SEPARATOR;
                script_ << fields[i];
            }
            script_ << '\n';
            script_.flush();
        }
        if ((human_.is_open() && !human_) || (script_.is_open() && !script_))
            throw std::runtime_error("failed to write operation log record");
        return id;
    }

    static std::string escape(const std::string &raw) {
        if (isBinary(raw)) return "base64:" + base64(raw);
        std::string out;
        out.reserve(raw.size());
        // Disambiguate a literal textual "base64:" prefix from encoded binary.
        if (raw.compare(0, 7, "base64:") == 0) out = "\\x62";
        const std::size_t first = raw.compare(0, 7, "base64:") == 0 ? 1 : 0;
        for (std::size_t i = first; i < raw.size();) {
            if (raw.compare(i, std::char_traits<char>::length(HUMAN_SEPARATOR), HUMAN_SEPARATOR) == 0 ||
                raw.compare(i, std::char_traits<char>::length(SCRIPT_SEPARATOR), SCRIPT_SEPARATOR) == 0) {
                // Escape *every byte* in either reserved delimiter, including spaces.
                const std::size_t len = std::char_traits<char>::length(HUMAN_SEPARATOR);
                for (std::size_t j = 0; j < len; ++j) {
                    const unsigned char ch = static_cast<unsigned char>(raw[i + j]);
                    const char *hex = "0123456789ABCDEF";
                    out += "\\x";
                    out.push_back(hex[ch >> 4]); out.push_back(hex[ch & 15]);
                }
                i += len;
            } else {
                const char c = raw[i++];
                switch (c) {
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(c);
                }
            }
        }
        return out;
    }

private:
    unsigned long pid_;
    std::string host_, filterHost_;
    std::ofstream human_, script_;
    std::mutex mutex_;
    std::uint64_t nextId_ = 0;

    static unsigned long processId() {
#ifdef _WIN32
        return static_cast<unsigned long>(_getpid());
#else
        return static_cast<unsigned long>(getpid());
#endif
    }

    static std::string hostnameAndAddress() {
#ifdef _WIN32
        // Winsock must be initialized before gethostname/getaddrinfo on Windows.
        static const bool ready = [] { WSADATA data{}; return WSAStartup(MAKEWORD(2,2), &data) == 0; }();
        if (!ready) return "unknown@unknown";
#endif
        char name[256]{};
        if (gethostname(name, sizeof(name)-1) != 0) return "unknown@unknown";
        struct addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        struct addrinfo *res = nullptr;
        std::string ipv4, ipv6;
        if (getaddrinfo(name, nullptr, &hints, &res) == 0) {
            for (auto *p = res; p; p = p->ai_next) {
                char buf[NI_MAXHOST]{};
                if (getnameinfo(p->ai_addr, static_cast<int>(p->ai_addrlen), buf, sizeof(buf),
                                nullptr, 0, NI_NUMERICHOST) != 0) continue;
                if (p->ai_family == AF_INET && ipv4.empty()) ipv4 = buf;
                else if (p->ai_family == AF_INET6 && ipv6.empty()) ipv6 = buf;
            }
            freeaddrinfo(res);
        }
        return std::string(name) + "@" + (!ipv4.empty() ? ipv4 : (!ipv6.empty() ? "[" + ipv6 + "]" : "unknown"));
    }

    static std::string utcNow() {
        auto now = std::chrono::system_clock::now();
        auto secs = std::chrono::time_point_cast<std::chrono::seconds>(now);
        const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now - secs).count();
        const std::time_t tt = std::chrono::system_clock::to_time_t(now);
        std::tm tm{};
#ifdef _WIN32
        gmtime_s(&tm, &tt);
#else
        gmtime_r(&tt, &tm);
#endif
        std::ostringstream os;
        os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(6) << micros << 'Z';
        return os.str();
    }

    static std::string padDepth(std::size_t depth) {
        std::ostringstream os;
        os << std::setw(16) << std::setfill('0') << depth;
        return os.str();
    }

    static bool isBinary(const std::string &s) {
        for (unsigned char c : s)
            if (c == 0 || (c < 32 && c != '\t' && c != '\r' && c != '\n') || c == 127) return true;
        return false;
    }

    static std::string base64(const std::string &s) {
        static const char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        unsigned int val = 0; int bits = -6;
        for (unsigned char c : s) {
            val = (val << 8) | c; bits += 8;
            while (bits >= 0) { out.push_back(tbl[(val >> bits) & 63]); bits -= 6; }
        }
        if (bits > -6) out.push_back(tbl[((val << 8) >> (bits + 8)) & 63]);
        while (out.size() % 4) out.push_back('=');
        return out;
    }

    // Wrap without splitting UTF-8 bytes or an escaped \\xHH sequence; 128 columns
    // maximum, including each four-space continuation prefix.
    static std::string wrap(const std::string &s) {
        const std::size_t limit = 124;
        std::ostringstream out;
        std::size_t i = 0;
        bool first = true;
        while (i < s.size() || first) {
            if (!first) out << '\n';
            out << "    ";
            first = false;
            std::size_t start = i, pos = i, lastWhitespace = std::string::npos;
            std::size_t units = 0;
            while (pos < s.size() && units < limit) {
                const unsigned char c = static_cast<unsigned char>(s[pos]);
                std::size_t step = 1;
                if (s[pos] == '\\' && pos + 3 < s.size() && s[pos+1] == 'x' &&
                    std::isxdigit(static_cast<unsigned char>(s[pos+2])) &&
                    std::isxdigit(static_cast<unsigned char>(s[pos+3]))) step = 4;
                else if (c >= 0xF0 && c <= 0xF4) step = 4;
                else if (c >= 0xE0 && c <= 0xEF) step = 3;
                else if (c >= 0xC2 && c <= 0xDF) step = 2;
                if (pos + step > s.size()) step = 1;
                if (step == 1 && c == ' ') lastWhitespace = pos;
                pos += step; ++units;
            }
            if (pos < s.size() && lastWhitespace != std::string::npos && lastWhitespace > start) {
                pos = lastWhitespace + 1; // preserve spaces for lossless join
            }
            out << s.substr(start, pos - start);
            i = pos;
        }
        return out.str();
    }
};
} // namespace pd
