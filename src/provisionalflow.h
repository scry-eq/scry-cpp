#ifndef PROVISIONALFLOW_H
#define PROVISIONALFLOW_H

#include "packetformat.h"

#include <QByteArray>

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace seq::shadow {

struct BufferedApplicationPacket {
    uint64_t order = 0;
    uint8_t stream = 0;
    uint8_t direction = 0;
    bool sourceIsLow = false;
    uint16_t opcode = 0;
    QByteArray payload;
    int64_t timestamp = 0;

    size_t retainedBytes() const
    {
        return sizeof(BufferedApplicationPacket) + size_t(payload.size());
    }
};

struct ProvisionalPacketFlow {
    std::deque<BufferedApplicationPacket> packets;
    size_t bytes = 0;
    uint64_t lastUsed = 0;
    bool complete = true;
};

class ProvisionalPacketStore {
public:
    static constexpr size_t MaxFlows = 16;
    static constexpr size_t MaxPacketsPerFlow = 256;
    static constexpr size_t MaxBytesPerFlow = 4 * 1024 * 1024;
    static constexpr size_t MaxTotalBytes = 16 * 1024 * 1024;

    struct Evicted {
        EQPacketFlowKey key;
        ProvisionalPacketFlow flow;
    };

    struct AppendResult {
        bool stored = false;
        bool flowInvalidated = false;
        std::vector<Evicted> evicted;
    };

    AppendResult append(EQPacketFlowKey key,
                        BufferedApplicationPacket packet)
    {
        AppendResult result;
        if (!key.isValid()) return result;

        auto found = m_flows.find(key);
        if (found == m_flows.end()) {
            if (m_flows.size() >= MaxFlows)
                result.evicted.push_back(evictOldest());
            found = m_flows.emplace(key, ProvisionalPacketFlow{}).first;
        }

        found->second.lastUsed = ++m_clock;
        if (!found->second.complete) return result;

        const size_t charge = packet.retainedBytes();
        if (charge > MaxBytesPerFlow ||
            found->second.packets.size() >= MaxPacketsPerFlow ||
            found->second.bytes + charge > MaxBytesPerFlow) {
            invalidate(found->second);
            result.flowInvalidated = true;
            return result;
        }

        while (m_totalBytes + charge > MaxTotalBytes && m_flows.size() > 1) {
            auto oldest = oldestExcept(key);
            if (oldest == m_flows.end()) break;
            result.evicted.push_back(evict(oldest));
        }
        if (m_totalBytes + charge > MaxTotalBytes) {
            invalidate(found->second);
            result.flowInvalidated = true;
            return result;
        }

        found->second.packets.push_back(std::move(packet));
        found->second.bytes += charge;
        m_totalBytes += charge;
        result.stored = true;
        return result;
    }

    ProvisionalPacketFlow* find(EQPacketFlowKey key)
    {
        auto found = m_flows.find(key);
        return found == m_flows.end() ? nullptr : &found->second;
    }

    const ProvisionalPacketFlow* find(EQPacketFlowKey key) const
    {
        auto found = m_flows.find(key);
        return found == m_flows.end() ? nullptr : &found->second;
    }

    std::optional<ProvisionalPacketFlow> take(EQPacketFlowKey key)
    {
        auto found = m_flows.find(key);
        if (found == m_flows.end()) return std::nullopt;
        ProvisionalPacketFlow flow = std::move(found->second);
        m_totalBytes -= flow.bytes;
        m_flows.erase(found);
        return flow;
    }

    std::vector<Evicted> takeAll()
    {
        std::vector<Evicted> result;
        result.reserve(m_flows.size());
        while (!m_flows.empty()) result.push_back(evict(m_flows.begin()));
        return result;
    }

    size_t size() const { return m_flows.size(); }
    size_t totalBytes() const { return m_totalBytes; }

private:
    using Iterator = std::map<EQPacketFlowKey, ProvisionalPacketFlow>::iterator;

    Iterator oldestExcept(EQPacketFlowKey excluded)
    {
        Iterator oldest = m_flows.end();
        for (auto it = m_flows.begin(); it != m_flows.end(); ++it) {
            if (it->first == excluded) continue;
            if (oldest == m_flows.end() ||
                it->second.lastUsed < oldest->second.lastUsed)
                oldest = it;
        }
        return oldest;
    }

    Evicted evictOldest()
    {
        Iterator oldest = m_flows.begin();
        for (auto it = std::next(m_flows.begin()); it != m_flows.end(); ++it)
            if (it->second.lastUsed < oldest->second.lastUsed) oldest = it;
        return evict(oldest);
    }

    Evicted evict(Iterator it)
    {
        Evicted result{it->first, std::move(it->second)};
        m_totalBytes -= result.flow.bytes;
        m_flows.erase(it);
        return result;
    }

    void invalidate(ProvisionalPacketFlow& flow)
    {
        m_totalBytes -= flow.bytes;
        flow.packets.clear();
        flow.bytes = 0;
        flow.complete = false;
    }

    std::map<EQPacketFlowKey, ProvisionalPacketFlow> m_flows;
    size_t m_totalBytes = 0;
    uint64_t m_clock = 0;
};

} // namespace seq::shadow

#endif
