#include <QCoreApplication>
#include <QHash>   // QHashSeed::setDeterministicGlobalSeed for reproducible goldens
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QSocketNotifier>

#include <csignal>
#include <cstdio>
#include <sys/socket.h>
#include <unistd.h>

#include "daemonapp.h"

namespace {

// Replaces Qt's default "msg only" stderr handler with one that prepends an
// ISO-8601 UTC timestamp and a fixed-width level tag. Plain text by design —
// systemd-journald slurps stderr verbatim and `journalctl --output=json`
// already adds structured metadata around each line. JSON-in-message would
// just make grepping painful for a one-user LAN tool.
void messageHandler(QtMsgType type, const QMessageLogContext& ctx,
                    const QString& msg)
{
    const char* lvl;
    switch (type) {
        case QtDebugMsg:    lvl = "DEBUG"; break;
        case QtInfoMsg:     lvl = "INFO "; break;
        case QtWarningMsg:  lvl = "WARN "; break;
        case QtCriticalMsg: lvl = "ERROR"; break;
        case QtFatalMsg:    lvl = "FATAL"; break;
        default:            lvl = "?????"; break;
    }
    const QString ts =
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    // Categories other than the default ("default") get a tag so it's
    // obvious which subsystem produced the line. Most of the daemon
    // logs through the default category today.
    const bool hasCat = ctx.category && qstrcmp(ctx.category, "default") != 0;
    if (hasCat) {
        std::fprintf(stderr, "%s [%s] [%s] %s\n",
                     qUtf8Printable(ts), lvl, ctx.category,
                     qUtf8Printable(msg));
    } else {
        std::fprintf(stderr, "%s [%s] %s\n",
                     qUtf8Printable(ts), lvl, qUtf8Printable(msg));
    }
    std::fflush(stderr);

    // Qt's default behavior on QtFatalMsg is to abort; preserve that.
    if (type == QtFatalMsg) {
        std::abort();
    }
}

// SIGINT/SIGTERM/SIGHUP bridged into the Qt event loop via a socketpair.
// Signal handlers write one byte ('Q' for quit, 'H' for handoff+quit);
// a QSocketNotifier on the read end dispatches on the main thread. This
// gives us clean Qt teardown (FileSink flush, OpcodeStatsLogger report,
// WsServer session drop) on Ctrl-C, `systemctl stop`, and SIGHUP reload.
int g_signalFd[2] = {-1, -1};

void sigQuit(int /*sig*/)
{
    char b = 'Q';
    ssize_t r = ::write(g_signalFd[0], &b, 1);
    (void)r;
}

void sigHup(int /*sig*/)
{
    char b = 'H';
    ssize_t r = ::write(g_signalFd[0], &b, 1);
    (void)r;
}

void installSignalBridge()
{
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, g_signalFd) != 0) {
        qWarning("socketpair failed; SIGINT/SIGTERM will be ungraceful");
        return;
    }
    struct sigaction sa{};
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = &sigQuit;
    ::sigaction(SIGINT, &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);
    sa.sa_handler = &sigHup;
    ::sigaction(SIGHUP, &sa, nullptr);
    // QSocketNotifier is set up in main() after DaemonApp is constructed
    // so its activated lambda can capture the daemon for handoff export.
}

} // namespace

