# luxfi/datastore

The Lux-native blockchain datastore — a ReplicatedMergeTree-style analytics store
whose **replication log is ordered by [consensus2](https://github.com/luxfi/consensus2)**
(leaderless, post-quantum-ready Quasar finality), with **zero ZooKeeper and zero
NuRaft**. Part bytes live off-chain on the S-Chain object store; the small,
ordered, consensus-final metadata (part manifests) is what compute replicas agree
on.

This is the Lux-native side of the two-datastore design in
[LP-400](https://github.com/luxfi/LPs/blob/main/LPs/lp-0400-blockchain-native-datastore.md):
`luxfi/datastore` coordinates through Quasar; `hanzoai/datastore` (the ClickHouse
fork) is the ZooKeeper-coordinated baseline being cut over to the same core.

## What's here (the coordination core, built & test-backed)

- **`ManifestLog`** (`manifest_log.hpp`) — the replicated state machine: an
  append-only, in-order log of part manifests + a `path → latest entry` view,
  mutated **only** by `commit()` in consensus order. This is the role ZooKeeper's
  znode tree played, rebuilt native. Each entry binds a **SHA-256 content digest**
  of the part bytes (a CRHF — a substituted object is caught on read).
- **`Coordinator`** (`coordinator.hpp`) — orders entries through consensus2:
  `propose → photon/wave liveness decision + >2/3-stake BLS quorum cert → commit`,
  in contiguous index order (out-of-order finality is buffered).

The join is the same decomplection consensus2 uses: the **liveness** layer
(photon sampling + wave FPC confidence) decides *which* entry converges; the
**safety** layer (`QuorumCertEngine`, real BLS + α-distinct + >2/3 stake) *proves*
it. The state machine cannot be advanced without a genuine quorum certificate.

## Status (honest)

- ✅ Coordination core + e2e: parts reach **real** consensus2 finality and commit
  in order; substituted parts are rejected by digest; a sub-quorum part does not
  commit. `coordinator_e2e_test` 3/3 PASS (clang-18, aarch64).
- ⏳ **Not yet:** the networking/gossip mesh (real distributed votes across a
  validator set — the e2e drives an in-process set with real BLS keys), the PQ
  triple-seal legs (Corona‖Pulsar‖Magnetar, via consensus2 Phase 2), the OLAP
  query/MergeTree engine (the ClickHouse-lineage port), and S-Chain object I/O.

## Build

Requires the luxcpp checkout (consensus2, blst, crypto/{bls,sha256}) as siblings;
see `CMakeLists.txt` (`-DLUXCPP_ROOT=...`).

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure
```

## Layout

```
datastore/
├── include/lux/datastore/{manifest_log,coordinator}.hpp
├── src/{manifest_log,coordinator}.cpp   ← the coordination core
└── test/coordinator_e2e_test.cpp        ← propose → consensus2 finality → commit
```
