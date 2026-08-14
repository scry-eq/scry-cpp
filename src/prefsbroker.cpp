#include "prefsbroker.h"

#include <QString>

#include "main.h"
#include "packet.h"
#include "tomlpreferences.h"

namespace {

// Allowlist. Adding a new entry requires touching three places below:
//   1. fillSnapshot() — read current value into the proto
//   2. apply()        — match section/key/value-type, persist, side-effect
//   3. (this comment) — keep the list of expected keys current
//
// Currently published:
//   Interface/DateTimeFormat (string) — chat timestamp format
//   Network/Device           (string) — pcap interface to capture on
//   Network/IP               (string) — EQ client IP filter (auto = 127.0.0.0)
constexpr const char* kInterfaceSection      = "Interface";
constexpr const char* kDateTimeFormatKey     = "DateTimeFormat";
constexpr const char* kDateTimeFormatDefault = "ddd MMM dd,yyyy - hh:mm ap";

constexpr const char* kNetworkSection   = "Network";
constexpr const char* kDeviceKey        = "Device";
constexpr const char* kDeviceDefault    = "";
constexpr const char* kIPKey            = "IP";
constexpr const char* kIPDefault        = "127.0.0.0";  // AUTOMATIC_CLIENT_IP

} // namespace

PrefsBroker::PrefsBroker(QObject* parent) : QObject(parent) {}

void PrefsBroker::fillSnapshot(seq::v1::PrefsSnapshot* out) const
{
    {
        auto* p = out->add_prefs();
        p->set_section(kInterfaceSection);
        p->set_key(kDateTimeFormatKey);
        p->set_string_value(
            pSEQPrefs->getPrefString(kDateTimeFormatKey, kInterfaceSection,
                                     kDateTimeFormatDefault).toStdString());
    }
    {
        auto* p = out->add_prefs();
        p->set_section(kNetworkSection);
        p->set_key(kDeviceKey);
        p->set_string_value(
            pSEQPrefs->getPrefString(kDeviceKey, kNetworkSection,
                                     kDeviceDefault).toStdString());
    }
    {
        auto* p = out->add_prefs();
        p->set_section(kNetworkSection);
        p->set_key(kIPKey);
        p->set_string_value(
            pSEQPrefs->getPrefString(kIPKey, kNetworkSection,
                                     kIPDefault).toStdString());
    }
}

bool PrefsBroker::apply(const seq::v1::Pref& pref)
{
    const QString section = QString::fromStdString(pref.section());
    const QString key     = QString::fromStdString(pref.key());

    if (section == kInterfaceSection && key == kDateTimeFormatKey) {
        if (pref.value_case() != seq::v1::Pref::kStringValue) return false;
        pSEQPrefs->setPrefString(
            key, section, QString::fromStdString(pref.string_value()));
        // The preference store batches modifications; flush so the change
        // survives a daemon restart.
        pSEQPrefs->save();
        emit prefChanged(pref);
        return true;
    }

    if (section == kNetworkSection && key == kDeviceKey) {
        if (pref.value_case() != seq::v1::Pref::kStringValue) return false;
        const QString dev = QString::fromStdString(pref.string_value());
        pSEQPrefs->setPrefString(key, section, dev);
        pSEQPrefs->save();
        // EQPacket::monitorDevice tears down the pcap handle and
        // restarts capture on the new interface. The user has to zone
        // for fresh decode state to flow — same behavior as showeq.
        // No-op in playback mode.
        if (m_packet && !dev.isEmpty()) m_packet->monitorDevice(dev);
        emit prefChanged(pref);
        return true;
    }

    if (section == kNetworkSection && key == kIPKey) {
        if (pref.value_case() != seq::v1::Pref::kStringValue) return false;
        const QString ip = QString::fromStdString(pref.string_value());
        pSEQPrefs->setPrefString(key, section, ip);
        pSEQPrefs->save();
        // Empty IP routes through monitorNextClient (auto-detect on
        // next session); explicit IP swaps the BPF filter.
        if (m_packet) {
            if (ip.isEmpty()) {
                m_packet->monitorNextClient();
            } else {
                m_packet->monitorIPClient(ip);
            }
        }
        emit prefChanged(pref);
        return true;
    }

    return false;
}
