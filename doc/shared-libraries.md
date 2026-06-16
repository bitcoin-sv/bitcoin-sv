Shared Libraries
================

## bitcoinconsensus

The purpose of this library is to make the verification functionality that is critical to Bitcoin's consensus available to other applications, e.g. to language bindings.

### API

The interface is defined in the C header `bitcoinconsensus.h` located in  `src/script/bitcoinconsensus.h`.

#### Version

`bitcoinconsensus_version` returns an `unsigned int` with the API version *(currently `2`)*. Version `2` added the Genesis/Chronicle era flags and made `bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID` usable (see below).

#### Script Validation

Two entry points are provided:

- `bitcoinconsensus_verify_script` - verifies an input without supplying the value of the output being spent.
- `bitcoinconsensus_verify_script_with_amount` - same as above but takes an additional `int64_t amount` parameter giving the value (in satoshis) of the output being spent.

Both return an `int` with the status of the verification. It will be `1` if the input script correctly spends the previous output `scriptPubKey`, and `0` otherwise (inspect `err` to distinguish a failed verification from an input error).

Whether the spent amount is needed depends on how the input's signature is computed. A SIGHASH_FORKID signature uses the BIP143-style sighash, which commits to the spent amount, so that amount must be known to verify it. Most BSV inputs since the UAHF are signed this way and therefore require `bitcoinconsensus_verify_script_with_amount`. Chronicle made the signing algorithm a per-signature choice, so a Chronicle-era signature may instead opt for the original (legacy) sighash, which does not commit to the amount.

To guard against silently verifying against an unknown amount, `bitcoinconsensus_verify_script` refuses any request that would require it: if `flags` contains `bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID` it returns `0` with `err` set to `bitcoinconsensus_ERR_AMOUNT_REQUIRED` without performing verification. Use `bitcoinconsensus_verify_script_with_amount` to enable SIGHASH_FORKID; pass `0` for `amount` only when the signature does not commit to it.

> **Note:** `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_ALL` includes `bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID`. Passing `VERIFY_ALL` to `bitcoinconsensus_verify_script` therefore *always* fails with `bitcoinconsensus_ERR_AMOUNT_REQUIRED`. Use `VERIFY_ALL` only with `bitcoinconsensus_verify_script_with_amount`.

##### Parameters
- `const unsigned char *scriptPubKey` - The previous output script that encumbers spending.
- `unsigned int scriptPubKeyLen` - The number of bytes for the `scriptPubKey`.
- `int64_t amount` - (`_with_amount` only) The value, in satoshis, of the output being spent. Required to verify SIGHASH_FORKID signatures; pass `0` when the amount is not committed to.
- `const unsigned char *txTo` - The transaction with the input that is spending the previous output.
- `unsigned int txToLen` - The number of bytes for the `txTo`.
- `unsigned int nIn` - The index of the input in `txTo` that spends the `scriptPubKey`.
- `unsigned int flags` - The script validation flags *(see below)*.
- `bitcoinconsensus_error* err` - Will have the error/success code for the operation *(see below)*.

##### Script Flags
- `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_NONE`
- `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_P2SH` - Evaluate P2SH ([BIP16](https://github.com/bitcoin/bips/blob/master/bip-0016.mediawiki)) subscripts
- `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_DERSIG` - Enforce strict DER ([BIP66](https://github.com/bitcoin/bips/blob/master/bip-0066.mediawiki)) compliance
- `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_NULLDUMMY` - Enforce NULLDUMMY ([BIP147](https://github.com/bitcoin/bips/blob/master/bip-0147.mediawiki))
- `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY` - Enable CHECKLOCKTIMEVERIFY ([BIP65](https://github.com/bitcoin/bips/blob/master/bip-0065.mediawiki))
- `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY` - Enable CHECKSEQUENCEVERIFY ([BIP112](https://github.com/bitcoin/bips/blob/master/bip-0112.mediawiki))
- `bitcoinconsensus_SCRIPT_ENABLE_SIGHASH_FORKID` - Accept signatures using SIGHASH_FORKID, which most BSV inputs since the UAHF use. It selects the BIP143-style sighash that commits to the spent amount and therefore must be used with `bitcoinconsensus_verify_script_with_amount`; `bitcoinconsensus_verify_script` rejects it with `bitcoinconsensus_ERR_AMOUNT_REQUIRED`. Note this flag is part of `bitcoinconsensus_SCRIPT_FLAGS_VERIFY_ALL`.
- `bitcoinconsensus_SCRIPT_GENESIS` - The transaction is part of a block that uses Genesis rules.
- `bitcoinconsensus_SCRIPT_UTXO_AFTER_GENESIS` - The UTXO being spent was created after Genesis activation (per-input flag).
- `bitcoinconsensus_SCRIPT_CHRONICLE` - The transaction is part of a block that uses Chronicle rules.
- `bitcoinconsensus_SCRIPT_UTXO_AFTER_CHRONICLE` - The UTXO being spent was created after Chronicle activation (per-input flag). Must not be set without `bitcoinconsensus_SCRIPT_UTXO_AFTER_GENESIS`.

##### Errors
- `bitcoinconsensus_ERR_OK` - No errors with input parameters *(see the return value of `bitcoinconsensus_verify_script` for the verification status)*
- `bitcoinconsensus_ERR_TX_INDEX` - An invalid index for `txTo`
- `bitcoinconsensus_ERR_TX_SIZE_MISMATCH` - `txToLen` did not match with the size of `txTo`
- `bitcoinconsensus_ERR_TX_DESERIALIZE` - An error deserializing `txTo`
- `bitcoinconsensus_ERR_AMOUNT_REQUIRED` - Input amount is required if SIGHASH_FORKID is used
- `bitcoinconsensus_ERR_INVALID_FLAGS` - `flags` contained a bit outside the supported set, or an invalid combination (e.g. `UTXO_AFTER_CHRONICLE` without `UTXO_AFTER_GENESIS`). Returned directly as the function's return value. Flag validity is checked first, so this takes precedence over `bitcoinconsensus_ERR_AMOUNT_REQUIRED` when both conditions hold.
