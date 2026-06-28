// Copyright (C) 2026, Lux Industries, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Eco
//
// coordinator.hpp — the Lux-native datastore coordinator. It orders replication-
// log entries (part manifests) through consensus2 — leaderless, post-quantum-
// ready, BLS quorum-cert finality — and applies them to the ManifestLog in
// consensus order. This is the component a ReplicatedMergeTree coordinates
// through, with ZERO ZooKeeper and ZERO NuRaft: the ZooKeeper API role is the
// ManifestLog; the ordering engine is consensus2.
//
// Flow per entry:  propose → (poll to liveness decision) + (collect signed votes
// to a >2/3-stake quorum cert) → commit to the ManifestLog, in contiguous index
// order. Out-of-order finality is buffered and flushed when the prefix is whole.

#pragma once

#include "lux/datastore/manifest_log.hpp"
#include "lux/consensus2/quorum_cert_engine.hpp"
#include "lux/consensus2/wave.hpp"

#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace lux::datastore {

class Coordinator {
public:
    Coordinator(std::vector<consensus2::Validator> validators,
                std::uint32_t alpha,
                consensus2::WaveConfig wave_cfg,
                std::uint64_t epoch);

    // Assign the next log index, derive the consensus block id from the entry
    // (SHA-256 of its canonical encoding), and register it for voting. Returns the
    // position validators sign over.
    consensus2::VotePosition propose(ManifestEntry entry);

    // Liveness layer: feed one poll round's tally for the block.
    consensus2::Decision record_poll(const consensus2::BlockId& block_id,
                                     std::uint32_t yes, std::uint32_t total);

    // Safety layer: record a validator's signed ACCEPT vote.
    consensus2::VoteResult record_vote(const consensus2::BlockId& block_id,
                                       const consensus2::PubKey& pk,
                                       const consensus2::Signature& sig);

    // Finalize+commit any blocks that have BOTH wave-decided Accept AND a
    // >2/3-stake quorum cert. Commits to the ManifestLog in contiguous index order
    // (buffers out-of-order finality). Returns the number newly committed.
    std::size_t try_commit();

    const ManifestLog& log() const { return mlog_; }
    std::uint64_t committed_index() const { return mlog_.last_committed_index(); }
    std::uint64_t total_stake() const { return gate_.total_stake(); }

private:
    std::uint64_t next_idx_ = 0;
    const std::uint64_t epoch_;
    consensus2::QuorumCertEngine gate_;
    consensus2::Wave wave_;
    ManifestLog mlog_;

    struct Proposed {
        ManifestEntry entry;
        std::uint64_t idx = 0;
        std::uint64_t item = 0;  // wave handle (first 8 bytes of block_id)
        bool ready = false;      // finality reached, queued for in-order commit
    };
    std::map<consensus2::BlockId, Proposed> proposed_;
    // idx → (entry, cert) finalized but awaiting a whole prefix to commit.
    std::map<std::uint64_t, std::pair<ManifestEntry, consensus2::QuorumCert>> ready_;
};

}  // namespace lux::datastore
