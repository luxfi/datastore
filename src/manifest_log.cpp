// Copyright (C) 2026, Lux Industries, Inc. All rights reserved.
// SPDX-License-Identifier: BSD-3-Clause-Eco

#include "lux/datastore/manifest_log.hpp"

#include "sha256.hpp"  // cevm::crypto::sha256 — REUSED CRHF (luxcpp/crypto/sha256)

#include <cstring>
#include <stdexcept>

namespace lux::datastore {

Digest part_digest(const std::uint8_t* bytes, std::size_t len) {
    Digest d{};
    cevm::crypto::sha256(reinterpret_cast<std::byte*>(d.data()),
                         reinterpret_cast<const std::byte*>(bytes), len);
    return d;
}

std::vector<std::uint8_t> encode_entry(const ManifestEntry& e) {
    std::vector<std::uint8_t> b;
    auto put64 = [&](std::uint64_t v) {
        for (int s = 56; s >= 0; s -= 8) b.push_back(std::uint8_t(v >> s));
    };
    // length-framed path, then digest, height, size — unambiguous and canonical.
    put64(e.path.size());
    b.insert(b.end(), e.path.begin(), e.path.end());
    b.insert(b.end(), e.digest.begin(), e.digest.end());
    put64(e.height);
    put64(e.size);
    return b;
}

void ManifestLog::commit(std::uint64_t log_idx, const ManifestEntry& entry) {
    if (log_idx != last_committed_ + 1)
        throw std::logic_error("manifest_log: out-of-order commit (gap in the replication log)");
    log_.push_back(entry);
    view_[entry.path] = entry;
    last_committed_ = log_idx;
}

std::optional<ManifestEntry> ManifestLog::get(const std::string& path) const {
    const auto it = view_.find(path);
    if (it == view_.end()) return std::nullopt;
    return it->second;
}

bool ManifestLog::verify_part(const std::string& path, const std::uint8_t* bytes, std::size_t len) const {
    const auto it = view_.find(path);
    if (it == view_.end()) return false;
    return part_digest(bytes, len) == it->second.digest;
}

}  // namespace lux::datastore
