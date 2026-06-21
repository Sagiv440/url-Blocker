#pragma once
#include <algorithm>
#include <fstream>
#include <string>
#include <unordered_set>

class Blocklist {
public:
    // Load from file; returns number of entries loaded (0 on failure is OK - empty list).
    size_t load(const std::string& path) {
        std::unordered_set<std::string> new_domains;
        std::ifstream f(path);
        if (!f.is_open()) return 0;

        std::string line;
        while (std::getline(f, line)) {
            // Strip comment
            const auto hash = line.find('#');
            if (hash != std::string::npos) line.resize(hash);

            // Trim whitespace
            const auto start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            const auto end = line.find_last_not_of(" \t\r\n");
            line = line.substr(start, end - start + 1);
            if (line.empty()) continue;

            // Normalize to lowercase, strip trailing dot
            std::transform(line.begin(), line.end(), line.begin(), ::tolower);
            if (line.back() == '.') line.pop_back();

            new_domains.insert(std::move(line));
        }

        domains_ = std::move(new_domains);
        return domains_.size();
    }

    // Returns true if domain or any of its ancestors is in the blocklist.
    bool is_blocked(std::string domain) const {
        std::transform(domain.begin(), domain.end(), domain.begin(), ::tolower);
        if (!domain.empty() && domain.back() == '.') domain.pop_back();

        if (domains_.count(domain)) return true;

        // Check each parent: e.g. "sub.example.com" → "example.com" → "com"
        size_t dot = domain.find('.');
        while (dot != std::string::npos) {
            const std::string parent = domain.substr(dot + 1);
            if (domains_.count(parent)) return true;
            dot = domain.find('.', dot + 1);
        }
        return false;
    }

    size_t size() const { return domains_.size(); }

private:
    std::unordered_set<std::string> domains_;
};
