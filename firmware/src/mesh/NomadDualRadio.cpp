#include "configuration.h"
#ifdef RADIOMASTER_NOMAD
#include "BluetoothStatus.h"
#include "Channels.h"
#include "DisplayFormatters.h"
#include "MeshModule.h"
#include "NodeDB.h"
#include "NomadDualRadio.h"
#include "Throttle.h"
#include "UptimeClock.h"
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
#include "main.h"
#endif
#include <Preferences.h>

static NomadDualRadioManager *manager;
static nomad::RfGate rfGate;

int nomadPacketOrigin(const meshtastic_MeshPacket *p)
{
    return manager ? manager->packetOrigin(p) : -1;
}

bool nomadRestoreTransmitOrigin(const meshtastic_MeshPacket *p, int radio)
{
    return manager && manager->restoreTransmitOrigin(p, radio);
}

bool NomadDualRadioManager::restoreTransmitOrigin(const meshtastic_MeshPacket *p, int radio)
{
    return radio >= 0 && radio < 2 && dispatcher.sent(getFrom(p), p->id, radio, Time::getMillis());
}

bool nomadSameRadio(const meshtastic_MeshPacket *a, const meshtastic_MeshPacket *b)
{
    if (!manager)
        return false;
    const int origin = manager->packetOrigin(a);
    return origin >= 0 && origin == manager->packetOrigin(b);
}

bool nomadAcceptResponse(const meshtastic_MeshPacket *p)
{
    return manager && manager->acceptResponse(p);
}

int NomadDualRadioManager::packetOrigin(const meshtastic_MeshPacket *p)
{
    const auto now = Time::getMillis();
    return dispatcher.find(getFrom(p), p->id, now);
}

bool NomadDualRadioManager::acceptResponse(const meshtastic_MeshPacket *p)
{
    if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag || !p->decoded.request_id)
        return true;
    const auto now = Time::getMillis();
    return dispatcher.responseAllowed(p->to, p->decoded.request_id, p->from, p->id, isToUs(p), now);
}

RadioTxHook::PreTxAction NomadRadioScheduler::beforeTransmit(RadioInterface *iface, meshtastic_MeshPacket *)
{
    for (uint8_t i = 0; i < 2; ++i)
        if (iface == RadioLibInterface::nomadInstances[i]) {
            auto other = RadioLibInterface::nomadInstances[1 - i];
            if (!rfGate.isEnabled() || (other && other->isSending()))
                return PRETX_DEFER;
        }
    return PRETX_SEND;
}

void nomadPrepareTransmit(const meshtastic_MeshPacket *p)
{
    if (manager)
        manager->prepareTransmit(p);
}

void NomadDualRadioManager::prepareTransmit(const meshtastic_MeshPacket *p)
{
    const auto now = Time::getMillis();
    if (packetOrigin(p) >= 0 || !isFromUs(p))
        return;
    int origin = -1;
    const bool response = p->which_payload_variant == meshtastic_MeshPacket_decoded_tag && p->decoded.request_id;
    if (response)
        origin = dispatcher.find(p->to, p->decoded.request_id, now);
    const auto request = MeshModule::getCurrentRequest();
    if (origin < 0 && response && request && request->id == p->decoded.request_id)
        origin = dispatcher.find(request->from, request->id, now);
    if (origin < 0 && response)
        return;
    if (origin < 0 && primary())
        origin = primary()->nomadIndex;
    if (origin >= 0)
        dispatcher.sent(getFrom(p), p->id, origin, now);
}

bool nomadLabEnabled()
{
#ifdef NOMAD_RF_LAB
    return rfGate.isEnabled();
#else
    return false;
#endif
}

