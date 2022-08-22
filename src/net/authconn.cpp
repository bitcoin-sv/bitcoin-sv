// Copyright (c) 2021 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#include "authconn.h"
#include "base58.h"
#include "script/script.h"
#include "util.h"
#include "utilstrencodings.h"

#include <iostream>

namespace authconn {

/**
 * The following constructor allows to read the auth conn keys from the 'authconnkeys.dat' data file. This file should contain
 * the same private/public key-pair used by the RI to authorise the coinbase document of the miner.
 *
 * This constructor aims to:
 * 1. It reads private and public keys from the 'authconnkeys.dat' data file, where:
 *    (a) The private key (BIP32-by-default or ECDSA data sequence) is stored in the 1st line.
 *    (b) The public key (33byte ECDSA hex-string data sequence as a compressed-by-default key) is stored in the 2nd line.
 * 2. If the 'authconnkeys.dat' file doesn't exist, and the node's instance is NOT running on the RegTest, then
 *    create the private/public key-pair and store them in the 'authconnkeys.dat'.
 * 3. If the 'authconnkeys.dat' file doesn't exist, and the node's instance is running on the RegTest, then
 *    create a deterministic private/public key-pair.
 *
 * Partially mocked functionality.
 * (a) The RI service controls the private key used to sign the coinbase document and to create the minerId public key.
 *     The same private key needs to be used to sign the 'auth challenge message' received by the node through the 'authch' net message.
 *     The signature is then sent to the counterparty by the 'authresp' net message for verification.
 *
 *     ISSUES:
 *     1. It has not yet been definied how the communication (required to sign the auth msg) between the node and RI should look like.
 *        (a) MinerID v0.2 protocol defines that the minerId private/public key is used for auth conn.
 *        (b) an update to the protocol may introduce a different data field where a separate auth conn public key could be specified.
 *
 * NOTE:
 * 1. The auth conn public key (minerId in the current version) should be shared with the counterparty through the MinerID protocol impl.
 */
AuthConnKeys::AuthConnKeys(AuthConnKeys::PrivKeyStoredFormat keyStoredFormat, bool fCompressed) {
    static constexpr char authConnKeyFileName[] = "authconnkeys.dat";
    auto path {GetDataDir() / authConnKeyFileName};
    if (!fs::exists(path)) {
        if (gArgs.GetBoolArg("-regtest", false)) {
            setPrivKeyFromData(
                {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
                 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f},
                fCompressed);
        } else {
            makeKeys(fCompressed);
        }
        std::ofstream file;
        file.open(path.string().c_str(), std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot create data file: " + std::string{authConnKeyFileName});
        }
        if (PrivKeyStoredFormat::BIP32 == keyStoredFormat) {
            // Create the master key.
            CExtKey masterKey {};
            masterKey.SetMaster(privKey.begin(), privKey.size());
            // The RI service creates/reads the private key directly from the master key (see CExtKey::key member variable).
            // The key derivation process (as it is explained by https://docs.moneybutton.com/docs/bsv/bsv-hd-private-key.html) is not currently in use.
            privKey = masterKey.key;
            pubKey = privKey.GetPubKey();
            // Decompress the public key.
            if (!fCompressed && pubKey.IsCompressed()) {
                pubKey.Decompress();
            }
            // Store the private key in the BIP32 format.
            CBitcoinExtKey b58key;
            b58key.SetKey(masterKey);
            // Write the keys to the data file.
            file << b58key.ToString();
        } else if (PrivKeyStoredFormat::ECDSA == keyStoredFormat) {
            file << HexStr(ToByteVector(privKey)).c_str();
        } else {
            throw std::runtime_error("Unsupported private key data storage format.");
        }
        file << std::endl;
        // Store the public key in the ECDSA format.
        file << HexStr(ToByteVector(pubKey)).c_str();
        file.close();
        LogPrint(BCLog::NETCONN, "Authentication keys successfully created and stored in the %s data file.\n", authConnKeyFileName);
    } else {
        std::ifstream file;
        file.open(path.string().c_str(), std::ios::in);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open data file: " + std::string{authConnKeyFileName});
        }
        std::string sPrivKey;
        // Read the private key as a BIP32 key.
        std::getline(file, sPrivKey);
        std::string sPubKey;
        // Read the public key as a 33byte ECDSA (compressed key) hex-string sequence.
        std::getline(file, sPubKey);
        file.close();
        // Check the expected format of the private key.
        if (PrivKeyStoredFormat::BIP32 == keyStoredFormat) {
            // Convert the key from the BIP32 to ECDSA format.
            CBitcoinExtKey bip32ExtPrivKey {sPrivKey};
            CExtKey newKey = bip32ExtPrivKey.GetKey();
            privKey.Set(newKey.key.begin(), newKey.key.end(), fCompressed);
        } else if (PrivKeyStoredFormat::ECDSA == keyStoredFormat) {
            std::vector<uint8_t> vchPrivKey {ParseHex(sPrivKey)};
            privKey.Set(vchPrivKey.begin(), vchPrivKey.end(), fCompressed);
        } else {
            throw std::runtime_error("Unsupported private key data storage format.");
        }
        // Check if the private key is correct.
        if (!privKey.IsValid()) {
            throw std::runtime_error(std::string{authConnKeyFileName}+": The private key is incorrect: "+sPrivKey);
        }
        if (!(fCompressed == privKey.IsCompressed())) {
            throw std::runtime_error(std::string{authConnKeyFileName}+": The private key: " + sPrivKey + ", is expected to be " +
                    (fCompressed ? "compressed" : "uncompressed"));
        }
        // Check if the public key is correct.
        std::vector<uint8_t> vchPubKey {ParseHex(sPubKey)};
        pubKey.Set(vchPubKey.begin(), vchPubKey.end());
        if (!(fCompressed == pubKey.IsCompressed())) {
            throw std::runtime_error(std::string{authConnKeyFileName}+": The public key: " + sPubKey + ", is expected to be " +
                    (fCompressed ? "compressed" : "uncompressed"));
        }
        if (!pubKey.IsValid()) {
            throw std::runtime_error(std::string{authConnKeyFileName}+": The public key is incorrect: "+sPubKey);
        }
        // Check if the key-pair is correct.
        if (!privKey.VerifyPubKey(pubKey)) {
            throw std::runtime_error(std::string{authConnKeyFileName}+": The private/public key-pair verification has failed.");
        }
        LogPrint(BCLog::NETCONN, "Authentication keys successfully loaded from the %s data file.\n", authConnKeyFileName);
    }
}
} // namespace authconn
