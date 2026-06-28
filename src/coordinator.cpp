// Copyright (C) 2026, Lux Industries, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Eco

#include "lux/datastore/coordinator.hpp"

namespace lux::datastore {

namespace {
// Reduce a 32-byte block id to a wave item handle (first 8 bytes, big-endian).
std::uint64_t item_of(const consensus2::BlockId& b) {
    std::uint64_t h = 0;
    for (int i = 0; i < 8; ++i) h = (h << 8) | b[i];
    return h;
}
}  // namespace

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
    proposed_[pos.block_id] = Proposed{std::move(entry), idx, item_of(pos.block_id), false};
    return pos;
}

consensus2::Decision Coordinator::record_poll(const consensus2::BlockId& block_id,
                                              std::uint32_t yes, std::uint32_t total) {
    const auto it = proposed_.find(block_id);
    if (it == proposed_.end()) return consensus2::Decision::Undecided;
    return wave_.record_round(it->second.item, yes, total);
}

consensus2::VoteResult Coordinator::record_vote(const consensus2::BlockId& block_id,
                                                const consensus2::PubKey& pk,
                                                const consensus2::Signature& sig) {
    return gate_.record_vote(block_id, pk, sig);
}

std::size_t Coordinator::try_commit() {
    // 1) Promote any block that has reached BOTH liveness (wave Accept) and safety
    //    (a >2/3-stake quorum cert) into the ready queue, keyed by its log index.
    for (auto& [block_id, p] : proposed_) {
        if (p.ready) continue;
        if (wave_.decision(p.item) != consensus2::Decision::Accept) continue;
        if (!gate_.is_final(block_id)) continue;
        auto cert = gate_.assemble_cert(block_id);
        if (!cert) continue;                         // structurally impossible once final
        ready_.emplace(p.idx, std::make_pair(p.entry, *cert));
        p.ready = true;
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
