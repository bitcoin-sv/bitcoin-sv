# Speeding up Initial Block Download (IBD)

IBD performance is dominated by disk I/O, script and transaction verification and
network throughput. The right hardware plus a handful of configuration tweaks can
dramatically reduce the time it takes a fresh node to reach the tip.

This document collects the levers a node operator can pull. It is a tip-sheet, not
an exhaustive tuning reference - for the full set of options and their help text,
run `bitcoind -help -help-debug`.

## Hardware

- **Networking** - A 1 Gbps link is easily saturated during the IBD phase; with a
  large number of peers 10 Gbps can help further. Latency to peers matters less
  than sustained throughput.
- **Storage** - NVMe SSDs are strongly preferred. The chainstate database and
  block files are random-access heavy; spinning disks bottleneck IBD by orders of
  magnitude. Sufficient free space matters too - running close to full may degrade
  filesystem performance.
- **CPU** - Both single-threaded performance (validation has serial sections) and
  core count (script verification is parallelised across cores) matter. Aim for a
  modern many-core CPU.
- **RAM** - More is better, you really cannot have too much memory. Beyond the
  explicit cache sizes set with `-coinsdbcache` and `-dbcache`, any free memory
  is used by the OS page cache to absorb LevelDB and block-file reads which really
  helps, particularly later in the IBD.

## Operating system

- **Filesystem** - A simple, low-overhead local filesystem such as `ext4` works
  well. Avoid complex (e.g. ZFS) or networked (e.g. NFS) filesystems for the data
  directory.
- **Open-file limit** - Raise the per-process open-files (`ulimit -n`) for the
  node to a high value (e.g. 16384). LevelDB plus many peer sockets can exhaust
  the low default on many systems.
- **I/O scheduler** - On NVMe, `none` or `mq-deadline` is usually appropriate.
  Avoid `bfq` on SSD.

## Node configuration

Use a recent build of the node software - IBD-relevant performance work lands
regularly, and a release from even a few months ago may be missing meaningful
improvements.

The options below are the ones most worth knowing about when tuning for IBD.
Flags marked *(debug)* are only listed by `bitcoind -help -help-debug`.

### Caches and validation

- **`-coinsdbcache=<MB>`** *(default: 8192)* — Total CoinsDB cache budget. 1/8 of
  this (capped at 2048 MiB) is used as the LevelDB chainstate cache; the remainder
  becomes the in-memory recent UTXO cache. This is the single biggest tuning
  knob for IBD on a modern node - consider raising it if RAM budget allows.
- **`-dbcache=<MB>`** *(default: 512)* — Cache size for all databases except the
  CoinsDB (block index, etc.). Much smaller impact than `-coinsdbcache` but cheap
  to raise on RAM-rich hosts.
- **`-maxcoinsdbfiles=<n>`** *(default: upto 8192)* - The maximum number of LevelDB
  chainstate files that can be open and memory-mapped at once. The default is to
  try to cover the entire chainstate, but depends on the system limits for
  maxmimum number of open files. This also presumes you have a significant RAM
  budget to support it; if you have less RAM, consider lowering this to save
  memory.
- **`-threadsperblock=<n>`** *(default: 0 = auto)* and **`-txnthreadsperblock=<n>`**
  *(default: 0 = auto)* — Script and transaction verification thread counts per
  block. Auto-detection from the CPU topology is almost always correct; override
  only when sharing the host with other heavy workloads or when oversubscription
  is observably hurting throughput.
- **`-assumevalid=<hex>`** *(default: a recent mainnet block hardcoded in the
  release)* — Skip script verification for blocks ancestral to the given hash.
  The default already does this, which is one reason why upgrading the binary
  itself sometimes speeds up IBD. Only override if you specifically want full
  verification (set to `0`) or to point at a more recent checkpoint than the
  binary ships with - understand the trust implications before doing so.

### Block download

- **`-blockdownloadlowerwindow=<n>`** *(debug; default: 1024 unpruned, 10 pruned)*
  — How far ahead of the validated tip blocks may be fetched. On a pruned node,
  raising this trades disk headroom against IBD speed and is well worth doing if
  you have the space available. On an unpruned node, lowering it caps the on-disk
  staging at the cost of throughput. Leave near the maximum (1024) if disk allows.
- **`-blockstallingmindownloadspeed=<Kb/s>`** *(default: 100)* — Floor below which
  a peer is considered stalling and block downloads will be redirected elsewhere.
  Lower this on slow links to tolerate slower peers, raise it on faster links to
  prefer peers who also have fast network links to us; setting it to `0` disables
  stall detection entirely (rarely a good idea on a healthy network).
- **`-maxconnections=<n>`** *(default: 125)* and
  **`-maxoutboundconnections=<n>`** *(default: 8)* — More outbound connections
  mean more parallel block sources. Modest increases to `-maxoutboundconnections`
  (e.g. 16) may help IBD on well-peered networks and hosts with good links.

### Storage and indexing

- **`-prune=<MB>`** *(default: 0 — disabled)* — Pruning reduces disk usage but
  interacts with `-blockdownloadlowerwindow` (which defaults to a very small
  window when pruning is on). If the goal is the fastest possible IBD and disk
  is available, do not prune; you can always prune later.
- **`-disablewallet`** - If the node has no wallet role, disabling the wallet
  removes a small amount of overhead during IBD and afterwards.

## Operational tips

- **Avoid extra load.** Don't run validation-heavy RPCs against the node during
  IBD (mass `getblock`, `verifychain`, large `gettxoutsetinfo`, etc.). These
  contend with the sync for the same locks, caches and disks.
- **Avoid restarts.** Each restart re-opens databases, replays the most recently
  flushed state, and warms caches from cold. Plan tuning changes so they are
  applied before the sync starts, not part-way through.
- **Seed known-good peers.** If a trusted node is reachable (one you run, or a
  vetted public peer), pre-seeding via `-addnode` removes peer discovery from the
  critical path early in IBD.
- **Watch the log.** Logging at level `bench` reports `UpdateTip` and block
  validation benchmarks for every connected block and logs flush events explicitly.
  Sudden slowdowns are usually disk or cache-bound and visible there before they
  show up in higher-level metrics.

