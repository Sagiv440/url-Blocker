#include "blocklist.hpp"
#include "iptables.hpp"
#include "proxy.hpp"
#include "stats.hpp"
#include "tui.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

// ─── Global shutdown flag (set by signal handler) ───────────────────────────

static std::atomic<bool> g_running{true};

static void on_signal(int) { g_running = false; }

// ─── UI layout constants ─────────────────────────────────────────────────────

// Rows reserved at the top and bottom; everything else is the query log.
static constexpr int TOP_ROWS    = 5;  // title + status + stats + sep + col-header
static constexpr int BOTTOM_ROWS = 12; // sep + top-table (10 rows) + controls

// ─── Helpers ─────────────────────────────────────────────────────────────────

static std::string trunc(const std::string& s, int max) {
    if (max <= 3) return s.substr(0, max);
    if (static_cast<int>(s.size()) > max)
        return s.substr(0, max - 3) + "...";
    return s;
}

static std::string fmt_count(uint64_t n) {
    // Insert thousands separators
    std::string s = std::to_string(n);
    int i = static_cast<int>(s.size()) - 3;
    while (i > 0) { s.insert(i, ","); i -= 3; }
    return s;
}

static std::string time_str(const std::chrono::system_clock::time_point& tp) {
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    char buf[16];
    strftime(buf, sizeof(buf), "%H:%M:%S", localtime(&t));
    return buf;
}

// ─── TUI rendering ───────────────────────────────────────────────────────────

