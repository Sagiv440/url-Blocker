#pragma once
#include <cstdint>
#include <cstdlib>
#include <string>

// RAII guard: installs iptables NAT rules on construction, removes on destruction.
//
// Strategy to avoid redirect loops:
//   Our forwarding sockets use SO_MARK=42. Two ACCEPT rules for mark=42 are
//   inserted first so our own upstream DNS traffic bypasses the redirect.
class IptablesGuard {
public:
    IptablesGuard() = default;
    ~IptablesGuard() { cleanup(); }

    IptablesGuard(const IptablesGuard&) = delete;
    IptablesGuard& operator=(const IptablesGuard&) = delete;

    bool setup(uint16_t port) {
        port_ = port;
        const std::string p = std::to_string(port);

        // Allow our marked forwarding sockets through without redirect
        if (!ipt("-t nat -I OUTPUT 1 -p udp --dport 53 -m mark --mark 42 -j ACCEPT")) return false;
        if (!ipt("-t nat -I OUTPUT 2 -p tcp --dport 53 -m mark --mark 42 -j ACCEPT")) return false;

        // Redirect all other local DNS to our proxy port
        if (!ipt("-t nat -I OUTPUT 3 -p udp --dport 53 -j REDIRECT --to-port " + p)) return false;
        if (!ipt("-t nat -I OUTPUT 4 -p tcp --dport 53 -j REDIRECT --to-port " + p)) return false;

        active_ = true;
        return true;
    }

    void cleanup() {
        if (!active_) return;
        active_ = false;
        const std::string p = std::to_string(port_);
        ipt("-t nat -D OUTPUT -p udp --dport 53 -m mark --mark 42 -j ACCEPT");
        ipt("-t nat -D OUTPUT -p tcp --dport 53 -m mark --mark 42 -j ACCEPT");
        ipt("-t nat -D OUTPUT -p udp --dport 53 -j REDIRECT --to-port " + p);
        ipt("-t nat -D OUTPUT -p tcp --dport 53 -j REDIRECT --to-port " + p);
    }

private:
    bool   active_ = false;
    uint16_t port_ = 0;

    static bool ipt(const std::string& args) {
        return std::system(("iptables " + args + " >/dev/null 2>&1").c_str()) == 0;
    }
};