void NomadRadioContext::limitPower(int8_t maximum)
{
    RadioInterface::limitPower(maximum);
    int8_t target = localConfig.tx_power;
#ifdef NOMAD_RF_LAB
    target = settings.targetDbm ? settings.targetDbm : 30;
#endif
    calibration = nullptr;
    const auto frequencyKhz = uint32_t(getFreq() * 1000.0f + 0.5f);
    for (const auto &step : NOMAD_MEASURED_STEPS)
        if (nomad::validCalibration(step, nomadIndex, frequencyKhz, target)) {
            calibration = &step;
            power = step.driveDbm;
            break;
        }
    if (!calibration)
        power = -17;
}

void NomadRadioContext::configHardwareForSend()
{
    if (rfGate.allows(calibration != nullptr)) {
        dacWrite(NOMAD_APC2_PIN, calibration->dac);
        digitalWrite(NOMAD_FAN_PIN, HIGH);
        if (manager)
            manager->transmitting();
    }
    LR1121Interface::configHardwareForSend();
}

int16_t NomadRadioContext::launchTransmit(size_t numbytes)
{
    auto other = nomadInstances[1 - nomadIndex];
    if (!rfGate.allows(calibration != nullptr) || (other && other->isSending()))
        return RADIOLIB_ERR_INVALID_OUTPUT_POWER;
    return LR1121Interface::launchTransmit(numbytes);
}
const char *nomadPacketPresetName(const meshtastic_MeshPacket *p)
{
    return manager ? manager->packetPresetName(p) : nullptr;
}
const char *NomadDualRadioManager::packetPresetName(const meshtastic_MeshPacket *p)
{
    const int origin = packetOrigin(p);
    return origin < 0 ? nullptr : radios[origin]->presetName();
}

bool nomadReceived(uint8_t index, const meshtastic_MeshPacket *p)
{
    return manager && manager->received(index, p);
}

void nomadEarlyInit()
{
    for (int reset : {15, 21}) {
        digitalWrite(reset, LOW);
        pinMode(reset, OUTPUT);
    }
    digitalWrite(NOMAD_FAN_PIN, LOW);
    pinMode(NOMAD_FAN_PIN, OUTPUT);
    // APC2 is an analogue bias, not a verified PA shutdown signal. TX stays
    // software inhibited.
    dacWrite(NOMAD_APC2_PIN, 120);
    pinMode(NOMAD_BUTTON_B, INPUT_PULLUP);
    for (int cs : {27, 13}) {
        digitalWrite(cs, HIGH);
        pinMode(cs, OUTPUT);
    }
}

NomadRadioContext::NomadRadioContext(LockingArduinoHal *hal, uint8_t index)
    : LR1121Interface(hal, index ? 13 : 27, index ? 34 : 37, index ? 21 : 15, index ? 39 : 36)
{
    settings.preset =
        index ? meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST : meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
#ifdef NOMAD_RF_LAB
    settings.targetDbm = 30;
#endif
    Preferences prefs;
    if (prefs.begin(index ? "nomad-b-v1" : "nomad-a-v1", true)) {
        if (prefs.getBytesLength("settings") == sizeof(settings)) {
            NomadRadioSettings saved;
            if (prefs.getBytes("settings", &saved, sizeof(saved)) == sizeof(saved) && saved.version == 1 &&
                saved.preset >= _meshtastic_Config_LoRaConfig_ModemPreset_MIN &&
                saved.preset <= _meshtastic_Config_LoRaConfig_ModemPreset_MAX && saved.targetDbm >= -20 && saved.targetDbm <= 30)
                settings = saved;
        }
        prefs.end();
    }
}

meshtastic_Config_LoRaConfig &NomadRadioContext::modemConfig()
{
    return localConfig;
}

void NomadRadioContext::prepareConfig()
{
    localConfig = config.lora;
    localConfig.use_preset = true;
    localConfig.modem_preset = settings.preset;
    localConfig.channel_num = settings.slot;
    localConfig.override_frequency = 0;
    localConfig.coding_rate = 0;
    localConfig.tx_enabled = localConfig.tx_enabled && settings.txAllowed;
    if (settings.targetDbm)
        localConfig.tx_power = settings.targetDbm;
}

