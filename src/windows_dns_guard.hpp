#pragma once
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

// RAII guard: points all active network adapters' DNS to 127.0.0.1 on setup,
// restores them to DHCP on destruction.
// The proxy must listen on port 53 so Windows DNS queries reach it directly.
class WinDnsGuard {
public:
    WinDnsGuard() = default;
    ~WinDnsGuard() { cleanup(); }

    WinDnsGuard(const WinDnsGuard&) = delete;
    WinDnsGuard& operator=(const WinDnsGuard&) = delete;

    bool setup(uint16_t /*port*/) {
        adapters_ = get_adapter_names();
        if (adapters_.empty()) return false;
        for (const auto& a : adapters_)
            netsh("interface ip set dns \"" + a + "\" static 127.0.0.1");
        active_ = true;
        return true;
    }

    void cleanup() {
        if (!active_) return;
        active_ = false;
        for (const auto& a : adapters_)
            netsh("interface ip set dns \"" + a + "\" dhcp");
    }

private:
    bool active_ = false;
    std::vector<std::string> adapters_;

    static bool netsh(const std::string& args) {
        return std::system(("netsh " + args + " >nul 2>&1").c_str()) == 0;
    }

    static std::vector<std::string> get_adapter_names() {
        std::vector<std::string> names;
        ULONG bufLen = 15000;
        auto buf = std::make_unique<uint8_t[]>(bufLen);
        auto* addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get());

        if (GetAdaptersAddresses(AF_INET, 0, nullptr, addrs, &bufLen) == ERROR_BUFFER_OVERFLOW) {
            buf = std::make_unique<uint8_t[]>(bufLen);
            addrs = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.get());
        }
        if (GetAdaptersAddresses(AF_INET, 0, nullptr, addrs, &bufLen) != NO_ERROR)
            return names;

        for (auto* a = addrs; a; a = a->Next) {
            if (a->OperStatus != IfOperStatusUp) continue;
            if (a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            char name[256]{};
            WideCharToMultiByte(CP_ACP, 0, a->FriendlyName, -1, name, sizeof(name), nullptr, nullptr);
            names.emplace_back(name);
        }
        return names;
    }
};
#endif // _WIN32
