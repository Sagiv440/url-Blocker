#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct DnsQuery {
    uint16_t id;
    std::string domain;
    uint16_t qtype;
};

// Parse the first question from a DNS query packet.
// Returns nullopt for malformed packets or DNS responses (QR=1).
inline std::optional<DnsQuery> dns_parse_query(const uint8_t* data, size_t len) {
    if (len < 12) return std::nullopt;

    const uint16_t id    = (uint16_t)((data[0] << 8) | data[1]);
    const uint16_t flags = (uint16_t)((data[2] << 8) | data[3]);

    if (flags & 0x8000) return std::nullopt; // QR bit set → response, not query

    size_t pos = 12;
    std::string domain;

    // Decode QNAME (sequence of length-prefixed labels terminated by 0)
    while (pos < len) {
        const uint8_t label_len = data[pos++];
        if (label_len == 0) break;
        if ((label_len & 0xC0) == 0xC0) { pos++; break; } // pointer compression
        if (pos + label_len > len) return std::nullopt;
        if (!domain.empty()) domain += '.';
        domain.append(reinterpret_cast<const char*>(data + pos), label_len);
        pos += label_len;
    }

    if (pos + 2 > len) return std::nullopt;
    const uint16_t qtype = (uint16_t)((data[pos] << 8) | data[pos + 1]);

    return DnsQuery{id, std::move(domain), qtype};
}

// Build a minimal NXDOMAIN response for the given query bytes.
inline std::vector<uint8_t> dns_make_nxdomain(const uint8_t* query, size_t len) {
    if (len < 12) return {};
    std::vector<uint8_t> resp(query, query + len);
    resp[2] = (uint8_t)((query[2] | 0x80) & 0xFB); // QR=1, AA=0
    resp[3] = (uint8_t)((query[3] & 0xF0) | 0x03); // RCODE=3 (NXDOMAIN)
    resp[6] = resp[7] = 0; // ANCOUNT = 0
    resp[8] = resp[9] = 0; // NSCOUNT = 0
    resp[10] = resp[11] = 0; // ARCOUNT = 0
    return resp;
}

inline const char* dns_qtype_str(uint16_t t) {
    switch (t) {
        case 1:   return "A";
        case 2:   return "NS";
        case 5:   return "CNAME";
        case 6:   return "SOA";
        case 12:  return "PTR";
        case 15:  return "MX";
        case 16:  return "TXT";
        case 28:  return "AAAA";
        case 33:  return "SRV";
        case 65:  return "HTTPS";
        case 255: return "ANY";
        default:  return "?";
    }
}
