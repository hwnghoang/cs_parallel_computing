// Small command-line helpers shared by test_barrier and bench_barrier.
#pragma once

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

inline int hardware_threads() {
    return static_cast<int>(std::max(1u, std::thread::hardware_concurrency()));
}

[[noreturn]] inline void usage_error(const char* usage, const char* prog) {
    std::fprintf(stderr, "usage: %s %s\n", prog, usage);
    std::exit(1);
}

// Parses a positive number; exits with the usage message otherwise.
inline long parse_positive(const char* s, const char* usage, const char* prog) {
    char* end = nullptr;
    long v = std::strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 1) usage_error(usage, prog);
    return v;
}

// Parses "2,4,8" into {2, 4, 8}.
inline std::vector<int> parse_list(const char* s, const char* usage, const char* prog) {
    std::vector<int> out;
    std::string str(s);
    std::size_t pos = 0;
    while (pos <= str.size()) {
        std::size_t comma = std::min(str.find(',', pos), str.size());
        out.push_back(static_cast<int>(parse_positive(str.substr(pos, comma - pos).c_str(), usage, prog)));
        pos = comma + 1;
    }
    return out;
}

inline void sort_unique(std::vector<int>& v) {
    std::sort(v.begin(), v.end());
    v.erase(std::unique(v.begin(), v.end()), v.end());
}
