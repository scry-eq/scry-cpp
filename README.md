# scry-cpp

Headless packet-capture and state-tracking daemon for
[ShowEQ](https://sourceforge.net/projects/seq/). Extracted from the legacy
monolithic `showeq` Qt application so multiple clients (web, native
Rust/Iced, future tools) can connect to a single capture process over
WebSocket + protobuf.

## What it does

- Captures EverQuest network traffic via libpcap.
- Reassembles the UDP session layer, decodes opcodes, tracks game state
  (spawns, zones, player, group, guild).
- Serves the state to clients on a WebSocket, encoded as `seq.v1` protobuf
  messages.

## What it is not

- Not a GUI. For a UI, run one of the clients (see `scry-web`, or run the
  legacy `showeq` standalone).
- Not a replacement for `showeq` yet. Until feature parity is reached,
  `showeq` remains the reference implementation and regression oracle.

## Build

Requires: CMake 3.20+, Qt 5.15+ (Core, Network, Xml, WebSockets), libpcap,
Protobuf, zlib, pthreads.

Debian/Ubuntu:

```sh
sudo apt install build-essential cmake \
    qtbase5-dev libqt5websockets5-dev \
    libpcap-dev libprotobuf-dev protobuf-compiler zlib1g-dev
```

Fedora/RHEL:

```sh
sudo dnf install gcc-c++ make cmake \
    qt5-qtbase-devel qt5-qtwebsockets-devel \
    libpcap-devel protobuf-devel protobuf-compiler zlib-devel
```

```sh
git submodule update --init --recursive
git config core.hooksPath scripts/hooks   # activate the committed pre-push hook (once per clone)
cmake -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

The pre-push hook (`scripts/hooks/pre-push`) rebuilds, runs the tier-2 replay
check, verifies the `proto/` submodule is in sync with canonical `origin/proto`,
and checks the Rust bindings are fresh when `everquest.h` changed. Bypass with
`--no-verify`.

For the optional Rust decoder integration, see [RUST.md](RUST.md).

## Run

libpcap requires CAP_NET_RAW or root:

```sh
sudo build/scryd --device eth0 --listen 127.0.0.1:9090
```

For LAN-reachable mode (trusted LAN only — no auth, no TLS in v1):

```sh
sudo build/scryd --device eth0 --listen 0.0.0.0:9090
```

## Running as a service

A systemd unit lives at `packaging/systemd/scryd.service`.
First-time install (assumes the daemon binary is already at
`/usr/local/bin/scryd` and config files at
`/usr/local/share/scryd`):

```sh
sudo install -d /etc/scryd /var/lib/scryd
sudo install -m 0644 packaging/systemd/scryd.env.example \
    /etc/scryd/scryd.env
sudo $EDITOR /etc/scryd/scryd.env       # set SEQ_DEVICE etc.
sudo install -m 0644 packaging/systemd/scryd.service \
    /etc/systemd/system/scryd.service
sudo systemctl daemon-reload
sudo systemctl enable --now scryd
```

Logs land in the journal — the daemon's Qt message handler already
prepends ISO timestamps and `[INFO ]`/`[WARN ]`/`[ERROR]` tags:

```sh
journalctl -u scryd -f
```

The unit runs as root for `CAP_NET_RAW` + `CAP_NET_ADMIN`. To drop
privileges, set `User=` to a regular account and uncomment the
`AmbientCapabilities` / `CapabilityBoundingSet` lines in the unit.

## Layout

```
src/              # Daemon sources (extracted from showeq + new glue)
  daemonapp.*     # Top-level wiring hub, replaces interface.cpp's role
  wsserver.*      # QWebSocketServer
  sessionadapter.*# Per-client adapter: QObject signals -> protobuf
  protoencoder.*  # Pure translation functions
  ...             # Packet layer + managers, see extraction inventory
proto/            # git submodule -> scry-proto
conf/             # Opcode XML + preference schema
docs/             # patch-day.md and friends
tests/            # tier-1 ctest suite + tier-2 replay scripts
packaging/        # systemd unit + env example
cmake/            # CMake helper modules
```

## License

GPL-2.0 (inherited from `showeq`). See [LICENSE](LICENSE).

## Beyond a trusted LAN

The v1 daemon ships **plaintext WebSocket, no auth, no TLS** by design
— it's a single-user packet-sniffer for a home LAN. If you want to put
it on a hostile network (untrusted Wi-Fi, VPS, public IP), don't expose
the daemon directly. Instead:

- **TLS** — terminate at a reverse proxy in front of the daemon
  (`nginx`, `caddy`, `traefik`). Bind the daemon to `127.0.0.1`,
  proxy `wss://` from the public side. The daemon doesn't need to
  know about TLS. Caddy's auto-HTTPS handles cert renewal in two
  config lines.
- **Auth** — same proxy can do HTTP basic auth or OIDC before
  forwarding the WebSocket upgrade. The daemon trusts whatever
  reaches its socket, so the proxy is the trust boundary.
- **Origin pinning** — the daemon's `QWebSocketServer` does not
  check `Origin` today; a malicious page on another origin could
  open a session if it can reach the listening port. A
  proxy-side `Origin` allowlist is the simplest fix.
- **Per-client rate limit** — the in-process token bucket caps
  inbound `ClientEnvelope`s at 30 msg/s with a 60-msg burst. That
  protects against accidental loops; for hostile clients, also
  put a connection-rate limit at the proxy.

None of these are on the daemon roadmap; they live at the edge.

## Patch day

EverQuest patches typically require updates to `src/everquest.h` and
`conf/*opcodes.xml` in *both* this repo and `showeq`. See
[docs/patch-day.md](docs/patch-day.md).