bool NomadRadioContext::reconfigure()
{
    disabled = false;
    rfGate.setLocalEnable(false);
    prepareConfig();
    if (!settings.enabled || (settings.slot && settings.slot > frequencySlotCount(localConfig))) {
        healthy = false;
        sleep();
        return false;
    }
    const bool configured = LR1121Interface::reconfigure();
    logVersion();
    healthy = configured && localConfig.modem_preset == settings.preset;
    if (configured && !healthy)
        sleep();
    return healthy;
}

bool NomadRadioContext::init()
{
    disabled = false;
    if (initializationAttempted)
        return reconfigure();
    prepareConfig();
    if (!settings.enabled || (settings.slot && settings.slot > frequencySlotCount(localConfig)))
        return false;
    initializationAttempted = true;
    const bool initialized = LR1121Interface::init();
    logVersion();
    if (initialized && localConfig.modem_preset != settings.preset)
        sleep();
    return initialized && localConfig.modem_preset == settings.preset;
}

void NomadRadioContext::logVersion()
{
    LR11x0VersionInfo_t version{};
    const int status = lora.getVersionInfo(&version);
    LOG_INFO("Nomad %c version status=%d device=%u hw=%u fw=%u.%u BUSY=%d RESET=%d", 'A' + nomadIndex, status, version.device,
             version.hardware, version.fwMajor, version.fwMinor, digitalRead(nomadIndex ? 39 : 36),
             digitalRead(nomadIndex ? 21 : 15));
}

const char *NomadRadioContext::presetName() const
{
    return DisplayFormatters::getModemPresetDisplayName(settings.preset, false, true);
}

const char *NomadRadioContext::modemChannelName()
{
    const auto &channel = channels.getByIndex(channels.getPrimaryIndex());
    if (channel.settings.name[0])
        return channel.settings.name;
    // Unnamed stock channels hash their PHY preset name on unmodified Meshtastic
    // peers.
    return presetName();
}

bool NomadRadioContext::canSendImmediately()
{
    auto other = nomadInstances[1 - nomadIndex];
    return rfGate.allows(calibration != nullptr) && (!other || !other->isSending()) && LR1121Interface::canSendImmediately();
}

ErrorCode NomadRadioContext::send(meshtastic_MeshPacket *p)
{
    if (!settings.enabled || !settings.txAllowed || !rfGate.allows(calibration != nullptr)) {
        packetPool.release(p);
        return ERRNO_DISABLED;
    }
    return LR1121Interface::send(p);
}

NomadDualRadioManager::NomadDualRadioManager(LockingArduinoHal *hal) : OSThread("NomadIO", 25)
{
    manager = this;
    radios[0] = std::make_unique<NomadRadioContext>(hal, 0);
    radios[1] = std::make_unique<NomadRadioContext>(hal, 1);
    pixels.begin();
    pixels.clear();
    pixels.show();
    Preferences prefs;
    if (prefs.begin("nomad-v1", false)) {
        fanMode = prefs.getUChar("fan", 0);
        operatingMode = prefs.getUChar("mode", 2);
        if (operatingMode > 2)
            operatingMode = 2;
        if (fanMode > 2)
            fanMode = 0;
#ifdef NOMAD_RF_LAB
        prefs.putChar("target-a", 30);
        prefs.putChar("target-b", 30);
#endif
        prefs.end();
    }
}

NomadRadioContext *NomadDualRadioManager::primary() const
{
    for (auto &radio : radios)
        if (radio->healthy && radio->settings.enabled)
            return radio.get();
    return nullptr;
}

NomadDualRadioManager::~NomadDualRadioManager()
{
    rfGate.setLocalEnable(false);
    if (manager == this)
        manager = nullptr;
    for (auto &radio : radios)
        radio->disableInterrupt();
}

