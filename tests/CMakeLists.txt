# Tier-1 regression tests: pure-function unit tests against the daemon
# core library. No event loop, no network, no replay fixtures required.
#
# Tier-2 (replay against .vpk goldens) is documented in tests/README.md
# and is gated on capturing fixture files from a live session.

find_package(Qt6 REQUIRED COMPONENTS Test)

add_executable(protoencoder_test
  protoencoder_test.cpp
)

target_link_libraries(protoencoder_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(protoencoder_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME protoencoder_test COMMAND protoencoder_test)

add_executable(seqcolor_test
  seqcolor_test.cpp
)

target_link_libraries(seqcolor_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(seqcolor_test PRIVATE
  -Wall
)

add_test(NAME seqcolor_test COMMAND seqcolor_test)

add_executable(tomlpreferences_test
  tomlpreferences_test.cpp
)

target_link_libraries(tomlpreferences_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(tomlpreferences_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME tomlpreferences_test COMMAND tomlpreferences_test)


add_executable(packetinfo_test
  packetinfo_test.cpp
)

target_link_libraries(packetinfo_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(packetinfo_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME packetinfo_test COMMAND packetinfo_test)

add_executable(itempacket_test
  itempacket_test.cpp
)

target_link_libraries(itempacket_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(itempacket_test PRIVATE
  -Wall
)

add_test(NAME itempacket_test COMMAND itempacket_test)

add_executable(itemcache_test
  itemcache_test.cpp
)

target_link_libraries(itemcache_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(itemcache_test PRIVATE
  -Wall
)

add_test(NAME itemcache_test COMMAND itemcache_test)

add_executable(lootstore_test
  lootstore_test.cpp
)

target_link_libraries(lootstore_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(lootstore_test PRIVATE
  -Wall
)

add_test(NAME lootstore_test COMMAND lootstore_test)

add_executable(mappackages_test
  mappackages_test.cpp
)

target_link_libraries(mappackages_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(mappackages_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME mappackages_test COMMAND mappackages_test)

add_executable(boxregistry_test
  boxregistry_test.cpp
)

target_link_libraries(boxregistry_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(boxregistry_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME boxregistry_test COMMAND boxregistry_test)

add_executable(namepromoter_test
  namepromoter_test.cpp
)

target_link_libraries(namepromoter_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(namepromoter_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME namepromoter_test COMMAND namepromoter_test)

add_executable(packetstream_dispatch_test
  packetstream_dispatch_test.cpp
)

target_link_libraries(packetstream_dispatch_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(packetstream_dispatch_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME packetstream_dispatch_test COMMAND packetstream_dispatch_test)

# FFI smoke test for the Rust decoder bridge (seq::rust::decode_*). seq-daemon-core
# links seq-bridge-cxx PUBLIC, so it propagates the generated cxx header here.
add_executable(rustdecode_test
  rustdecode_test.cpp
)

target_link_libraries(rustdecode_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(rustdecode_test PRIVATE
  -Wall
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME rustdecode_test COMMAND rustdecode_test)

add_executable(rustsession_test
  rustsession_test.cpp
)

target_link_libraries(rustsession_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_options(rustsession_test PRIVATE
  -Wall
  -Werror
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME rustsession_test COMMAND rustsession_test)

add_executable(applicationtrace_test
  applicationtrace_test.cpp
)

target_link_libraries(applicationtrace_test PRIVATE
  seq-daemon-core
  Qt6::Test
)

target_compile_definitions(applicationtrace_test PRIVATE
  SEQ_DECODER_RS_DIR_PATH="${SEQ_DECODER_RS_DIR}"
  SEQ_TRACE_TARGET_DIR="${CMAKE_CURRENT_BINARY_DIR}/seq-trace-cargo-${SEQ_TARGET}"
)

target_compile_options(applicationtrace_test PRIVATE
  -Wall
  -Werror
  -Wno-unused-parameter
  -Wno-deprecated-declarations
)

add_test(NAME applicationtrace_test COMMAND applicationtrace_test)
