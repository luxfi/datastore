// Copyright (C) 2026, Lux Industries, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Eco
//
// coordinator_e2e_test.cpp — end-to-end proof that the Lux-native datastore
// coordinator orders replication-log entries through REAL consensus2 finality
// (leaderless: photon/wave liveness + a >2/3-stake BLS quorum cert) and applies
// them to the ManifestLog in consensus order — with ZERO ZooKeeper and ZERO
// NuRaft (note the includes: only consensus2 + datastore + bls/sha256, no
// libnuraft, no ZooKeeper client).
//
//   propose(part manifest) → 4 virtuous polls (wave Accept) + 4 signed votes
//   carrying >2/3 stake (quorum cert) → commit, in index order. Reads return the
//   committed manifest; a substituted part is caught by its SHA-256 digest.

#include "lux/datastore/coordinator.hpp"
#include "lux/datastore/manifest_log.hpp"
#include "lux/consensus2/quorum_cert_engine.hpp"
#include "lux/consensus2/wave.hpp"
#include "bls_signature.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace lux::datastore;
namespace c2 = lux::consensus2;

namespace {
int g_fail = 0;
void check(bool ok, const std::string& what) {
    if (!ok) { std::printf("    ASSERT FAILED: %s\n", what.c_str()); ++g_fail; }
}

struct Key { std::array<std::uint8_t, 32> sk{}; c2::PubKey pk{}; };
Key make_key(std::uint8_t tag) {
    std::array<std::uint8_t, 32> seed{};
    seed[0] = tag;
    for (int i = 1; i < 32; ++i) seed[i] = std::uint8_t(0xA5 ^ (tag + i));
    Key k;
    if (cevm::crypto::bls::keygen(seed.data(), k.sk.data()) != 0) { std::puts("keygen failed"); std::exit(2); }
    if (cevm::crypto::bls::sk_to_pk(k.sk.data(), k.pk.data()) != 0) { std::puts("sk_to_pk failed"); std::exit(2); }
    return k;
}
c2::Signature sign(const Key& key, const c2::VotePosition& pos) {
    const std::vector<std::uint8_t> msg = c2::canonical_vote_message(pos);
    c2::Signature s{};
    if (cevm::crypto::bls::sign(key.sk.data(), msg.data(), msg.size(), s.data()) != 0) { std::puts("sign failed"); std::exit(2); }
    return s;
}
std::vector<std::uint8_t> part_bytes(std::uint8_t tag, std::size_t n) {
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = std::uint8_t(tag * 31 + i);
    return v;
}
}  // namespace

int main() {
    std::printf("=========== luxfi/datastore — coordinator e2e (zero ZooKeeper) ===========\n");
    std::printf("replication log ordered by consensus2 (BLS quorum cert) | no NuRaft, no ZK\n\n");

    // Validator set: 5 validators, stake 20 each (total 100), α = 4.
    std::vector<Key> keys;
    for (std::uint8_t i = 0; i < 5; ++i) keys.push_back(make_key(std::uint8_t(0x50 + i)));
    std::vector<c2::Validator> set;
    for (const auto& k : keys) set.push_back({k.pk, 20});

    Coordinator coord(set, /*alpha=*/4, c2::WaveConfig{/*k=*/5, /*alpha=*/0.8, /*beta=*/4}, /*epoch=*/1);
    check(coord.total_stake() == 100, "coordinator validator stake == 100");

    // Drive one part manifest through full consensus to commit. Returns the path.
    auto coordinate_part = [&](std::uint8_t tag, int n_votes) -> c2::VotePosition {
        const std::string path = "/tables/events/parts/all_" + std::to_string(tag) + "_" + std::to_string(tag) + "_0";
        const auto bytes = part_bytes(tag, 4096);
        ManifestEntry e;
        e.path = path;
        e.digest = part_digest(bytes.data(), bytes.size());
        e.size = bytes.size();

        const c2::VotePosition pos = coord.propose(std::move(e));
        // liveness: 4 virtuous poll rounds → wave decides Accept
        for (int r = 0; r < 4; ++r) coord.record_poll(pos.block_id, /*yes=*/5, /*total=*/5);
        // safety: n_votes signed ACCEPT votes
        for (int i = 0; i < n_votes; ++i)
            coord.record_vote(pos.block_id, keys[i].pk, sign(keys[i], pos));
        return pos;
    };

    // ── [1] three parts reach finality and commit in order ───────────────────
    {
        for (std::uint8_t t = 1; t <= 3; ++t) {
            coordinate_part(t, /*n_votes=*/4);          // 4 distinct, 80 stake (>66)
            coord.try_commit();
        }
        check(coord.committed_index() == 3, "committed index == 3 after 3 parts");
        check(coord.log().size() == 3, "manifest log holds 3 entries");
        auto m2 = coord.log().get("/tables/events/parts/all_2_2_0");
        check(m2.has_value(), "part 2 manifest is readable");
        check(m2 && m2->height == 2, "part 2 committed at log height 2 (consensus order)");
        std::printf("[1/3] 3 parts reach consensus2 finality and commit in order  => %s\n",
                    g_fail == 0 ? "PASS" : "FAIL");
    }

    // ── [2] content integrity: a substituted part is caught by its digest ────
    {
        const auto good = part_bytes(2, 4096);
        auto tampered = good;
        tampered[100] ^= 0xFF;  // flip one byte
        check(coord.log().verify_part("/tables/events/parts/all_2_2_0", good.data(), good.size()),
              "correct part bytes verify against the committed digest");
        check(!coord.log().verify_part("/tables/events/parts/all_2_2_0", tampered.data(), tampered.size()),
              "TAMPERED part bytes are REJECTED (SHA-256 mismatch)");
        std::printf("[2/3] substituted part rejected by cryptographic digest      => %s\n",
                    g_fail == 0 ? "PASS" : "FAIL");
    }

    // ── [3] no quorum ⇒ no commit (safety holds) ─────────────────────────────
    {
        coordinate_part(9, /*n_votes=*/3);              // only 3 < α=4 → not final
        const std::size_t n = coord.try_commit();
        check(n == 0, "a part with < α votes does NOT commit");
        check(coord.committed_index() == 3, "committed index unchanged (still 3)");
        check(!coord.log().get("/tables/events/parts/all_9_9_0").has_value(), "uncommitted part is not readable");
        std::printf("[3/3] sub-quorum part does not commit (safety)               => %s\n",
                    g_fail == 0 ? "PASS" : "FAIL");
    }

    std::printf("-------------------------------------------------------------------------\n");
    if (g_fail) { std::printf("==== DATASTORE E2E: FAIL (%d) ====\n", g_fail); return 1; }
    std::printf("==== DATASTORE E2E: 3/3 PASS — replication log finalized by consensus2, zero ZK ====\n");
    return 0;
}
