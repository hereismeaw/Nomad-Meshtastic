#pragma once
#ifdef RADIOMASTER_NOMAD
#include "LR1121Interface.h"
#include "NomadRadioPolicy.h"
#include "NomadRfFrontend.h"
#include "RadioTxHook.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include <Adafruit_NeoPixel.h>
#include <array>

struct NomadRadioSettings {
    uint8_t version = 1;
    bool enabled = true, rxAllowed = true, txAllowed = true;
    meshtastic_Config_LoRaConfig_ModemPreset preset = meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    uint32_t slot = 0;
    int8_t targetDbm = 0;
    uint8_t role = 0;
};

struct NomadRadioStatistics {
    uint8_t version = 1;
    bool healthy = false, calibrated = false, transmitting = false;
    uint32_t received = 0, bad = 0, sent = 0, dropped = 0, queued = 0;
    float frequencyMhz = 0, snr = 0;
    int32_t rssi = 0;
};

class NomadRadioScheduler : public RadioTxHook
{
  public:
    PreTxAction beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *p) override;
};

class NomadRadioContext : public LR1121Interface
{
    meshtastic_Config_LoRaConfig localConfig = {};
    const nomad::CalibrationStep *calibration = nullptr;
    bool initializationAttempted = false;
    void prepareConfig();
    void logVersion();

  protected:
    meshtastic_Config_LoRaConfig &modemConfig() override;
    const char *modemChannelName() override;
    void limitPower(int8_t maximum) override;
    int16_t launchTransmit(size_t numbytes) override;

  public:
    NomadRadioSettings settings;
    float lastSnr = 0;
    int32_t lastRssi = 0;
    NomadRadioContext(LockingArduinoHal *hal, uint8_t index);
    bool init() override;
    bool reconfigure() override;
    bool healthy = false;
    void configHardwareForSend() override;
    bool canSendImmediately() override;
    ErrorCode send(meshtastic_MeshPacket *p) override;
    void addReceiveMetadata(meshtastic_MeshPacket *p) override;
    void startReceive() override;
    bool configure(const NomadRadioSettings &value);
    NomadRadioStatistics statistics();
    const char *presetName() const;
};

class NomadDualRadioManager : public RadioInterface, private concurrency::OSThread
{
    NomadRadioScheduler scheduler;
    std::array<std::unique_ptr<NomadRadioContext>, 2> radios;
    Adafruit_NeoPixel pixels{2, NOMAD_RGB_PIN, NEO_GRB + NEO_KHZ800};
    uint32_t rxCounts[2] = {}, badCounts[2] = {}, activityMs[2] = {};
    uint32_t lastMaintenance = 0, lastTx = 0, buttonStart = 0;
    uint8_t fanMode = 0;
    bool buttonDown = false, fanRunning = false, longPressHandled = false;
    uint8_t operatingMode = 2;
    nomad::PacketDispatcher dispatcher;
    int32_t runOnce() override;
    NomadRadioContext *primary() const;
    NomadRadioContext *contextForPacket(const meshtastic_MeshPacket *p);

  public:
    explicit NomadDualRadioManager(LockingArduinoHal *hal);
    ~NomadDualRadioManager() override;
    bool received(uint8_t index, const meshtastic_MeshPacket *p);
    bool configureRadio(uint8_t index, const NomadRadioSettings &settings);
    NomadRadioStatistics radioStatistics(uint8_t index);
    void transmitting();
    const char *packetPresetName(const meshtastic_MeshPacket *p);
    int packetOrigin(const meshtastic_MeshPacket *p);
    bool acceptResponse(const meshtastic_MeshPacket *p);
    bool restoreTransmitOrigin(const meshtastic_MeshPacket *p, int radio);
    void prepareTransmit(const meshtastic_MeshPacket *p);
    bool init() override;
    bool reconfigure() override;
    ErrorCode send(meshtastic_MeshPacket *p) override;
    bool wideLora() override { return true; }
    bool canSleep(bool deepSleep = false) override;
    bool sleep() override;
    uint32_t getPacketTime(uint32_t length, bool received = false) override;
    uint32_t getPacketTime(const meshtastic_MeshPacket *p, bool received = false) override;
    uint32_t getRetransmissionMsec(const meshtastic_MeshPacket *p) override;
    meshtastic_QueueStatus getQueueStatus() override;
    bool cancelSending(NodeNum from, PacketId id) override;
    bool findInTxQueue(NodeNum from, PacketId id) override;
    bool removePendingTXPacket(NodeNum from, PacketId id, uint32_t hops) override;
    void clampToLateRebroadcastWindow(NodeNum from, PacketId id) override;
    float getFreq() override;
};

void nomadEarlyInit();
bool nomadReceived(uint8_t index, const meshtastic_MeshPacket *p);
const char *nomadPacketPresetName(const meshtastic_MeshPacket *p);
bool nomadLabEnabled();
void nomadPrepareTransmit(const meshtastic_MeshPacket *p);
bool nomadSameRadio(const meshtastic_MeshPacket *a, const meshtastic_MeshPacket *b);
bool nomadAcceptResponse(const meshtastic_MeshPacket *p);
int nomadPacketOrigin(const meshtastic_MeshPacket *p);
bool nomadRestoreTransmitOrigin(const meshtastic_MeshPacket *p, int radio);
#endif