bool NomadDualRadioManager::init()
{
    for (auto &radio : radios) {
        if (operatingMode < 2 && radio->nomadIndex != operatingMode)
            continue;
        radio->healthy = radio->init();
        LOG_INFO("Nomad radio %c: %s, RX only pending conducted PA calibration", 'A' + radio->nomadIndex,
                 radio->healthy ? "detected" : "failed");
    }
    for (auto &radio : radios)
        if (!radio->healthy && radio->settings.enabled && (operatingMode == 2 || operatingMode == radio->nomadIndex)) {
            radio->healthy = radio->init();
            LOG_INFO("Nomad radio %c bounded startup retry: %s", 'A' + radio->nomadIndex,
                     radio->healthy ? "recovered" : "failed");
        }
    RadioLibInterface::instance = primary();
    return primary() != nullptr;
}

bool NomadDualRadioManager::received(uint8_t index, const meshtastic_MeshPacket *p)
{
    return dispatcher.receive(p->from, p->id, index, Time::getMillis());
}

void NomadDualRadioManager::transmitting()
{
    lastTx = Time::skipZero(Time::getMillis());
    fanRunning = true;
}

bool NomadDualRadioManager::reconfigure()
{
    bool ok = false;
    for (auto &radio : radios)
        if (radio->healthy)
            ok |= radio->reconfigure();
    return ok;
}

int32_t NomadDualRadioManager::runOnce()
{
    const uint32_t now = Time::getMillis();
    nomad::BluetoothLight bluetooth = nomad::BluetoothLight::Off;
#if !MESHTASTIC_EXCLUDE_BLUETOOTH
    if (config.bluetooth.enabled && nimbleBluetooth && nimbleBluetooth->isActive() && !nimbleBluetooth->isDeInit) {
        bluetooth = nomad::BluetoothLight::Waiting;
        if (bluetoothStatus) {
            const auto state = bluetoothStatus->getConnectionState();
            if (state == meshtastic::BluetoothStatus::ConnectionState::PAIRING)
                bluetooth = nomad::BluetoothLight::Pairing;
            else if (state == meshtastic::BluetoothStatus::ConnectionState::CONNECTED)
                bluetooth = nomad::BluetoothLight::Connected;
        }
    }
#endif
    if (bluetooth != lastBluetoothLight) {
        static const char *names[] = {"off", "waiting for connection", "pairing", "connected"};
        LOG_INFO("Nomad RGB Bluetooth: %s", names[static_cast<uint8_t>(bluetooth)]);
        lastBluetoothLight = bluetooth;
    }
    if (!statusLights.hasStarted())
        LOG_INFO("Nomad RGB self-test: 2 GRB pixels on GPIO22, red/green/blue 400ms each");
    bool transmitting = false;
    bool pixelsChanged = false;
    for (uint8_t i = 0; i < 2; ++i) {
        auto &radio = radios[i];
        if (radio->healthy && !radio->statistics().healthy)
            radio->healthy = false;
        uint32_t activity = 0;
        if (radio->healthy) {
            radio->pollMissedIrqs();
            if (radio->rxGood != rxCounts[i] || radio->rxBad != badCounts[i]) {
                activityMs[i] = now;
                activityColors[i] = radio->rxBad != badCounts[i] ? nomad::StatusLights::RED : nomad::StatusLights::GREEN;
                rxCounts[i] = radio->rxGood;
                badCounts[i] = radio->rxBad;
            }
            if (activityColors[i]) {
                if (Throttle::isWithinTimespanMs(activityMs[i], 150))
                    activity = activityColors[i];
                else
                    activityColors[i] = 0;
            }
        }
        const bool sending = radio->healthy && radio->isSending();
        transmitting |= sending;
        const auto color = statusLights.color(now, bluetooth, radio->healthy, sending, activity);
        if (pixels.getPixelColor(i) != color) {
            pixels.setPixelColor(i, color);
            pixelsChanged = true;
        }
    }
    if (pixelsChanged)
        pixels.show();
    if (!bootLightsLogged && statusLights.bootComplete(now)) {
        LOG_INFO("Nomad RGB self-test complete; waiting=slow blue, pairing=fast blue, connected=cyan");
        bootLightsLogged = true;
    }
    RadioLibInterface::instance = primary();
    if (transmitting)
        lastTx = Time::skipZero(now);
    const bool pressed = digitalRead(NOMAD_BUTTON_B) == LOW;
    if (pressed && !buttonDown) {
        buttonStart = now;
        longPressHandled = false;
    }
    if (pressed && !longPressHandled && Throttle::hasElapsed(buttonStart, 3000)) {
        rfGate.setLocalEnable(!rfGate.isEnabled());
        longPressHandled = true;
        LOG_WARN("Nomad local RF gate %s; only measured calibration steps can transmit",
                 rfGate.isEnabled() ? "enabled" : "disabled");
    }
    if (!pressed && buttonDown && !longPressHandled && Throttle::hasElapsed(buttonStart, 50)) {
        fanMode = (fanMode + 1) % 3;
        Preferences prefs;
        if (prefs.begin("nomad-v1", false)) {
            prefs.putUChar("fan", fanMode);
            prefs.end();
        }
        LOG_INFO("Nomad fan mode: %s", fanMode == 0 ? "AUTO" : fanMode == 1 ? "ON" : "OFF");
    }
    buttonDown = pressed;
    const bool fan = nomad::fanRequired(fanMode, transmitting, lastTx != 0, uint32_t(now - lastTx));
    if (fan != fanRunning) {
        digitalWrite(NOMAD_FAN_PIN, fan ? HIGH : LOW);
        fanRunning = fan;
    }
    if (Throttle::hasElapsed(lastMaintenance, AGC_RESET_INTERVAL_MS)) {
        lastMaintenance = now;
        for (auto &radio : radios)
            if (radio->healthy && radio.get() != RadioLibInterface::instance) {
                radio->updateNoiseFloor();
                radio->periodicRadioMaintenance();
            }
    }
    return 25;
}

