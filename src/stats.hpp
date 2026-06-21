#pragma once
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct QueryEntry {
    std::chrono::system_clock::time_point time;
    std::string domain;
    std::string qtype;
    std::string client;
    bool blocked;
};

struct StatsSnapshot {
    uint64_t total;
    uint64_t blocked;
    std::deque<QueryEntry> recent;                         // newest first
    std::vector<std::pair<std::string, uint64_t>> top_all;     // by query count
    std::vector<std::pair<std::string, uint64_t>> top_blocked; // by block count
};

class Stats {
public:
    std::atomic<uint64_t> total{0};
    std::atomic<uint64_t> blocked{0};

    void record(const std::string& domain, const std::string& qtype,
                bool is_blocked, const std::string& client) {
        ++total;
        if (is_blocked) ++blocked;

        std::lock_guard<std::mutex> lk(mu_);
        ++counts_[domain];
        if (is_blocked) ++blocked_counts_[domain];

        recent_.push_front(QueryEntry{
            std::chrono::system_clock::now(), domain, qtype, client, is_blocked});
        if (recent_.size() > 200) recent_.pop_back();
    }

    StatsSnapshot snapshot(size_t top_n = 10) const {
        std::lock_guard<std::mutex> lk(mu_);
        StatsSnapshot s;
        s.total   = total.load();
        s.blocked = blocked.load();
        s.recent  = recent_;
        s.top_all     = top_sorted(counts_, top_n);
        s.top_blocked = top_sorted(blocked_counts_, top_n);
        return s;
    }

    void clear() {
        total = 0;
        blocked = 0;
        std::lock_guard<std::mutex> lk(mu_);
        recent_.clear();
        counts_.clear();
        blocked_counts_.clear();
    }

private:
    mutable std::mutex mu_;
    std::deque<QueryEntry> recent_;
    std::map<std::string, uint64_t> counts_;
    std::map<std::string, uint64_t> blocked_counts_;

    static std::vector<std::pair<std::string, uint64_t>>
    top_sorted(const std::map<std::string, uint64_t>& m, size_t n) {
        std::vector<std::pair<std::string, uint64_t>> v(m.begin(), m.end());
        std::sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        if (v.size() > n) v.resize(n);
        return v;
    }
};
