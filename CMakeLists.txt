# Copyright (C) 2026, Lux Industries, Inc. All rights reserved.
# SPDX-License-Identifier: BSD-3-Clause-Eco
#
# luxfi/datastore — the Lux-native blockchain datastore coordination core.
# Replication-log ordering via consensus2 (leaderless PQ Quasar); part-content
# integrity via SHA-256; ZERO ZooKeeper, ZERO NuRaft.
#
# Reused, unmodified, from the luxcpp checkout (NOT vendored):
#   ${CONSENSUS2_DIR}                 — lux::consensus2 (the gate + photon/wave)
#   ${LUXCPP_ROOT}/crypto/sha256/cpp  — cevm::crypto::sha256 (CRHF)
#   ${LUXCPP_ROOT}/crypto/bls/cpp     — cevm::crypto::bls (for the e2e validator votes)
#   ${LUXCPP_ROOT}/blst               — blst BLS12-381 backend

cmake_minimum_required(VERSION 3.20)
project(luxfi_datastore LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(_default_luxcpp "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
set(LUXCPP_ROOT    "${_default_luxcpp}"                  CACHE PATH "luxcpp checkout root")
set(CONSENSUS2_DIR "${LUXCPP_ROOT}/consensus2-seed"      CACHE PATH "consensus2 source dir")
set(BLST_DIR       "${LUXCPP_ROOT}/blst"                 CACHE PATH "blst source tree")
set(BLS_DIR        "${LUXCPP_ROOT}/crypto/bls/cpp"       CACHE PATH "cevm::crypto::bls dir")
set(SHA_DIR        "${LUXCPP_ROOT}/crypto/sha256/cpp"    CACHE PATH "cevm::crypto::sha256 dir")

foreach(_p "${CONSENSUS2_DIR}/include/lux/consensus2/quorum_cert_engine.hpp"
           "${BLST_DIR}/bindings/blst.h" "${BLS_DIR}/bls_signature.cpp"
           "${SHA_DIR}/sha256.hpp")
  if(NOT EXISTS "${_p}")
    message(FATAL_ERROR "luxfi/datastore: required reused asset not found: ${_p}\n"
                        "set -DLUXCPP_ROOT=/path/to/luxcpp")
  endif()
endforeach()

# ── blst + cevm::crypto::bls (reused, for validator signing in the e2e test) ──
add_library(blst STATIC "${BLST_DIR}/src/server.c" "${BLST_DIR}/build/assembly.S")
target_compile_options(blst PRIVATE -O2 -fno-builtin)
target_include_directories(blst PUBLIC "${BLST_DIR}/bindings")

add_library(bls_signature STATIC "${BLS_DIR}/bls_signature.cpp")
target_include_directories(bls_signature PUBLIC "${BLS_DIR}")
target_link_libraries(bls_signature PUBLIC blst)

# ── cevm::crypto::sha256 (reused CRHF for the manifest digest) ────────────────
add_library(sha256 STATIC "${SHA_DIR}/sha256.cpp")
target_include_directories(sha256 PUBLIC "${SHA_DIR}")

# ── consensus2 (reused engine: the finality gate + photon/wave poll driver) ───
add_library(consensus2 STATIC
  "${CONSENSUS2_DIR}/src/quorum_cert_engine.cpp"
  "${CONSENSUS2_DIR}/src/wave.cpp")
target_include_directories(consensus2 PUBLIC "${CONSENSUS2_DIR}/include")
target_link_libraries(consensus2 PUBLIC bls_signature)

# ── luxfi/datastore — the NEW coordination core ───────────────────────────────
add_library(luxdatastore STATIC src/manifest_log.cpp src/coordinator.cpp)
target_include_directories(luxdatastore PUBLIC include)
target_link_libraries(luxdatastore PUBLIC consensus2 sha256)
target_compile_options(luxdatastore PRIVATE -Wall -Wextra -Wpedantic)

# ── e2e acceptance test ───────────────────────────────────────────────────────
enable_testing()
add_executable(coordinator_e2e_test test/coordinator_e2e_test.cpp)
target_link_libraries(coordinator_e2e_test PRIVATE luxdatastore)
target_compile_options(coordinator_e2e_test PRIVATE -Wall -Wextra)
add_test(NAME coordinator_e2e COMMAND coordinator_e2e_test)
