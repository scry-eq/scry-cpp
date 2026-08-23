#include "combatrouter.h"
#include "seq-bridge-cxx/lib.h"

#include "everquest.h"
#include "spawn.h"
#include "spawnshell.h"
#include "spells.h"

CombatRouter::CombatRouter(SpawnShell* spawnShell, Spells* spells,
                           QObject* parent)
    : QObject(parent)
    , m_spawnShell(spawnShell)
    , m_spells(spells)
{
}

// Returns the human-readable form of a spawn's name. For NPCs that's
// the cleanedName (drops the trailing instance digits and unescapes
// underscores into spaces — "a_pyre_beetle25" → "a pyre beetle"); for
// PCs it's the name verbatim. We deliberately skip the article-move
// done by transformedName() ("a pyre beetle" → "pyre beetle, a") since
// combat log lines read more naturally with the article in front.
static QString lookupSpawnName(SpawnShell* shell, uint16_t id)
{
    if (!shell || id == 0) return QString();
    const Item* it = shell->findID(tSpawn, id);
    if (!it) it = shell->findID(tPlayer, id);
    if (!it) return QString();
    if (const Spawn* sp = dynamic_cast<const Spawn*>(it)) {
        return sp->cleanedName();
    }
    return it->name();
}

void CombatRouter::applyDamage(std::optional<uint32_t> sourceId,
                               std::optional<uint32_t> targetId,
                               uint32_t type, int32_t damage,
                               std::optional<uint32_t> spellId)
{
    const auto spawnName = [this](std::optional<uint32_t> id) {
        return id && *id <= UINT16_MAX
            ? lookupSpawnName(m_spawnShell, uint16_t(*id)) : QString();
    };
    QString spellName;
    if (spellId && m_spells) {
        if (const Spell* spell = m_spells->spell(*spellId))
            spellName = spell->name();
    }

    // seq.v1 predates optional ids. Preserve absence until this signal, then
    // use its documented compatibility zero at the protobuf edge.
    emit combatEvent(sourceId.value_or(0), spawnName(sourceId),
                     targetId.value_or(0), spawnName(targetId), type, damage,
                     spellId.value_or(0), spellName);
}

void CombatRouter::applyCastStarted(std::optional<uint32_t> casterId,
                                    uint32_t spellId,
                                    std::optional<uint32_t> castTimeMs)
{
    if (!castTimeMs) return;
    QString casterName;
    if (casterId && *casterId <= UINT16_MAX)
        casterName = lookupSpawnName(m_spawnShell, uint16_t(*casterId));
    QString spellName;
    if (m_spells) {
        if (const Spell* spell = m_spells->spell(spellId))
            spellName = spell->name();
    }
    emit spawnCast(casterId.value_or(0), casterName, spellId, spellName,
                   *castTimeMs);
}

void CombatRouter::action2(const uint8_t* data, size_t len, uint8_t /*dir*/)
{
    if (!data || len < sizeof(action2Struct)) return;
    auto out = seq::rust::decode_action2(
        rust::Slice<const uint8_t>{data, sizeof(action2Struct)});
    if (!out.ok) return;

    QString sourceName = lookupSpawnName(m_spawnShell, out.source);
    QString targetName = lookupSpawnName(m_spawnShell, out.target);

    QString spellName;
    if (out.spell > 0 && m_spells) {
        if (const Spell* sp = m_spells->spell(static_cast<uint16_t>(out.spell))) {
            spellName = sp->name();
        }
    }

    emit combatEvent(out.source, sourceName,
                     out.target, targetName,
                     static_cast<uint32_t>(out.kind),
                     out.damage,
                     static_cast<uint32_t>(out.spell), spellName);
}

void CombatRouter::beginCast(const uint8_t* data, size_t len, uint8_t /*dir*/)
{
    if (!data || len < 8) return;
    auto out = seq::rust::decode_begin_cast(
        rust::Slice<const uint8_t>{data, len});
    if (!out.ok) return;

    QString casterName = lookupSpawnName(m_spawnShell,
                                         static_cast<uint16_t>(out.caster_id));

    // eql spell ids exceed uint16 (e.g. 74023) — spell() takes uint32_t, so
    // pass the id verbatim (decode_begin_cast reads a clean u32, no Live-style
    // int16 sign-extension to mask).
    QString spellName;
    if (out.spell_id > 0 && m_spells) {
        if (const Spell* sp = m_spells->spell(out.spell_id)) {
            spellName = sp->name();
        }
    }

    emit spawnCast(out.caster_id, casterName,
                   out.spell_id, spellName, out.cast_time_ms);
}