static void draw(Tui& tui, const StatsSnapshot& snap,
                 size_t blocklist_sz, const std::string& upstream,
                 uint16_t port, const std::string& blocklist_path) {
    int rows, cols;
    tui.get_size(rows, cols);

    tui.begin_frame();

    // ── Row 1: title bar ──────────────────────────────────────────────────
    {
        tui.move(1, 1);
        tui.emit(ansi::BOLD); tui.emit(ansi::REVERSE); tui.emit(ansi::BG_BLUE);
        const std::string title = " WebBlock  Network Monitor & DNS Firewall ";
        const int pad = (cols - static_cast<int>(title.size())) / 2;
        if (pad > 0) tui.emit(std::string(pad, ' '));
        tui.emit(title);
        tui.clear_eol();
        tui.emit(ansi::RESET);
    }

    // ── Row 2: status ────────────────────────────────────────────────────
    {
        tui.move(2, 1);
        tui.emit(ansi::DIM);
        tui.emitf(" Upstream: %s  Port: %u  Blocklist: %s (%zu domains)",
                  upstream.c_str(), port,
                  blocklist_path.c_str(), blocklist_sz);
        tui.clear_eol();
        tui.emit(ansi::RESET);
    }

    // ── Row 3: stats ─────────────────────────────────────────────────────
    {
        tui.move(3, 1);
        const double pct = snap.total > 0
            ? 100.0 * snap.blocked / snap.total : 0.0;
        tui.emit(ansi::BOLD); tui.emit(ansi::BYELLOW);
        tui.emitf(" Queries: %-10s", fmt_count(snap.total).c_str());
        tui.emit(ansi::RESET); tui.emit(ansi::BRED); tui.emit(ansi::BOLD);
        tui.emitf("Blocked: %-10s(%.1f%%)", fmt_count(snap.blocked).c_str(), pct);
        tui.emit(ansi::RESET); tui.emit(ansi::BCYAN); tui.emit(ansi::BOLD);
        tui.emitf("  Unique: %zu", snap.top_all.size());
        tui.clear_eol();
        tui.emit(ansi::RESET);
    }

    // ── Row 4: separator + column headers ────────────────────────────────
    {
        tui.hline(4, cols, '-', ansi::DIM);
        tui.move(4, 2);
        tui.emit(ansi::RESET); tui.emit(ansi::CYAN); tui.emit(ansi::BOLD);
        tui.emit(" Recent DNS Activity ");
        tui.emit(ansi::RESET);
    }
    {
        tui.move(5, 1);
        tui.emit(ansi::BOLD);
        tui.emitf(" %-10s %-6s %-*s %s",
                  "Time", "Type",
                  std::min(cols - 28, 48), "Domain",
                  "Action");
        tui.clear_eol();
        tui.emit(ansi::RESET);
    }

    // ── Query log rows ────────────────────────────────────────────────────
    {
        const int log_start = TOP_ROWS + 1;      // first log row (1-based)
        const int log_end   = rows - BOTTOM_ROWS; // last log row (inclusive)
        const int max_dom   = std::min(cols - 28, 48);

        int r = log_start;
        for (const auto& q : snap.recent) {
            if (r > log_end) break;

            tui.move(r, 1);
            if (q.blocked)
                tui.emit(ansi::BRED);
            else
                tui.emit(ansi::BGREEN);

            tui.emitf(" %-10s %-6s %-*s ",
                      time_str(q.time).c_str(),
                      q.qtype.c_str(),
                      max_dom, trunc(q.domain, max_dom).c_str());

            if (q.blocked) {
                tui.emit(ansi::BOLD);
                tui.emit("BLOCKED");
                tui.emit(ansi::RESET); tui.emit(ansi::BRED);
                tui.emitf("  (from %s)", q.client.c_str());
            } else {
                tui.emit(ansi::DIM);
                tui.emit("allow");
            }
            tui.clear_eol();
            tui.emit(ansi::RESET);
            r++;
        }
        // Blank out unused log rows
        for (; r <= log_end; r++) {
            tui.move(r, 1);
            tui.clear_eol();
        }
    }

    // ── Separator + top-table header ─────────────────────────────────────
    {
        const int sep_row = rows - BOTTOM_ROWS + 1;
        tui.hline(sep_row, cols, '-', ansi::DIM);
        tui.move(sep_row, 2);
        tui.emit(ansi::RESET); tui.emit(ansi::BOLD); tui.emit(ansi::YELLOW);
        tui.emit(" Top Queries ");
        tui.emit(ansi::RESET); tui.emit(ansi::DIM);

        // Divider in the middle
        tui.move(sep_row, cols / 2 + 1);
        tui.emit(ansi::RESET); tui.emit(ansi::BOLD); tui.emit(ansi::BRED);
        tui.emit(" Top Blocked ");
        tui.emit(ansi::RESET);
    }

    // ── Top-queries table (left) and top-blocked table (right) ───────────
    {
        const int table_start = rows - BOTTOM_ROWS + 2;
        const int table_end   = rows - 2; // leave room for controls row
        const int half        = cols / 2 - 1;
        const int max_dom     = std::max(10, half - 14);

        for (int i = 0; i < (table_end - table_start + 1); i++) {
            const int r = table_start + i;
            tui.move(r, 1);

            // Left: top all queries
            if (i < static_cast<int>(snap.top_all.size())) {
                tui.emit(ansi::BGREEN);
                tui.emitf(" %2d. %-*s %6llu",
                           i + 1, max_dom,
                           trunc(snap.top_all[i].first, max_dom).c_str(),
                           (unsigned long long)snap.top_all[i].second);
                tui.emit(ansi::RESET);
            } else {
                tui.emit(std::string(half, ' '));
            }

            // Right: top blocked queries
            tui.move(r, half + 2);
            if (i < static_cast<int>(snap.top_blocked.size())) {
                tui.emit(ansi::BRED);
                tui.emitf(" %2d. %-*s %6llu",
                           i + 1, max_dom,
                           trunc(snap.top_blocked[i].first, max_dom).c_str(),
                           (unsigned long long)snap.top_blocked[i].second);
                tui.emit(ansi::RESET);
            }
            tui.clear_eol();
        }
    }

    // ── Bottom separator + controls ───────────────────────────────────────
    {
        tui.hline(rows - 1, cols, '-', ansi::DIM);
        tui.move(rows, 1);
        tui.emit(ansi::RESET); tui.emit(ansi::BOLD);
        tui.emit(" [q] Quit");
        tui.emit(ansi::RESET);
        tui.emit("  [c] Clear stats");
        tui.emit("  [r] Reload blocklist");
        tui.emit("  [↑↓] Scroll");
        tui.clear_eol();
    }

    tui.end_frame();
}

