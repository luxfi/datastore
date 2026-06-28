// Copyright (C) 2026, Lux Industries, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Eco

#include "lux/datastore/coordinator.hpp"

namespace lux::datastore {

Coordinator::Coordinator(std::vector<consensus2::Validator> validators,
                         std::uint32_t alpha,
                         consensus2::WaveConfig wave_cfg,
                         std::uint64_t epoch)
    : epoch_(epoch),
      gate_(std::move(validators), alpha),
      wave_(wave_cfg) {}

consensus2::VotePosition Coordinator::propose(ManifestEntry entry) {
    const std::uint64_t idx = ++next_idx_;
    entry.height = idx;

    // The consensus block id binds the exact manifest content: SHA-256 of its
    // canonical encoding. A quorum cert over this id certifies this entry, period.
    const std::vector<std::uint8_t> enc = encode_entry(entry);
    const Digest d = part_digest(enc.data(), enc.size());

    consensus2::VotePosition pos{};
    pos.block_id = d;          // Digest and BlockId are both array<uint8_t,32>
    pos.height = idx;
    pos.epoch = epoch_;

    gate_.submit(pos);
    proposed_[pos.block_id] = Proposed{std::move(entry), idx, false};
    return pos;
}

consensus2::Decision Coordinator::record_poll(const consensus2::BlockId& block_id,
                                              std::uint32_t yes, std::uint32_t total) {
    const auto it = proposed_.find(block_id);
    if (it == proposed_.end()) return consensus2::Decision::Undecided;
    return wave_.record_round(block_id, yes, total);  // wave keys on the full block id (M4)
}

consensus2::VoteResult Coordinator::record_vote(const consensus2::BlockId& block_id,
                                                const consensus2::PubKey& pk,
                                                const consensus2::Signature& sig) {
    return gate_.record_vote(block_id, pk, sig);
}

std::size_t Coordinator::try_commit() {
    // 1) Promote finalized blocks (wave Accept AND a >2/3-stake quorum cert), then
    //    ERASE them from proposed_ and drop their votes from the gate — scan walks
    //    in-flight blocks only (no O(N²) re-walk) and memory stays bounded (no
    //    retained vote sets). [coordination benchmark fix]
    for (auto it = proposed_.begin(); it != proposed_.end();) {
        const auto& block_id = it->first;
        if (wave_.decision(block_id) != consensus2::Decision::Accept || !gate_.is_final(block_id)) {
            ++it;
            continue;
        }
        auto cert = gate_.assemble_cert(block_id);
        if (!cert) { ++it; continue; }               // structurally impossible once final
        ready_.emplace(it->second.idx, std::make_pair(it->second.entry, *cert));
        gate_.drop(block_id);                          // release the gate's votes
        it = proposed_.erase(it);                      // shrink the working set
    }

    // 2) Commit a contiguous prefix to the ManifestLog, in strict index order.
    std::size_t committed = 0;
    for (;;) {
        const std::uint64_t want = mlog_.last_committed_index() + 1;
        const auto it = ready_.find(want);
        if (it == ready_.end()) break;
        mlog_.commit(want, it->second.first);
        ready_.erase(it);
        ++committed;
    }
    return committed;
}

}  // namespace lux::datastore
