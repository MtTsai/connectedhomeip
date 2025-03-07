/*
 *
 *    Copyright (c) 2021-2022 Project CHIP Authors
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#pragma once

#include <app-common/zap-generated/cluster-objects.h>
#include <app/server/AppDelegate.h>
#include <app/server/CommissioningModeProvider.h>
#include <lib/dnssd/Advertiser.h>
#include <platform/CHIPDeviceConfig.h>
#include <protocols/secure_channel/RendezvousParameters.h>
#include <system/SystemClock.h>

namespace chip {

enum class CommissioningWindowAdvertisement
{
    kAllSupported,
    kDnssdOnly,
};

class Server;

class CommissioningWindowManager : public SessionEstablishmentDelegate,
                                   public app::CommissioningModeProvider,
                                   public SessionDelegate
{
public:
    CommissioningWindowManager() : mPASESession(*this) {}

    CHIP_ERROR Init(Server * server)
    {
        if (server == nullptr)
        {
            return CHIP_ERROR_INVALID_ARGUMENT;
        }
        mServer = server;
        return CHIP_NO_ERROR;
    }

    static constexpr System::Clock::Seconds16 MaxCommissioningTimeout()
    {
        // Specification section 5.4.2.3. Announcement Duration says 15 minutes.
        return System::Clock::Seconds16(15 * 60);
    }

    System::Clock::Seconds16 MinCommissioningTimeout() const
    {
        // Specification section 5.4.2.3. Announcement Duration says 3 minutes.
        return mMinCommissioningTimeoutOverride.ValueOr(System::Clock::Seconds16(3 * 60));
    }

    void SetAppDelegate(AppDelegate * delegate) { mAppDelegate = delegate; }

    /**
     * Open the pairing window using default configured parameters.
     */
    CHIP_ERROR
    OpenBasicCommissioningWindow(
        System::Clock::Seconds16 commissioningTimeout      = System::Clock::Seconds16(CHIP_DEVICE_CONFIG_DISCOVERY_TIMEOUT_SECS),
        CommissioningWindowAdvertisement advertisementMode = chip::CommissioningWindowAdvertisement::kAllSupported);

    CHIP_ERROR OpenEnhancedCommissioningWindow(System::Clock::Seconds16 commissioningTimeout, uint16_t discriminator,
                                               Spake2pVerifier & verifier, uint32_t iterations, chip::ByteSpan salt);

    void CloseCommissioningWindow();

    app::Clusters::AdministratorCommissioning::CommissioningWindowStatus CommissioningWindowStatus() const { return mWindowStatus; }

    // CommissioningModeProvider implemetation.
    Dnssd::CommissioningMode GetCommissioningMode() const override;

    //////////// SessionEstablishmentDelegate Implementation ///////////////
    void OnSessionEstablishmentError(CHIP_ERROR error) override;
    void OnSessionEstablishmentStarted() override;
    void OnSessionEstablished(const SessionHandle & session) override;

    void Shutdown();
    void Cleanup();

    void OnPlatformEvent(const DeviceLayer::ChipDeviceEvent * event);

    // For tests only, allow overriding the spec-defined minimum value of the
    // commissioning window timeout.
    void OverrideMinCommissioningTimeout(System::Clock::Seconds16 timeout) { mMinCommissioningTimeoutOverride.SetValue(timeout); }

private:
    //////////// SessionDelegate Implementation ///////////////
    void OnSessionReleased() override;

    void SetBLE(bool ble) { mIsBLE = ble; }

    CHIP_ERROR SetTemporaryDiscriminator(uint16_t discriminator);

    CHIP_ERROR RestoreDiscriminator();

    CHIP_ERROR StartAdvertisement();

    CHIP_ERROR StopAdvertisement(bool aShuttingDown);

    // Start a timer that will call HandleCommissioningWindowTimeout, and then
    // start advertising and listen for PASE.
    CHIP_ERROR OpenCommissioningWindow(System::Clock::Seconds16 commissioningTimeout);

    // Start advertising and listening for PASE connections.  Should only be
    // called when a commissioning window timeout timer is running.
    CHIP_ERROR AdvertiseAndListenForPASE();

    // Call AdvertiseAndListenForPASE, only if max attempts have not been reached.
    // Cleans up and calls app server delegate on failure.
    // err gives the current error we're attemping to recover from
    void HandleFailedAttempt(CHIP_ERROR err);

    // Helper for Shutdown and Cleanup.  Does not do anything with
    // advertisements, because Shutdown and Cleanup want to handle those
    // differently.
    void ResetState();

    /**
     * Function that gets called when our commissioning window timeout timer
     * fires.
     *
     * This timer is started when a commissioning window is initially opened via
     * OpenEnhancedCommissioningWindow or OpenBasicCommissioningWindow.
     *
     * The timer is canceled when a PASE connection is established, because it
     * should not affect the actual commissioning process, and after a PASE
     * connection is established we will not re-enter commissioning mode without
     * a new call to OpenEnhancedCommissioningWindow or
     * OpenBasicCommissioningWindow.
     */
    static void HandleCommissioningWindowTimeout(chip::System::Layer * aSystemLayer, void * aAppState);

    /**
     * Helper to immediately expire the fail-safe if it's currently armed.
     */
    void ExpireFailSafeIfArmed();

    AppDelegate * mAppDelegate = nullptr;
    Server * mServer           = nullptr;

    app::Clusters::AdministratorCommissioning::CommissioningWindowStatus mWindowStatus =
        app::Clusters::AdministratorCommissioning::CommissioningWindowStatus::kWindowNotOpen;

    bool mIsBLE = true;

    PASESession mPairingSession;

    uint8_t mFailedCommissioningAttempts = 0;

    bool mUseECM = false;
    Spake2pVerifier mECMPASEVerifier;
    uint16_t mECMDiscriminator = 0;
    // mListeningForPASE is true only when we are listening for
    // PBKDFParamRequest messages.
    bool mListeningForPASE = false;
    // Boolean that tracks whether we have a live commissioning timeout timer.
    bool mCommissioningTimeoutTimerArmed = false;
    uint32_t mECMIterations              = 0;
    uint32_t mECMSaltLength              = 0;
    uint8_t mECMSalt[kSpake2p_Max_PBKDF_Salt_Length];

    // For tests only, so that we can test the commissioning window timeout
    // without having to wait 3 minutes.
    Optional<System::Clock::Seconds16> mMinCommissioningTimeoutOverride;

    // The PASE session we are using, so we can handle CloseSession properly.
    SessionHolderWithDelegate mPASESession;
};

} // namespace chip
