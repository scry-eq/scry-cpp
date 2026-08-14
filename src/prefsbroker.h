#pragma once

#include <QObject>

#include "seq/v1/events.pb.h"

class EQPacket;

// PrefsBroker is the curated bridge between the TOML preference store
// store (`pSEQPrefs`) and the seq.v1 Pref wire schema. Only an
// allowlisted subset of XML preferences is shareable; reads, writes,
// and broadcasts all flow through this class.
//
// Storage stays XML on disk — file format ports are an open question
// (see MODERNIZATION_PLAN.md Phase 5). What we get from the broker is
// a stable wire surface that survives such a port without rewiring the
// clients.
class PrefsBroker : public QObject {
    Q_OBJECT
public:
    explicit PrefsBroker(QObject* parent = nullptr);

    // The capture pipeline is optional — neither --device nor --replay
    // means no EQPacket. Network/Device and Network/IP edits still
    // persist to XML in that mode; they just take effect on next start.
    void setPacket(EQPacket* packet) { m_packet = packet; }

    // Populate `out` with one Pref per allowlisted entry, reading the
    // current value from pSEQPrefs (falling back to the per-entry
    // default if the user has never set it).
    void fillSnapshot(seq::v1::PrefsSnapshot* out) const;

    // Validate against the allowlist, persist via pSEQPrefs, and emit
    // prefChanged() so SessionAdapters can broadcast PrefChanged to
    // every connected client. Returns false if `pref` does not match
    // any allowlist entry (unknown key/section, or value variant
    // mismatch) — in that case nothing is written or signalled.
    bool apply(const seq::v1::Pref& pref);

signals:
    void prefChanged(const seq::v1::Pref& pref);

private:
    EQPacket* m_packet = nullptr;
};
