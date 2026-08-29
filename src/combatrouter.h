#pragma once

#include <QObject>
#include <QString>
#include <cstdint>
#include <cstddef>
#include <map>
#include <optional>

class SpawnShell;
class Spells;

// Parses OP_Action2 packets into structured combat events for the
// websocket layer. Sits at the daemon level (one instance), so the
// id→name and spellId→spellName lookups happen once per packet rather
// than once per connected client.
class CombatRouter : public QObject {
    Q_OBJECT
public:
    struct ActiveCast {
        std::optional<uint32_t> casterId;
        std::optional<uint32_t> targetId;
        uint32_t spellId = 0;
        std::optional<uint32_t> castTimeMs;
        std::optional<int32_t> slot;

        bool operator==(const ActiveCast& other) const
        {
            return casterId == other.casterId && targetId == other.targetId &&
                   spellId == other.spellId && castTimeMs == other.castTimeMs &&
                   slot == other.slot;
        }
    };

    CombatRouter(SpawnShell* spawnShell, Spells* spells,
                 QObject* parent = nullptr);

    void applyDamage(std::optional<uint32_t> sourceId,
                     std::optional<uint32_t> targetId, uint32_t type,
                     int32_t damage, std::optional<uint32_t> spellId);
    void applyCastStarted(std::optional<uint32_t> casterId,
                          std::optional<uint32_t> targetId, uint32_t spellId,
                          std::optional<uint32_t> castTimeMs,
                          std::optional<int32_t> slot);
    void applyCastInterrupted(std::optional<uint32_t> casterId,
                              uint32_t spellId);
    void applyCastResolved(std::optional<uint32_t> casterId,
                           std::optional<uint32_t> spellId);
    std::optional<ActiveCast> activeCast(
        std::optional<uint32_t> casterId) const;
    size_t activeCastCount() const { return m_activeCasts.size(); }

public slots:
    // Wired to OP_Action2 by DaemonApp. Layout matches struct
    // action2Struct in everquest.h:2042.
    void action2(const uint8_t* data, size_t len, uint8_t dir);

    // Wired to eql OP_BeginCast (0x6cbd) by wire_eql.cpp. A spawn started
    // casting; emitted as a transient spawnCast event (NOT a buff insertion).
    void beginCast(const uint8_t* data, size_t len, uint8_t dir);

signals:
    void combatEvent(uint32_t sourceId, const QString& sourceName,
                     uint32_t targetId, const QString& targetName,
                     uint32_t type, int32_t damage,
                     uint32_t spellId, const QString& spellName);

    void spawnCast(uint32_t casterId, const QString& casterName,
                   uint32_t spellId, const QString& spellName,
                   uint32_t castTimeMs);

private:
    void finishCast(std::optional<uint32_t> casterId,
                    std::optional<uint32_t> spellId);

    SpawnShell* m_spawnShell;
    Spells*     m_spells;
    std::map<std::optional<uint32_t>, ActiveCast> m_activeCasts;
};
