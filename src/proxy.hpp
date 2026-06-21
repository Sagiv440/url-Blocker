#pragma once
#include <atomic>
#include <memory>
#include <string>
#include "blocklist.hpp"
#include "stats.hpp"

class DnsProxy {
public:
    DnsProxy(uint16_t port, std::string upstream,
             std::shared_ptr<Blocklist> bl, std::shared_ptr<Stats> st)
        : port_(port), upstream_(std::move(upstream)),
          blocklist_(std::move(bl)), stats_(std::move(st)) {}

    // Blocking: runs the UDP proxy loop until stop() is called.
    void run();
    void stop() { running_ = false; }

private:
    uint16_t port_;
    std::string upstream_;
    std::shared_ptr<Blocklist> blocklist_;
    std::shared_ptr<Stats>     stats_;
    std::atomic<bool> running_{true};

    void handle(int sock, const uint8_t* buf, size_t len,
                struct sockaddr_in& client_addr);

    bool forward(const uint8_t* query, size_t qlen,
                 uint8_t* resp, size_t& rlen,
                 const std::string& host, uint16_t up_port);
};
