# Bitcoin SV Node software – v1.2.3 Release

Overview
========

The 1.2.3 node release is an important upgrade from version 1.2.2 that
contains a number of performance improvements, most notably to initial
block download (IBD) and block validation, together with new features
and bug fixes.

Important fixes and improvements in 1.2.3
-----------------------------------------

* Improved initial block download (IBD) and block validation performance.
  The CoinsDB (UTXO) database configuration has been simplified and retuned:
  a larger default file size, a dynamically calculated limit on open files,
  and a dedicated cache setting. See the configuration section below and
  the file [ibd-performance.md](ibd-performance.md) for more details.
* TCP socket improvements:
  - The P2P socket loop now waits with poll() instead of select() on Linux and
    BSD. Windows and macOS remain on select().
  - **-maxconnections** is no longer silently capped near 865 on poll() builds;
    it is bounded only by the process file descriptor limit. select() builds
    keep a cap, now about 428 with default settings, since each peer may hold
    two sockets and all of them must stay below FD_SETSIZE.
  - When the system file descriptor limit is too low for the requested
    configuration, the coins database file budget (**-maxcoinsdbfiles**) is
    reduced before the connection count, and startup now fails with a clear
    error rather than a warning if no connections at all would be possible.
  - Running out of file descriptors on accept() or connect() is now reported in
    the log at the default level, and a block that cannot be read from disk,
    whether while answering a filtered block request or while announcing a new
    block as a compact block, is logged instead of aborting the node.
* Improved parallelism during block validation by grouping transactions less
  pessimistically and reducing the default validation batch size.
* Accelerated SHA-256 and merkle root computation using SHA-NI instructions
  where the CPU supports them.
* Improved block file pruning. Pruning now runs less frequently but more
  aggressively, and unnecessary expensive coins-cache flushes on the
  AcceptBlock path have been eliminated.
* Added a new **rescanblockchain** RPC, ported from Bitcoin Core, allowing
  the wallet to be rescanned over a range of blocks.
* Added optional USDT (userland statically-defined tracing) instrumentation,
  including RPC and P2P latency tracepoints, enabled at build time with
  `--enable-usdt`.
* The public libbitcoinconsensus library now exposes the Genesis and
  Chronicle era flags and SIGHASH_FORKID, so post-Genesis/Chronicle and real
  (FORKID-signed) BSV inputs can be verified. The API version has been bumped
  to 2.
* Various bug fixes, including corrected coins-cache memory accounting, a fix
  in the block download tracker, correct handling of a failed connect at the
  end of a reorged chain, out-of-range integer handling in OP_SPLIT and
  deserialization, and fixes for invalid memory access in `CScript` and a
  use-after-free in the mempool eviction tracker.

Other items
-----------

* Thread-safety hardening, including new mutex wrappers, `GUARDED_BY`
  annotations, a ThreadSanitizer build, and a fix for a startup null
  dereference in `FlushStateToDisk`.
* Memory and performance improvements to transaction inventory tracking and
  the `prevector` container.
* Dead code removal, wallet code refactoring, and general code-quality
  improvements and hardening.

New or modified configuration options
-------------------------------------

* **-coinsdbcache** is a new option specifying the size of the CoinsDB
  cache(s). The value is split 1/8 to the underlying LevelDB cache and the
  remainder to the in-memory CoinsDB cache.
* **-dbcache** no longer affects the CoinsDB cache; it now controls only the
  block tree and merkle tree index DB caches. Most node operators should no
  longer need to set it.
* **-maxcoinsprovidercachesize** now defaults to the full size of the CoinsDB
  memory cache.
* **-maxcoinsdbfiles** default is now calculated dynamically (as close to 8192
  as system limits allow) instead of the previous fixed value of 64.
* **-maxcoinsdbfilesize** default has been increased to 128MB.
* **-prunehysteresis** is a new debug option that makes pruning remove block
  files until disk usage is a given percentage below the target cap, so
  pruning runs less often but more aggressively.