int main(int argc, char** argv)
{
    qInstallMessageHandler(messageHandler);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("scryd");
    QCoreApplication::setApplicationVersion("0.0.1");

    installSignalBridge();

    QCommandLineParser parser;
    parser.setApplicationDescription("Headless Scry packet-capture daemon");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption deviceOpt(QStringList{"d", "device"},
        "Network device to capture on (e.g. eth0). Required for live capture.",
        "device");
    QCommandLineOption ipOpt(QStringList{"ip"},
        "EQ client IP, or a server netblock in CIDR form (e.g. 69.174.0.0/16), "
        "to scope the pcap BPF filter (host vs net chosen by the '/'). Defaults "
        "to auto-detect the client on the first session handshake; set a client "
        "IP when multiple EQ clients share the LAN, or a server CIDR to keep "
        "only EQ traffic and drop ambient LAN UDP the mirror port also sees.",
        "ip");
    QCommandLineOption listenOpt(QStringList{"l", "listen"},
        "Address:port to serve WebSocket clients on (default 127.0.0.1:9090).",
        "host:port", "127.0.0.1:9090");
    QCommandLineOption replayOpt(QStringList{"r", "replay"},
        "Replay a recorded .vpk file instead of live capture.", "file");
    QCommandLineOption replayPcapOpt(QStringList{"replay-pcap"},
        "Replay a raw pcap/tcpdump capture (e.g. from dumpcap/tcpdump) "
        "through the offline decode path, instead of a .vpk. A UDP filter is "
        "applied so mixed captures (TCP/HTTPS/etc.) decode cleanly. Mutually "
        "exclusive with --replay.", "file");
    QCommandLineOption agentOpt(QStringList{"agent"},
        "Capture from a remote scry-agent at host:port (default port 9099) "
        "instead of a local device: the daemon dials the agent and asks it to "
        "capture EQ UDP with a BPF filter (scoped by --ip). Needs no "
        "cap_net_raw locally. Mutually exclusive with --device and --replay.",
        "host:port");
    QCommandLineOption configDirOpt(QStringList{"c", "config-dir"},
        "Directory holding opcode XML and other shared read-only config. "
        "Overrides the compiled-in PKGDATADIR; the writable user dir "
        "stays at ~/.scry. Convenient for running from the build tree.",
        "dir");
    QCommandLineOption mapsDirOpt(QStringList{"m", "maps-dir"},
        "Directory holding zone .map / .txt files. Defaults to "
        "~/.scry/maps, falling back to "
        "$config-dir/maps.", "dir");
    QCommandLineOption mapPackageOpt(QStringList{"map-package"},
        "Active map package: a subdirectory under a maps root holding a "
        "per-zone set of .map/.txt files, or \"default\" for the flat "
        "root. Overrides the persisted [Maps] Package preference for this "
        "run.", "id");
    QCommandLineOption recordVpkOpt(QStringList{"record-vpk"},
        "Record raw EQ packets to FILE.vpk for later --replay. Combine "
        "with --device for live capture.", "file");
    QCommandLineOption recordGoldenOpt(QStringList{"record-golden"},
        "Record the envelope stream a client would receive to FILE as "
        "length-delimited seq.v1.Envelope protobuf. With --replay, the "
        "daemon exits at EOF — the regression-harness golden workflow.",
        "file");
    QCommandLineOption opcodeStatsOpt(QStringList{"opcode-stats"},
        "Patch-day diagnostic: tally every decoded opcode (known + "
        "unknown) and write a sorted report with payload-size matches "
        "against known-struct sizes to FILE on shutdown.", "file");
    QCommandLineOption noListenOpt(QStringList{"no-listen"},
        "Skip the WebSocket server entirely. Useful for any session "
        "where no client connects: --record-vpk live captures, "
        "--replay + --record-golden runs, --opcode-stats diagnostics. "
        "Also avoids port-bind conflicts alongside another daemon.");
    QCommandLineOption strictGateSizesOpt(QStringList{"strict-gate-sizes"},
        "Exit non-zero if the backend gate-size audit flags any mapped "
        "SZC_Match opcode still gating on an inherited Live sizeof "
        "(default: warn only). Used by CI's per-target opcode-load smoke; "
        "no-op on backends without size overrides (live/test).");
    QCommandLineOption dumpPayloadOpt(QStringList{"dump-payload"},
        "Recon: write raw payload bytes of a chosen zone opcode to disk "
        "every time it fires. Format OPCODE:PATH (e.g. 0xdb56:/tmp/profile). "
        "Each match writes PATH.<N>.bin (1-indexed). Repeatable.",
        "spec");
    QCommandLineOption listEventsOpt(QStringList{"list-events"},
        "Recon: write one line per decoded packet to FILE — "
        "<unix_ms> <C|S> 0xXXXX <bytes> <stream> <name>. Use to time-"
        "correlate which opcode fired around an in-game event.",
        "file");
    QCommandLineOption listBoxesOpt(QStringList{"list-boxes"},
        "Multibox recon: dump the BoxRegistry (every distinct EQ "
        "client observed on the wire) to stderr every 5s. Pairs "
        "with --no-listen for client-less inspection. See "
        "docs/MULTIBOX_PLAN.md.");
    QCommandLineOption dumpAllSessionsOpt(QStringList{"dump-all-sessions"},
        "Recon: feed EVERY box/session's decoded packets to --dump-payload / "
        "--opcode-stats / --list-events, not just the first world session "
        "seen. Needed for multi-zone captures where the opcode you want is in "
        "a later session (e.g. a high-Y zone reached after zoning). "
        "Recon-only; no effect on proto output or goldens.");
    QCommandLineOption onlySessionOpt(QStringList{"only-session"},
        "Recon: restrict --dump-payload / --opcode-stats / --list-events to "
        "ONE box/session. SELECTOR is a character name (case-insensitive, "
        "matched once the name resolves; each new zone session of that "
        "character is followed automatically), a 1-based session index in "
        "discovery order, or 'first' (= index 1, the primary box). Overrides "
        "--dump-all-sessions. Recon-only; no effect on proto output or "
        "goldens.",
        "SELECTOR");
    QCommandLineOption waitForClientOpt(QStringList{"wait-for-client"},
        "With --replay: pause playback until the first WebSocket "
        "client subscribes, and don't quit at EOF. Use to drive the "
        "web UI from a recorded capture for manual verification.");
    QCommandLineOption boxIdleTtlOpt(QStringList{"box-idle-ttl"},
        "Reclaim a multibox session that's been idle for SECONDS "
        "(default 600). Each zone change spawns a fresh per-character "
        "Box; the sweep retires the superseded/logged-off ones. 0 "
        "disables eviction.", "seconds");
    QCommandLineOption lifecycleDecoderOpt(QStringList{"lifecycle-decoder"},
        "Immutable lifecycle owner for each session: legacy, shadow, or rust. "
        "The default is shadow. Changing it requires a session restart.",
        "mode", "shadow");
    QCommandLineOption entityDecoderOpt(QStringList{"entity-decoder"},
        "Immutable entity and spatial owner for each session: legacy, shadow, "
        "or rust. The default remains legacy until capture soak completes. "
        "Changing it requires a session restart.",
        "mode", "legacy");
    QCommandLineOption playerDecoderOpt(QStringList{"player-decoder"},
        "Immutable player identity, movement, vitals, appearance, and death "
        "owner for each session: legacy, shadow, or rust. The default remains "
        "legacy until capture soak completes. Changing it requires a session "
        "restart.", "mode", "legacy");
    QCommandLineOption progressionDecoderOpt(QStringList{"progression-decoder"},
        "Immutable items and progression owner for each session: legacy, "
        "shadow, or rust. The default remains legacy until capture soak "
        "completes. Changing it requires a session restart.",
        "mode", "legacy");
    QCommandLineOption lootDecoderOpt(QStringList{"loot-decoder"},
        "Immutable loot correlation and persistence owner for each session: "
        "legacy, shadow, or rust. The default remains legacy until capture "
        "soak completes. Changing it requires a session restart.",
        "mode", "legacy");
    QCommandLineOption combatDecoderOpt(QStringList{"combat-decoder"},
        "Immutable combat, spells, casting, damage, and buffs owner for each "
        "session: legacy, shadow, or rust. The default remains legacy until "
        "capture soak completes. Changing it requires a session restart.",
        "mode", "legacy");
    parser.addOption(deviceOpt);
    parser.addOption(ipOpt);
    parser.addOption(listenOpt);
    parser.addOption(replayOpt);
    parser.addOption(replayPcapOpt);
    parser.addOption(agentOpt);
    parser.addOption(configDirOpt);
    parser.addOption(mapsDirOpt);
    parser.addOption(mapPackageOpt);
    parser.addOption(recordVpkOpt);
    parser.addOption(recordGoldenOpt);
    parser.addOption(opcodeStatsOpt);
    parser.addOption(noListenOpt);
    parser.addOption(strictGateSizesOpt);
    parser.addOption(dumpPayloadOpt);
    parser.addOption(listEventsOpt);
    parser.addOption(listBoxesOpt);
    parser.addOption(dumpAllSessionsOpt);
    parser.addOption(onlySessionOpt);
    parser.addOption(waitForClientOpt);
    parser.addOption(boxIdleTtlOpt);
    parser.addOption(lifecycleDecoderOpt);
    parser.addOption(entityDecoderOpt);
    parser.addOption(playerDecoderOpt);
    parser.addOption(progressionDecoderOpt);
    parser.addOption(lootDecoderOpt);
    parser.addOption(combatDecoderOpt);
    parser.process(app);

    DaemonApp::Config cfg;
    cfg.device       = parser.value(deviceOpt);
    cfg.ip           = parser.value(ipOpt);
    cfg.replay       = parser.value(replayOpt);
    // --replay-pcap reuses the whole replay-session path (no device, quit at
    // EOF, persistence off); it only differs in which offline reader runs, so
    // fold it into cfg.replay and flag the format. Mutually exclusive with
    // --replay since both feed the single VPacket Filename pref.
    const QString replayPcap = parser.value(replayPcapOpt);
    if (!cfg.replay.isEmpty() && !replayPcap.isEmpty()) {
        qCritical("--replay and --replay-pcap are mutually exclusive");
        return 2;
    }
    if (!replayPcap.isEmpty()) {
        cfg.replay       = replayPcap;
        cfg.replayIsPcap = true;
    }
    cfg.agent        = parser.value(agentOpt);
    // The remote agent replaces the local capture device and is a live source,
    // so it can't combine with a local --device or an offline --replay.
    if (!cfg.agent.isEmpty() && !cfg.device.isEmpty()) {
        qCritical("--agent and --device are mutually exclusive");
        return 2;
    }
    if (!cfg.agent.isEmpty() && !cfg.replay.isEmpty()) {
        qCritical("--agent and --replay/--replay-pcap are mutually exclusive");
        return 2;
    }
    cfg.configDir    = parser.value(configDirOpt);
    cfg.mapsDir      = parser.value(mapsDirOpt);
    cfg.mapPackage   = parser.value(mapPackageOpt);
    cfg.recordVpk    = parser.value(recordVpkOpt);
    cfg.recordGolden = parser.value(recordGoldenOpt);
    cfg.opcodeStats  = parser.value(opcodeStatsOpt);
    cfg.noListen     = parser.isSet(noListenOpt);
    cfg.strictGateSizes = parser.isSet(strictGateSizesOpt);
    cfg.dumpPayload  = parser.values(dumpPayloadOpt);
    cfg.listEvents   = parser.value(listEventsOpt);
    cfg.listBoxes    = parser.isSet(listBoxesOpt);
    cfg.dumpAllSessions = parser.isSet(dumpAllSessionsOpt);
    cfg.onlySession   = parser.value(onlySessionOpt);
    cfg.waitForClient = parser.isSet(waitForClientOpt);
    cfg.lifecycleDecoder = parser.value(lifecycleDecoderOpt).toLower();
    if (cfg.lifecycleDecoder != QLatin1String("legacy") &&
        cfg.lifecycleDecoder != QLatin1String("shadow") &&
        cfg.lifecycleDecoder != QLatin1String("rust")) {
        qCritical("--lifecycle-decoder must be legacy, shadow, or rust");
        return 2;
    }
    cfg.entityDecoder = parser.value(entityDecoderOpt).toLower();
    if (cfg.entityDecoder != QLatin1String("legacy") &&
        cfg.entityDecoder != QLatin1String("shadow") &&
        cfg.entityDecoder != QLatin1String("rust")) {
        qCritical("--entity-decoder must be legacy, shadow, or rust");
        return 2;
    }
    cfg.playerDecoder = parser.value(playerDecoderOpt).toLower();
    if (cfg.playerDecoder != QLatin1String("legacy") &&
        cfg.playerDecoder != QLatin1String("shadow") &&
        cfg.playerDecoder != QLatin1String("rust")) {
        qCritical("--player-decoder must be legacy, shadow, or rust");
        return 2;
    }
    cfg.progressionDecoder = parser.value(progressionDecoderOpt).toLower();
    if (cfg.progressionDecoder != QLatin1String("legacy") &&
        cfg.progressionDecoder != QLatin1String("shadow") &&
        cfg.progressionDecoder != QLatin1String("rust")) {
        qCritical("--progression-decoder must be legacy, shadow, or rust");
        return 2;
    }
    cfg.lootDecoder = parser.value(lootDecoderOpt).toLower();
    if (cfg.lootDecoder != QLatin1String("legacy") &&
        cfg.lootDecoder != QLatin1String("shadow") &&
        cfg.lootDecoder != QLatin1String("rust")) {
        qCritical("--loot-decoder must be legacy, shadow, or rust");
        return 2;
    }
    cfg.combatDecoder = parser.value(combatDecoderOpt).toLower();
    if (cfg.combatDecoder != QLatin1String("legacy") &&
        cfg.combatDecoder != QLatin1String("shadow") &&
        cfg.combatDecoder != QLatin1String("rust")) {
        qCritical("--combat-decoder must be legacy, shadow, or rust");
        return 2;
    }
    if (parser.isSet(boxIdleTtlOpt)) {
        bool ok = false;
        const qint64 secs = parser.value(boxIdleTtlOpt).toLongLong(&ok);
        if (ok && secs >= 0) {
            cfg.boxIdleTtlMs = secs * 1000;
        } else {
            qWarning("--box-idle-ttl: expected a non-negative integer "
                     "(seconds), got '%s'; keeping default %lld s",
                     qUtf8Printable(parser.value(boxIdleTtlOpt)),
                     cfg.boxIdleTtlMs / 1000);
        }
    }

    // Resolve record paths against cwd for the same reason --config-dir
    // is — under sudo, $HOME points at /root.
    if (!cfg.recordVpk.isEmpty() && QDir::isRelativePath(cfg.recordVpk)) {
        cfg.recordVpk = QFileInfo(cfg.recordVpk).absoluteFilePath();
    }
    if (!cfg.recordGolden.isEmpty() && QDir::isRelativePath(cfg.recordGolden)) {
        cfg.recordGolden = QFileInfo(cfg.recordGolden).absoluteFilePath();
    }
    // Golden recording must be byte-reproducible across processes. Qt seeds
    // QHash with a per-process random value, so QHash iteration order (e.g.
    // SpawnShell's spawn maps, which the decode path both iterates and creates
    // spawns from on position updates) varies run-to-run — flipping close-call
    // add/update decisions and making tier-2 goldens flap. Pin the seed so a
    // recording is deterministic. Record-golden only; live capture keeps Qt's
    // hash randomization. Must run before any decode-path QHash is populated.
    if (!cfg.recordGolden.isEmpty())
        QHashSeed::setDeterministicGlobalSeed();
    if (!cfg.opcodeStats.isEmpty() && QDir::isRelativePath(cfg.opcodeStats)) {
        cfg.opcodeStats = QFileInfo(cfg.opcodeStats).absoluteFilePath();
    }
    if (!cfg.listEvents.isEmpty() && QDir::isRelativePath(cfg.listEvents)) {
        cfg.listEvents = QFileInfo(cfg.listEvents).absoluteFilePath();
    }

    // Resolve --config-dir relative to the invocation cwd, not $HOME.
    // Under sudo, $HOME is /root, which would silently send DataLocationMgr
    // off into the wrong directory. QFileInfo::absoluteFilePath() uses the
    // process cwd, which is what users expect when they type `./conf`.
    if (!cfg.configDir.isEmpty() && QDir::isRelativePath(cfg.configDir)) {
        cfg.configDir = QFileInfo(cfg.configDir).absoluteFilePath();
    }
    if (!cfg.mapsDir.isEmpty() && QDir::isRelativePath(cfg.mapsDir)) {
        cfg.mapsDir = QFileInfo(cfg.mapsDir).absoluteFilePath();
    }

    const QString listen = parser.value(listenOpt);
    const int colon = listen.lastIndexOf(':');
    if (colon <= 0) {
        qCritical("invalid --listen value %s (expected host:port)",
                  qUtf8Printable(listen));
        return 2;
    }
    cfg.listenHost = QHostAddress(listen.left(colon));
    cfg.listenPort = static_cast<quint16>(listen.mid(colon + 1).toUInt());

    DaemonApp daemon(cfg);
    if (!daemon.start()) {
        return 1;
    }

    // Wire signal bytes to actions now that daemon is alive and can be
    // captured by the lambda. 'Q' → graceful quit; 'H' → export session
    // handoff state then quit (new binary reads it on startup).
    auto* notifier = new QSocketNotifier(g_signalFd[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, [&daemon, &cfg] {
        char b = 'Q';
        ssize_t r = ::read(g_signalFd[1], &b, 1);
        (void)r;
        if (b == 'H') {
            qInfo("SIGHUP: exporting session handoff state");
            daemon.exportHandoffState(cfg.configDir);
            // Qt teardown crashes when invoked with live pcap state.
            // The handoff file is fully flushed; skip teardown and let
            // the OS reclaim resources. _exit() bypasses C++ dtors and
            // Qt cleanup but not stdio (already fflush'd by our handler).
            ::_exit(75);
        }
        qInfo("shutdown signal received, exiting");
        QCoreApplication::quit();
    });

    if (daemon.importHandoffState(cfg.configDir))
        qInfo("session resumed from handoff state");

    return app.exec();
}
