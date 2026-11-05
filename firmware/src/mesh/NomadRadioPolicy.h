#pragma once
#include <array>
#include <cstdint>

namespace nomad
{
class PacketOrigins
{
    struct Entry {
        uint32_t from = 0, id = 0, seen = 0;
        uint8_t radio = 0;
        bool used = false;
    };
    std::array<Entry, 128> entries{};
    unsigned next = 0;

  public:
    static constexpr uint32_t RETENTION_MS = 600000;
    int find(uint32_t from, uint32_t id, uint32_t now) const
    {
        for (auto &entry : entries)
            if (entry.used && entry.from == from && entry.id == id && uint32_t(now - entry.seen) < RETENTION_MS)
                return entry.radio;
        return -1;
    }
    bool receive(uint32_t from, uint32_t id, uint8_t radio, uint32_t now)
    {
        int prior = find(from, id, now);
        if (prior >= 0)
            return prior == radio;
        entries[next] = {from, id, now, radio, true};
        next = (next + 1) % entries.size();
        return true;
    }
};

class PacketDispatcher
{
    PacketOrigins received, transmitted;

  public:
    int find(uint32_t from, uint32_t id, uint32_t now) const
    {
        const int outgoing = transmitted.find(from, id, now);
        return outgoing >= 0 ? outgoing : received.find(from, id, now);
    }
    bool receive(uint32_t from, uint32_t id, uint8_t radio, uint32_t now)
    {
        const int outgoing = transmitted.find(from, id, now);
        return outgoing >= 0 ? outgoing == radio : received.receive(from, id, radio, now);
    }
    bool sent(uint32_t from, uint32_t id, uint8_t radio, uint32_t now) { return transmitted.receive(from, id, radio, now); }
    bool responseAllowed(uint32_t requestFrom, uint32_t requestId, uint32_t responseFrom, uint32_t responseId, bool toUs,
                         uint32_t now) const
    {
        const int request = transmitted.find(requestFrom, requestId, now);
        return request < 0 ? !toUs : request == find(responseFrom, responseId, now);
    }
};

inline bool fanRequired(uint8_t mode, bool transmitting, bool hasTransmitted, uint32_t elapsed)
{
    return mode == 1 || transmitting || (hasTransmitted && elapsed < 60000);
}
} // namespace nomad
