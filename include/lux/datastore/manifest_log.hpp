// Copyright (C) 2026, Lux Industries, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Eco
//
// manifest_log.hpp — the datastore's replicated state machine. An append-only,
// in-order log of part manifests plus a path→latest-entry view. This is what the
// ZooKeeper znode tree did for a ReplicatedMergeTree, rebuilt native: it is
// mutated ONLY by commit(), and only in consensus order (the Coordinator drives
// it from consensus2 finality). No ZooKeeper, no NuRaft.
//
// The bulk part bytes live off-chain on the S-Chain object store; the manifest is
// the small, ordered, consensus-final metadata — "part <path> with content digest
// <digest> exists at <height>" — that compute replicas agree on.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lux::datastore {

// SHA-256 of a part's bytes — a cryptographic CRHF, so a substituted object is
// detected on read (not a non-cryptographic CityHash/SipHash part checksum).
using Digest = std::array<std::uint8_t, 32>;

struct ManifestEntry {
    std::string path;        // e.g. /tables/events/parts/all_1_1_0
    Digest digest{};         // SHA-256 over the part bytes
    std::uint64_t height = 0;  // the log index this entry occupies
    std::uint64_t size = 0;    // part size in bytes (informational)
};

// SHA-256 content digest of a part's bytes (the CRHF the manifest binds).
Digest part_digest(const std::uint8_t* bytes, std::size_t len);

// Canonical, length-framed encoding of an entry — hashed to derive the consensus
// block id, so a quorum certificate binds the exact manifest content.
std::vector<std::uint8_t> encode_entry(const ManifestEntry& e);

class ManifestLog {
public:
    // Apply a consensus-final entry at its log index. MUST be called in strict
    // ascending order (1,2,3,…); throws on a gap — an ordering safety invariant.
    void commit(std::uint64_t log_idx, const ManifestEntry& entry);

    std::optional<ManifestEntry> get(const std::string& path) const;
    std::uint64_t last_committed_index() const { return last_committed_; }
    std::size_t size() const { return log_.size(); }

    // Verify part bytes against the committed manifest digest (content integrity
    // on read). A node serving substituted bytes is caught by the CRHF.
    bool verify_part(const std::string& path, const std::uint8_t* bytes, std::size_t len) const;

private:
    std::vector<ManifestEntry> log_;
    std::map<std::string, ManifestEntry> view_;
    std::uint64_t last_committed_ = 0;
};

}  // namespace lux::datastore
