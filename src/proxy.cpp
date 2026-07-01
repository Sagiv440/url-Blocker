#include "proxy.hpp"
#include "dns.hpp"

#ifdef __linux__
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#elif _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>

#endif

#ifdef _WIN32
static inline bool sock_invalid(socket_t s) { return s == INVALID_SOCKET; }
#else
static inline bool sock_invalid(socket_t s) { return s < 0; }
#endif

void DnsProxy::run()
{
    const socket_t sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_invalid(sock))
        return;

#ifdef __linux__
    // Mark all sockets created in this thread so our own forwarded DNS queries
    // are NOT intercepted again by the iptables REDIRECT rule (mark=42 → ACCEPT).
    const int mark = 42;
    setsockopt(sock, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
#endif

    const int on = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port_);

    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
#ifdef __linux__
        close(sock);
#else
        closesocket(sock);
#endif
        return;
    }

    // (upstream parsing happens per-packet in handle() to keep run() minimal)

    uint8_t buf[4096];

    while (running_)
    {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{0, 100'000}; // 100 ms poll interval

        if (select(sock + 1, &fds, nullptr, nullptr, &tv) <= 0)
            continue;

        struct sockaddr_in client{};
        socklen_t cl_len = sizeof(client);

#ifdef __linux__
        ssize_t n;
#else
        int n;
#endif

        n = recvfrom(sock, (char *)buf, sizeof(buf), 0, reinterpret_cast<sockaddr *>(&client), &cl_len);
        if (n <= 0)
            continue;

        handle(sock, buf, static_cast<size_t>(n), client);
        // Note: handle() is called synchronously. For a single workstation
        // the query rate is low enough that this is fine. Each forward() call
        // has a 3-second timeout so worst case latency is bounded.
    }
#ifdef __linux__
    close(sock);
#elif _WIN32
    closesocket(sock);
#endif
}

void DnsProxy::handle(socket_t sock, const uint8_t *data, size_t len,
                      struct sockaddr_in &client)
{
    char client_ip[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &client.sin_addr, client_ip, sizeof(client_ip));

    // Parse upstream once (already done in run(), but reuse here via members)
    std::string up_host = upstream_;
    uint16_t up_port = 53;
    const auto colon = upstream_.rfind(':');
    if (colon != std::string::npos)
    {
        up_host = upstream_.substr(0, colon);
        try
        {
            up_port = static_cast<uint16_t>(std::stoi(upstream_.substr(colon + 1)));
        }
        catch (...)
        {
        }
    }

    const auto query = dns_parse_query(data, len);
    if (!query)
    {
        // Forward even unparseable packets (e.g. DNS over TCP-style length-prefixed)
        uint8_t resp[4096];
        size_t rlen = sizeof(resp);
        if (forward(data, len, resp, rlen, up_host, up_port))
            sendto(sock, (char *)resp, rlen, 0, reinterpret_cast<sockaddr *>(&client), sizeof(client));
        return;
    }

    const bool blocked = blocklist_->is_blocked(query->domain);
    stats_->record(query->domain, dns_qtype_str(query->qtype), blocked, client_ip);

    if (blocked)
    {
        const auto nxd = dns_make_nxdomain(data, len);
        sendto(sock, (char *)nxd.data(), nxd.size(), 0,
               reinterpret_cast<sockaddr *>(&client), sizeof(client));
    }
    else
    {
        uint8_t resp[4096];
        size_t rlen = sizeof(resp);
        if (forward(data, len, resp, rlen, up_host, up_port))
            sendto(sock, (char *)resp, rlen, 0, reinterpret_cast<sockaddr *>(&client), sizeof(client));
    }
}

bool DnsProxy::forward(const uint8_t *query, size_t qlen,
                       uint8_t *resp, size_t &rlen,
                       const std::string &host, uint16_t up_port)
{
    // Resolve upstream address
    struct sockaddr_in up{};
    up.sin_family = AF_INET;
    up.sin_port = htons(up_port);

    if (inet_pton(AF_INET, host.c_str(), &up.sin_addr) != 1)
    {
        struct addrinfo hints{}, *ai;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(host.c_str(), nullptr, &hints, &ai) != 0)
            return false;
        up.sin_addr = reinterpret_cast<sockaddr_in *>(ai->ai_addr)->sin_addr;
        freeaddrinfo(ai);
    }

    const socket_t fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_invalid(fd))
        return false;
#ifdef __linux__
    // Mark so iptables ACCEPT rule lets this through without redirect
    const int mark = 42;
    setsockopt(fd, SOL_SOCKET, SO_MARK, &mark, sizeof(mark));
#endif

// 3-second receive timeout
#ifdef __linux__
    timeval tv{3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#elif _WIN32
    DWORD tv = 3000; // milliseconds;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (char *)&tv, sizeof(tv));
#endif

    bool ok = false;
    if (sendto(fd, (char *)query, qlen, 0, reinterpret_cast<sockaddr *>(&up), sizeof(up)) > 0)
    {
#ifdef __linux__
        ssize_t n;
#else
        int n;
#endif
        n = recv(fd, (char *)resp, rlen, 0);
        if (n > 0)
        {
            rlen = static_cast<size_t>(n);
            ok = true;
        }
    }

#ifdef __linux__
    close(fd);
#else
    closesocket(fd);
#endif
    return ok;
}