ErrorCode NomadDualRadioManager::send(meshtastic_MeshPacket *p)
{
    const uint32_t now = Time::getMillis();
    int origin = packetOrigin(p);
    if (origin < 0 && p->which_payload_variant == meshtastic_MeshPacket_decoded_tag && p->decoded.request_id)
        origin = dispatcher.find(p->to, p->decoded.request_id, now);
    if (origin >= 0) {
        if (radios[origin]->healthy) {
            dispatcher.sent(getFrom(p), p->id, origin, now);
            return radios[origin]->send(p);
        }
        packetPool.release(p);
        return ERRNO_DISABLED;
    }
    packetPool.release(p);
    return ERRNO_DISABLED;
}
uint32_t NomadDualRadioManager::getPacketTime(uint32_t length, bool received)
{
    return primary() ? static_cast<RadioInterface *>(primary())->getPacketTime(length, received) : 0;
}

NomadRadioContext *NomadDualRadioManager::contextForPacket(const meshtastic_MeshPacket *p)
{
    const int origin = packetOrigin(p);
    return origin < 0 ? primary() : radios[origin].get();
}

uint32_t NomadDualRadioManager::getPacketTime(const meshtastic_MeshPacket *p, bool received)
{
    RadioInterface *radio = contextForPacket(p);
    return radio ? radio->getPacketTime(p, received) : 0;
}

