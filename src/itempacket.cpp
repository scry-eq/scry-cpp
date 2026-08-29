/*
 * itempacket.cpp - see itempacket.h.
 *
 * Wire layout (validated against inventory-naked.vpk strip→redress×2,
 * see OPCODES_LIVE_TODO.md 2026-05-03):
 *
 *   +0  u32      packetType  (0x74 in-bag, 0x76 bag-itself, 0x78 move-response)
 *   +4  char[16] ASCII instance-id (e.g. "vIS00B80001H3G00")
 *   +20 u8       NUL terminator
 *   +21 u32      stack/charges count
 *   +25 u32      mainSlot   (0 = top-level worn/inv/cursor; nonzero = parent bag slot)
 *   +29 u16      subSlot    (when mainSlot==0, the worn/inv slot index)
 *   +31 ...      remaining wrapper bytes (aug-link u32, padding) ending
 *                with the marker `ff ff ff ff 00 ...` immediately before
 *                the item name.
 *
 * After the wrapper, two null-terminated copies of the name follow
 * (item name then lore name); parsedItemTemplateStruct begins at the
 * byte after the second null. We locate the name region by scanning
 * forward from offset 0x40 for the first uppercase ASCII letter
 * preceded by a zero byte rather than walking the wrapper, since a few
 * wrapper fields (the aug-link region, padding) still aren't pinned.
 */

#include "itempacket.h"

#include <cstring>

namespace {

inline uint32_t readU32LE(const uint8_t* p)
{
    return  uint32_t(p[0])
         | (uint32_t(p[1]) << 8)
         | (uint32_t(p[2]) << 16)
         | (uint32_t(p[3]) << 24);
}

inline int32_t readI32LE(const uint8_t* p)
{
    return int32_t(readU32LE(p));
}

// Scan forward from `start` looking for the first uppercase ASCII
// letter preceded by a NUL byte — that's the item-name start in the
// jumbled binary header preceding the names.
size_t findNameStart(const uint8_t* data, size_t len, size_t start)
{
    for (size_t i = start; i + 1 < len; i++) {
        uint8_t b = data[i];
        if (b >= 'A' && b <= 'Z' && i > 0 && data[i - 1] == 0) {
            return i;
        }
    }
    return SIZE_MAX;
}

// Find the next NUL byte at or after `start`. Returns SIZE_MAX if not
// found within the buffer.
size_t findNul(const uint8_t* data, size_t len, size_t start)
{
    for (size_t i = start; i < len; i++) {
        if (data[i] == 0) return i;
    }
    return SIZE_MAX;
}

} // namespace

bool parseItemPacket(const uint8_t* data, size_t len, ItemTemplate* out)
{
    if (!data || !out) return false;

    // Locate the item name. The pre-name header is ~115 bytes; start
    // scanning from 0x40 to skip the leading instance-ID ASCII string.
    constexpr size_t kHeaderProbeStart = 0x40;
    if (len < kHeaderProbeStart) return false;

    // Wrapper fields at fixed offsets. The instance-id is exactly 16
    // ASCII chars + NUL across every observed sample; bail if the NUL
    // marker isn't where we expect, since downstream offsets won't be
    // trustworthy either.
    if (data[20] != 0) return false;
    out->serial = QString::fromLatin1(
        reinterpret_cast<const char*>(data + 4), 16);
    out->packetType = readU32LE(data + 0);
    out->stackCount = readU32LE(data + 21);
    out->wireStackCount = out->stackCount;
    out->mainSlot   = readU32LE(data + 25);
    out->subSlot    = uint16_t(data[29]) | (uint16_t(data[30]) << 8);
    out->containerId = 0;
    out->containerSlot = out->subSlot;
    out->parentSlot = out->mainSlot == 0 ? uint16_t(0xFFFF)
                                         : uint16_t(out->mainSlot);

    size_t nameStart = findNameStart(data, len, kHeaderProbeStart);
    if (nameStart == SIZE_MAX) return false;

    size_t firstNul = findNul(data, len, nameStart);
    if (firstNul == SIZE_MAX) return false;

    size_t loreStart = firstNul + 1;
    size_t secondNul = findNul(data, len, loreStart);
    if (secondNul == SIZE_MAX) return false;

    // parsedItemTemplateStruct starts here. We need at least 63 bytes
    // for the documented prefix (through AC at offset +62).
    constexpr size_t kParsedItemMinSize = 63;
    size_t postName = secondNul + 1;
    if (postName + kParsedItemMinSize > len) return false;

    out->itemName = QString::fromLatin1(
        reinterpret_cast<const char*>(data + nameStart),
        int(firstNul - nameStart));
    out->loreName = QString::fromLatin1(
        reinterpret_cast<const char*>(data + loreStart),
        int(secondNul - loreStart));

    const uint8_t* p = data + postName;

    out->itemId      = readU32LE(p + 8);
    out->weight      = readU32LE(p + 12) / 10.0f;
    out->weightTenths = readU32LE(p + 12);
    out->flags       = readU32LE(p + 16);
    out->wireFlags   = out->flags;
    out->slotBitmask = readU32LE(p + 20);

    for (int i = 0; i < ITEM_RES_COUNT; i++) {
        out->resists[i] = int8_t(p[34 + i]);
    }
    out->corruption = int8_t(p[39]);
    out->wireCorruption = out->corruption;
    for (int i = 0; i < ITEM_STAT_COUNT; i++) {
        out->stats[i] = int8_t(p[40 + i]);
    }

    out->hp        = readI32LE(p + 47);
    out->mana      = readI32LE(p + 51);
    out->endurance = readI32LE(p + 55);
    out->ac        = readI32LE(p + 59);

    return true;
}
