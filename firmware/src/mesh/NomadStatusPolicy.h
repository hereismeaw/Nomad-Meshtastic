#pragma once
#include <cstdint>

namespace nomad
{
enum class BluetoothLight : uint8_t { Off, Waiting, Pairing, Connected };

class StatusLights
{
    bool started = false;
    bool bootFinished = false;
    uint32_t bootStart = 0;

  public:
    static constexpr uint32_t BOOT_DURATION_MS = 1400;
    static constexpr uint32_t RED = 0x600000, GREEN = 0x006000, BLUE = 0x000060;
    bool hasStarted() const { return started; }
    bool bootComplete(uint32_t now) const { return bootFinished || (started && uint32_t(now - bootStart) >= BOOT_DURATION_MS); }

    uint32_t color(uint32_t now, BluetoothLight bluetooth, bool healthy, bool transmitting, uint32_t activity)
    {
        if (!started) {
            started = true;
            bootStart = now;
        }
        const uint32_t elapsed = now - bootStart;
        if (!bootFinished && elapsed < BOOT_DURATION_MS) {
            if (elapsed < 400)
                return RED;
            if (elapsed < 800)
                return GREEN;
            if (elapsed < 1200)
                return BLUE;
            return 0;
        }
        bootFinished = true;
        if (bluetooth == BluetoothLight::Pairing)
            return (now / 250) % 2 ? BLUE : 0;
        if (!healthy || transmitting)
            return RED;
        if (activity)
            return activity;
        if (bluetooth == BluetoothLight::Waiting)
            return now % 1000 < 250 ? 0x000040 : 0;
        if (bluetooth == BluetoothLight::Connected)
            return 0x001818;
        return now % 2000 < 150 ? 0x040404 : 0;
    }
};
} // namespace nomad