uint32_t NomadDualRadioManager::getRetransmissionMsec(const meshtastic_MeshPacket *p)
{
    auto radio = contextForPacket(p);
    return radio ? radio->getRetransmissionMsec(p) : 0;
}
float NomadDualRadioManager::getFreq()
{
    return primary() ? primary()->getFreq() : 0;
}
meshtastic_QueueStatus NomadDualRadioManager::getQueueStatus()
{
    return primary() ? static_cast<RadioInterface *>(primary())->getQueueStatus() : meshtastic_QueueStatus{};
}
bool NomadDualRadioManager::canSleep(bool deepSleep)
{
    for (auto &radio : radios)
        if (radio->healthy && !radio->canSleep(deepSleep))
            return false;
    return true;
}
bool NomadDualRadioManager::sleep()
{
    rfGate.setLocalEnable(false);
    bool ok = true;
    for (auto &radio : radios)
        if (radio->healthy)
            ok &= radio->sleep();
    digitalWrite(NOMAD_FAN_PIN, LOW);
    pixels.clear();
    pixels.show();
    return ok;
}
bool NomadDualRadioManager::cancelSending(NodeNum from, PacketId id)
{
    bool found = false;
    for (auto &radio : radios)
        found |= radio->cancelSending(from, id);
    return found;
}
bool NomadDualRadioManager::findInTxQueue(NodeNum from, PacketId id)
{
    for (auto &radio : radios)
        if (radio->findInTxQueue(from, id))
            return true;
    return false;
}
bool NomadDualRadioManager::removePendingTXPacket(NodeNum from, PacketId id, uint32_t hops)
{
    bool found = false;
    for (auto &radio : radios)
        found |= static_cast<RadioInterface *>(radio.get())->removePendingTXPacket(from, id, hops);
    return found;
}
void NomadDualRadioManager::clampToLateRebroadcastWindow(NodeNum from, PacketId id)
{
    for (auto &radio : radios)
        static_cast<RadioInterface *>(radio.get())->clampToLateRebroadcastWindow(from, id);
}
#endif

#ifdef RADIOMASTER_NOMAD
void NomadRadioContext::addReceiveMetadata(meshtastic_MeshPacket *p)
{
    LR1121Interface::addReceiveMetadata(p);
    lastRssi = p->rx_rssi;
    lastSnr = p->rx_snr;
}

void NomadRadioContext::startReceive()
{
    if (!settings.rxAllowed) {
        setStandby();
        return;
    }
    LR1121Interface::startReceive();
}

bool NomadRadioContext::configure(const NomadRadioSettings &value)
{
    if (value.version != 1 || value.preset < _meshtastic_Config_LoRaConfig_ModemPreset_MIN ||
        value.preset > _meshtastic_Config_LoRaConfig_ModemPreset_MAX || value.targetDbm < -20 || value.targetDbm > 30)
        return false;
    auto candidate = config.lora;
    candidate.use_preset = true;
    candidate.modem_preset = value.preset;
    candidate.channel_num = value.slot;
    if (!checkConfigRegion(candidate) || (value.slot && value.slot > frequencySlotCount(candidate)))
        return false;
    Preferences prefs;
    if (!prefs.begin(nomadIndex ? "nomad-b-v1" : "nomad-a-v1", false))
        return false;
    const bool persisted = prefs.putBytes("settings", &value, sizeof(value)) == sizeof(value);
    prefs.end();
    if (!persisted)
        return false;
    rfGate.setLocalEnable(false);
    settings = value;
    if (healthy)
        healthy = reconfigure();
    else
        healthy = init();
    return true;
}

NomadRadioStatistics NomadRadioContext::statistics()
{
    NomadRadioStatistics result;
    result.healthy = healthy && !disabled;
    result.calibrated = calibration != nullptr;
    result.transmitting = isSending();
    result.received = rxGood;
    result.bad = rxBad;
    result.sent = txGood;
    result.dropped = txDrop;
    result.queued = packetsInTxQueue();
    result.frequencyMhz = healthy ? getFreq() : 0;
    result.rssi = lastRssi;
    result.snr = lastSnr;
    return result;
}

bool NomadDualRadioManager::configureRadio(uint8_t index, const NomadRadioSettings &settings)
{
    return index < radios.size() && radios[index]->configure(settings);
}

NomadRadioStatistics NomadDualRadioManager::radioStatistics(uint8_t index)
{
    return index < radios.size() ? radios[index]->statistics() : NomadRadioStatistics{};
}
#endif
