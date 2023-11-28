// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Copyright (c) 2019 Bitcoin Association
// Distributed under the Open BSV software license, see the accompanying file LICENSE.

#ifndef BITCOIN_INIT_H
#define BITCOIN_INIT_H

#include <atomic>
#include <string>

#include "sync.h"
#include "taskcancellation.h"
#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

class Config; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class ConfigInit; // NOLINT(cppcoreguidelines-virtual-class-destructor)
class CScheduler;
class CWallet;

#if ENABLE_ZMQ
/**
* cs_zmqNotificationInterface is used to protect pzmqNotificationInterface. One of the race conditions can occur
* at shutdown when pzmqNotificationInterface gets deleted while RPC thread might still be using it.
*/
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern CCriticalSection cs_zmqNotificationInterface;
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
extern CZMQNotificationInterface *pzmqNotificationInterface;
#endif

namespace boost {
class thread_group;
} // namespace boost

void StartShutdown();
task::CCancellationToken GetShutdownToken();
/** Interrupt threads */
void Interrupt(boost::thread_group &threadGroup);
void Shutdown();
//! Initialize the logging infrastructure
void InitLogging();
//! Parameter interaction: change current parameters depending on various rules
void InitParameterInteraction();

// Get/set AppInit finished flag
std::atomic_bool& GetAppInitCompleted();

/** Initialize bitcoin core: Basic context setup.
 *  @note This can be done before daemonization.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInitBasicSetup();
/**
 * Initialization: parameter interaction.
 * @note This can be done before daemonization.
 * @pre Parameters should be parsed and config file should be read,
 * AppInitBasicSetup should have been called.
 */
bool AppInitParameterInteraction(ConfigInit &config);
/**
 * Initialization sanity checks: ecc init, sanity checks, dir lock.
 * @note This can be done before daemonization.
 * @pre Parameters should be parsed and config file should be read,
 * AppInitParameterInteraction should have been called.
 */
bool AppInitSanityChecks();
/**
 * Bitcoin core main initialization.
 * @note This should only be done after daemonization.
 * @pre Parameters should be parsed and config file should be read,
 * AppInitSanityChecks should have been called.
 */
bool AppInitMain(ConfigInit &config, boost::thread_group &threadGroup,
                 CScheduler &scheduler, const task::CCancellationToken& shutdownToken);

/** The help message mode determines what help message to show */
enum HelpMessageMode { HMM_BITCOIND };

/** Help for options shared between UI and daemon (for -help) */
std::string HelpMessage(HelpMessageMode mode, const Config& config);
/** Returns licensing information (for -version) */
std::string LicenseInfo();

#endif // BITCOIN_INIT_H