// ─── Entry point ─────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    fprintf(stdout,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  -b FILE    Blocklist file           (default: blocklist.txt)\n"
        "  -u ADDR    Upstream DNS server      (default: 8.8.8.8:53)\n"
        "  -p PORT    Local proxy port         (default: 15353)\n"
        "  -h         Show this help\n"
        "\n"
        "Must be run as root (sudo ./webblock).\n"
        "\n"
        "How it works:\n"
        "  1. Installs iptables NAT rules to redirect all local DNS traffic\n"
        "     (UDP/TCP port 53) to the local proxy on PORT.\n"
        "  2. The proxy checks each query against the blocklist.\n"
        "     Blocked domains get an NXDOMAIN response; others are forwarded\n"
        "     to the upstream DNS server.\n"
        "  3. iptables rules are automatically removed on exit.\n",
        prog);
}

int main(int argc, char* argv[]) {
    std::string blocklist_path = "blocklist.txt";
    std::string upstream       = "8.8.8.8:53";
    uint16_t    port           = 15353;

    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i];
        if ((arg == "-b") && i + 1 < argc) { blocklist_path = argv[++i]; }
        else if ((arg == "-u") && i + 1 < argc) { upstream = argv[++i]; }
        else if ((arg == "-p") && i + 1 < argc) {
            try { port = static_cast<uint16_t>(std::stoi(argv[++i])); }
            catch (...) { fprintf(stderr, "Invalid port\n"); return 1; }
        }
        else if (arg == "-h" || arg == "--help") { print_usage(argv[0]); return 0; }
        else { fprintf(stderr, "Unknown option: %s\n", arg.c_str()); return 1; }
    }

    // Root check
    if (geteuid() != 0) {
        fprintf(stderr, "Error: webblock requires root privileges.\n"
                        "       Run with: sudo %s\n", argv[0]);
        return 1;
    }

    // Load blocklist
    auto blocklist = std::make_shared<Blocklist>();
    const size_t bl_sz = blocklist->load(blocklist_path);
    if (bl_sz == 0) {
        fprintf(stderr, "Warning: blocklist '%s' is empty or not found.\n",
                blocklist_path.c_str());
    }

    // Shared stats
    auto stats = std::make_shared<Stats>();

    // Install iptables rules (RAII – removed on destruction)
    IptablesGuard ipt;
    if (!ipt.setup(port)) {
        fprintf(stderr,
            "Error: failed to install iptables rules.\n"
            "       Make sure iptables is installed: apt install iptables\n");
        return 1;
    }

    // Signal handlers – flip g_running so the main loop exits cleanly
    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    // Launch DNS proxy on a background thread
    DnsProxy proxy(port, upstream, blocklist, stats);
    std::thread proxy_thread([&proxy] { proxy.run(); });

    // ── TUI loop ──────────────────────────────────────────────────────────
    Tui tui;
    tui.init();

    using Clock = std::chrono::steady_clock;
    auto last_draw = Clock::now() - std::chrono::seconds(1);

    while (g_running) {
        // Handle keyboard input (non-blocking)
        const int ch = tui.getch();
        switch (ch) {
            case 'q': case 'Q': case 3: // Ctrl-C
                g_running = false;
                break;
            case 'c': case 'C':
                stats->clear();
                break;
            case 'r': case 'R': {
                const size_t n = blocklist->load(blocklist_path);
                (void)n; // reload success visible through blocklist size update
                break;
            }
            default: break;
        }

        // Redraw at ~5 Hz
        const auto now = Clock::now();
        if (now - last_draw >= std::chrono::milliseconds(200)) {
            const auto snap = stats->snapshot(10);
            draw(tui, snap, blocklist->size(), upstream, port, blocklist_path);
            last_draw = now;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    tui.fini();

    // Graceful shutdown
    proxy.stop();
    proxy_thread.join();
    // ipt destructor removes iptables rules automatically

    fprintf(stderr, "\nwebblock stopped. iptables rules removed.\n");
    return 0;
}
