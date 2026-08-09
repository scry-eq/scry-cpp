cmake_minimum_required(VERSION 3.20)
project(showeq-daemon CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Daemon links QtCore + QtNetwork + QtXml + QtWebSockets only. QtGui
# and QtWidgets were both dropped: QtWidgets after stripping
# CategoryDlg + dead pass-through, QtGui after replacing QColor with
# the SeqColor POD (src/seqcolor.{h,cpp}). The daemon only instantiates
# QCoreApplication, never QGuiApplication or QApplication, so it
# still runs without a display.
find_package(Qt6 REQUIRED COMPONENTS Core Network Xml WebSockets)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(PCAP REQUIRED libpcap)
# Loot history (src/lootstore.cpp). The only DB in the daemon.
pkg_check_modules(SQLITE3 REQUIRED sqlite3)
pkg_check_modules(ZLIB REQUIRED zlib)

# --- backend target selection ------------------------------------------------
# A backend can differ from Live along two INDEPENDENT axes:
#   - struct set (which everquest.h is compiled): only `test` forks its own;
#     `live` and `eql` share Live's struct set. (eql owns no wire structs — it
#     reads the Legends wire by offset in EqlDispatch, so it reuses Live's.)
#   - wiring TU (which opcode->handler table): only `eql` differs (wire_eql.cpp
#     + EqlDispatch); `live` and `test` share wire_live.cpp.
# everquest.h lives per-backend at src/backend/<struct-set>/everquest.h and
# resolves via ${SEQ_STRUCT_DIR} on the core include path — no #ifdef in core.
#
# Switching SEQ_TARGET needs a clean configure (build.sh --clean or a fresh -B
# build dir) — build.sh only configures on first run.
set(SEQ_TARGET "live" CACHE STRING "Target server backend: live | test | eql")
set_property(CACHE SEQ_TARGET PROPERTY STRINGS live test eql)
if(NOT SEQ_TARGET MATCHES "^(live|test|eql)$")
  message(FATAL_ERROR "SEQ_TARGET must be one of: live, test, eql (got '${SEQ_TARGET}').")
endif()
string(TOUPPER "${SEQ_TARGET}" SEQ_TARGET_UPPER)

# Axis 1 — struct set (everquest.h include dir): test forks its own; live+eql share.
if(SEQ_TARGET STREQUAL "test")
  set(SEQ_STRUCT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/test")
else()                                       # live | eql share Live's struct set
  set(SEQ_STRUCT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/backend/live")
endif()

# Axis 2 — wiring TU: eql has its own (+ EqlDispatch); live+test share wire_live.cpp.
if(SEQ_TARGET STREQUAL "eql")
  set(SEQ_BACKEND_SOURCES
      ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/eql/wire_eql.cpp
      ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/eql/eqldispatch.cpp)
else()                                       # live | test share the live wiring
  set(SEQ_BACKEND_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/backend/live/wire_live.cpp)
endif()

# Live is the base target: its opcode table lives FLAT at conf/ and its data
# dir is FLAT at ~/.showeq, both kept backwards-compatible with legacy showeq
# (which shares that conf/ tree + ~/.showeq). Test/EQL nest under a per-target
# subdir so their opcode tables + item/spawn data never collide with Live's.
if(SEQ_TARGET STREQUAL "live")
  set(SEQ_DATA_NAMESPACE ".showeq")          # flat — shared with legacy showeq
  set(SEQ_OPCODE_SUBDIR ".")                 # runtime: conf/ root (flat)
  set(_seq_conf_dir "${CMAKE_CURRENT_SOURCE_DIR}/conf")
else()
  set(SEQ_DATA_NAMESPACE ".showeq/${SEQ_TARGET}")
  set(SEQ_OPCODE_SUBDIR "${SEQ_TARGET}")     # runtime: conf/<target>/
  set(_seq_conf_dir "${CMAKE_CURRENT_SOURCE_DIR}/conf/${SEQ_TARGET}")
endif()
message(STATUS "SEQ_TARGET=${SEQ_TARGET} (structs: ${SEQ_STRUCT_DIR}, data ns: ${SEQ_DATA_NAMESPACE}, opcodes: ${_seq_conf_dir})")

# --- Rust decoder bridge -----------------------------------------------------
# showeq-decoder-rs is a sibling repo. seq-bridge is a cxx staticlib exposing
# the per-opcode Rust decoders (seq::rust::decode_*) to C++. Rust is the only
# decoder — no C++ fallback, no SEQ_USE_RUST toggle. Corrosion builds seq-bridge
# with exactly one backend feature, matched 1:1 to SEQ_TARGET (live|test|eql).
include(FetchContent)
FetchContent_Declare(
  Corrosion
  GIT_REPOSITORY https://github.com/corrosion-rs/corrosion.git
  GIT_TAG v0.6
)
FetchContent_MakeAvailable(Corrosion)

# Defaults to the sibling checkout; override with -DSEQ_DECODER_RS_DIR=<path>
# (CI checks the crate out inside the workspace, not as a true sibling).
set(SEQ_DECODER_RS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../showeq-decoder-rs"
    CACHE PATH "Path to the showeq-decoder-rs checkout")
if(NOT EXISTS "${SEQ_DECODER_RS_DIR}/Cargo.toml")
  message(FATAL_ERROR
    "showeq-decoder-rs not found at ${SEQ_DECODER_RS_DIR}.\n"
    "Clone github.com/showeq-unofficial/showeq-decoder-rs as a sibling of "
    "showeq-daemon, or pass -DSEQ_DECODER_RS_DIR=<path>.")
endif()

corrosion_import_crate(
  MANIFEST_PATH "${SEQ_DECODER_RS_DIR}/Cargo.toml"
  CRATES seq-bridge
  NO_DEFAULT_FEATURES
  FEATURES "backend-${SEQ_TARGET}"
)
corrosion_add_cxxbridge(seq-bridge-cxx
  CRATE seq_bridge
  FILES lib.rs
)

# --- protobuf ----------------------------------------------------------------
# The schema lives in the showeq-proto submodule under proto/.
set(SEQ_PROTO_DIR "${CMAKE_SOURCE_DIR}/proto")
set(SEQ_PROTO_FILES
  "${SEQ_PROTO_DIR}/seq/v1/events.proto"
  "${SEQ_PROTO_DIR}/seq/v1/client.proto"
)

if(NOT EXISTS "${SEQ_PROTO_DIR}/seq/v1/events.proto")
  message(FATAL_ERROR
    "showeq-proto submodule is not checked out. Run:\n"
    "  git submodule update --init --recursive")
endif()

set(SEQ_PROTO_GEN_DIR "${CMAKE_BINARY_DIR}/gen-proto")
file(MAKE_DIRECTORY "${SEQ_PROTO_GEN_DIR}")

set(SEQ_PROTO_SRCS)
set(SEQ_PROTO_HDRS)
foreach(proto ${SEQ_PROTO_FILES})
  get_filename_component(proto_name "${proto}" NAME_WE)
  get_filename_component(proto_dir "${proto}" DIRECTORY)
  file(RELATIVE_PATH proto_rel "${SEQ_PROTO_DIR}" "${proto_dir}")
  set(cc "${SEQ_PROTO_GEN_DIR}/${proto_rel}/${proto_name}.pb.cc")
  set(hh "${SEQ_PROTO_GEN_DIR}/${proto_rel}/${proto_name}.pb.h")
  add_custom_command(
    OUTPUT "${cc}" "${hh}"
    COMMAND protobuf::protoc
            --cpp_out ${SEQ_PROTO_GEN_DIR}
            --proto_path ${SEQ_PROTO_DIR}
            ${proto}
    DEPENDS "${proto}" protobuf::protoc
    COMMENT "protoc ${proto_rel}/${proto_name}.proto"
    VERBATIM
  )
  list(APPEND SEQ_PROTO_SRCS "${cc}")
  list(APPEND SEQ_PROTO_HDRS "${hh}")
endforeach()

add_library(seq-proto STATIC ${SEQ_PROTO_SRCS} ${SEQ_PROTO_HDRS})
target_include_directories(seq-proto PUBLIC "${SEQ_PROTO_GEN_DIR}")
target_link_libraries(seq-proto PUBLIC protobuf::libprotobuf)
set_target_properties(seq-proto PROPERTIES AUTOMOC OFF AUTORCC OFF AUTOUIC OFF)

# --- daemon core library -----------------------------------------------------
# Everything except the WebSocket/main wiring goes into a static library so
# the test binaries (tests/) can link against the same code without dragging
# in QWebSockets or the command-line parser. Daemon executable adds the
# remaining pieces (main.cpp + WsServer + SessionAdapter + DaemonApp wiring).
#
# Sources extracted from showeq/src (authoritative list derived from
# showeq/src/Makefile.am's showeq_SOURCES, minus all QWidget-bound files).
set(SEQ_DAEMON_EXTRACTED_SOURCES
  src/bazaarlog.cpp
  src/category.cpp
  src/datalocationmgr.cpp
  src/datetimemgr.cpp
  src/diagnosticmessages.cpp
  src/eqstr.cpp
  src/filter.cpp
  src/filteredspawnlog.cpp
  src/filtermgr.cpp
  src/group.cpp
  src/guild.cpp
  src/guildshell.cpp
  src/logger.cpp
  src/mapcore.cpp
  src/message.cpp
  src/messagefilter.cpp
  src/messages.cpp
  src/messageshell.cpp
  src/netstream.cpp
  src/packet.cpp
  src/packetcapture.cpp
  src/packetcaptureprovider.cpp
  src/packetformat.cpp
  src/packetfragment.cpp
  src/packetinfo.cpp
  src/packetlog.cpp
  src/packetstream.cpp
  src/player.cpp
  src/spawn.cpp
  src/spawnlog.cpp
  src/spawnmonitor.cpp
  src/spawnshell.cpp
  src/spells.cpp
  src/spellshell.cpp
  src/util.cpp
  src/vpacket.cpp
  src/xmlconv.cpp
  src/xmlpreferences.cpp
  src/zonemgr.cpp
)

set(SEQ_DAEMON_CORE_NEW_SOURCES
  src/boxregistry.cpp
  src/remotecapture.cpp
  src/namepromoter.cpp
  src/zoneserverobserver.cpp
  src/protoencoder.cpp
  src/combatrouter.cpp
  src/filesink.cpp
  src/opcodestats.cpp
  src/opcodepayloaddumper.cpp
  src/eventlogger.cpp
  src/prefsbroker.cpp
  src/seq_globals.cpp
  src/seqcolor.cpp
  src/itempacket.cpp
  src/itemcache.cpp
  src/lootstore.cpp
  src/zoneservermgr.cpp
  src/dbstrings.cpp
  src/spellmessages.cpp
)

# The opcode table is the ONE part of conf/ that varies per target: Live's lives
# FLAT at conf/ (base, legacy-showeq compatible), Test/EQL under conf/<target>/.
# Everything else (seqdef.xml, maps/, spawnpoints/, filters/, daemon/) is shared
# at the conf/ root, so --config-dir stays "conf" for every target; the runtime
# picks the opcode dir via the SEQ_OPCODE_SUBDIR define ("." for live).
#
# opcodes.toml is read DIRECTLY at runtime (packetinfo.cpp, via vendored
# toml++). There is no generated XML step any more — it gave the same data two
# on-disk representations and two ways to desync (a stale regenerated XML, and
# a TOML syntax error leaving the old XML in place so check.sh passed).
file(MAKE_DIRECTORY "${_seq_conf_dir}")

add_library(seq-daemon-core STATIC
  ${SEQ_DAEMON_EXTRACTED_SOURCES}
  ${SEQ_DAEMON_CORE_NEW_SOURCES}
)

target_include_directories(seq-daemon-core PUBLIC
  src
  ${SEQ_STRUCT_DIR}    # per-backend everquest.h resolves here (src/ has none)
  # Vendored toml++ (single header, MIT) — the opcode table reader. Vendored
  # rather than fetched so configure needs no network and both CI containers
  # build identically; refresh by replacing the one file.
  third_party/tomlplusplus
  ${PCAP_INCLUDE_DIRS}
  ${SQLITE3_INCLUDE_DIRS}
)

target_link_libraries(seq-daemon-core PUBLIC
  seq-proto
  seq-bridge-cxx          # Rust decoders (cxx); propagates the generated header
  Qt6::Core
  Qt6::Network
  Qt6::Xml
  ${PCAP_LIBRARIES}
  ${SQLITE3_LIBRARIES}
  ${ZLIB_LIBRARIES}
  Threads::Threads
)

target_compile_options(seq-daemon-core PRIVATE
  -Wall
  -Werror
  # -Wno-unused-parameter / -Wno-deprecated-declarations remain because the
  # legacy code still triggers them; the rest of -Wall is clean and gated.
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

# Fedora's GCC 13+ flags Qt6's qmetatype.h SFINAE traits hitting our
# forward-declared signal-arg classes (EQPacketOPCode, Item, Spawn) as
# "previously failed to be complete in a SFINAE context". Restructuring
# signal/slot signatures to fix it isn't worth it; downgrade to warning
# on compilers that know the option (Ubuntu 22.04's GCC 11 doesn't, and
# would error on the `-Wno-error=…` itself).
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-Wsfinae-incomplete HAVE_W_SFINAE_INCOMPLETE)
if(HAVE_W_SFINAE_INCOMPLETE)
  target_compile_options(seq-daemon-core PRIVATE -Wno-error=sfinae-incomplete)
endif()

# Matches showeq's Makefile.am: INCLUDES = ... -DPKGDATADIR="..."
# datalocationmgr.cpp reads this; tests get the same install path so the
# extracted code keeps its single source of truth.
target_compile_definitions(seq-daemon-core PRIVATE
  PKGDATADIR="${CMAKE_INSTALL_PREFIX}/share/showeq-daemon/"
)

# Backend-target identity, PUBLIC so the executable + tests inherit them.
# SEQ_TARGET_<X> lets the rare target-aware spot branch without hardcoding;
# SEQ_DATA_NAMESPACE is the DataLocationMgr writable-root subdir (flat
# ".showeq" for live, nested ".showeq/<target>" otherwise).
target_compile_definitions(seq-daemon-core PUBLIC
  SEQ_TARGET_${SEQ_TARGET_UPPER}=1
  SEQ_DATA_NAMESPACE="${SEQ_DATA_NAMESPACE}"
  SEQ_OPCODE_SUBDIR="${SEQ_OPCODE_SUBDIR}"
)

# --- daemon executable -------------------------------------------------------
add_executable(showeq-daemon
  src/main.cpp
  src/daemonapp.cpp
  src/wsserver.cpp
  src/sessionadapter.cpp
  ${SEQ_BACKEND_SOURCES}   # per-target wiring TU (+ EqlDispatch for eql)
)

target_link_libraries(showeq-daemon PRIVATE
  seq-daemon-core
  Qt6::WebSockets
)

target_compile_options(showeq-daemon PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

# --- setcap (opt-in) ---------------------------------------------------------
# Restore cap_net_admin,cap_net_raw on the daemon after a rebuild so live
# pcap capture works without sudo. Requires a sudoers.d rule permitting
# passwordless setcap on this binary; see the project README. Not part of
# the default `all` target — invoke explicitly:
#   cmake --build build --target setcap
add_custom_target(setcap
  COMMAND sudo -n /usr/sbin/setcap cap_net_admin,cap_net_raw=eip $<TARGET_FILE:showeq-daemon>
  DEPENDS showeq-daemon
  COMMENT "Restoring cap_net_admin,cap_net_raw on showeq-daemon"
  VERBATIM
)

# --- tests -------------------------------------------------------------------
enable_testing()
if(EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
  add_subdirectory(tests)
endif()
