// Tests StatusLights in NomadStatusPolicy.h. Startup must visibly exercise each
// color even when initialization was slow or the clock wraps. Bluetooth idle
// indications must not hide radio faults or traffic, and must not leak into RX.
#include "mesh/NomadStatusPolicy.h"
#include <initializer_list>
#ifdef NOMAD_STATUS_STANDALONE
#include <cassert>
#include <iostream>
#define CHECK(x) assert(x)
#else
#include <Arduino.h>
#include <cstdlib>
#include <unity.h>
#define CHECK(x) TEST_ASSERT_TRUE(x)
void setUp() {}
void tearDown() {}
#endif

using nomad::BluetoothLight;
using nomad::StatusLights;

void test_boot_starts_at_first_tick_and_wraps()
{
    for (uint32_t start : {100000U, 0xffffff00U}) {
        StatusLights lights;
        CHECK(!lights.bootComplete(start));
        CHECK(lights.color(start, BluetoothLight::Connected, true, false, 0) == StatusLights::RED);
        CHECK(lights.color(start + 400, BluetoothLight::Off, true, false, 0) == StatusLights::GREEN);
        CHECK(lights.color(start + 800, BluetoothLight::Off, true, false, 0) == StatusLights::BLUE);
        CHECK(lights.color(start + 1200, BluetoothLight::Off, true, false, 0) == 0);
        CHECK(!lights.bootComplete(start + 1399));
        CHECK(lights.bootComplete(start + 1400));
        CHECK(lights.color(start + 1400, BluetoothLight::Connected, true, false, 0) == 0x001818);
        CHECK(lights.color(start, BluetoothLight::Connected, true, false, 0) == 0x001818);
    }
}

void test_bluetooth_and_radio_status_priority()
{
    StatusLights lights;
    lights.color(0, BluetoothLight::Off, true, false, 0);
    CHECK(lights.color(2000, BluetoothLight::Waiting, true, false, 0) != 0);
    CHECK(lights.color(2500, BluetoothLight::Waiting, true, false, 0) == 0);
    CHECK(lights.color(2500, BluetoothLight::Waiting, false, false, 0) == StatusLights::RED);
    CHECK(lights.color(2500, BluetoothLight::Connected, true, true, 0) == StatusLights::RED);
    CHECK(lights.color(2500, BluetoothLight::Connected, true, false, StatusLights::GREEN) == StatusLights::GREEN);
    CHECK(lights.color(2500, BluetoothLight::Connected, true, false, 0) == 0x001818);
    CHECK(lights.color(1750, BluetoothLight::Pairing, true, false, StatusLights::GREEN) == StatusLights::BLUE);
    CHECK(lights.color(2000, BluetoothLight::Pairing, true, false, 0) == 0);
    CHECK(lights.color(2000, BluetoothLight::Off, true, false, 0) != 0);
    CHECK(lights.color(2500, BluetoothLight::Off, true, false, 0) == 0);
}

#ifdef NOMAD_STATUS_STANDALONE
int main()
{
    test_boot_starts_at_first_tick_and_wraps();
    test_bluetooth_and_radio_status_priority();
    std::cout << "Nomad RGB startup/status: PASS\n";
}
#else
void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_boot_starts_at_first_tick_and_wraps);
    RUN_TEST(test_bluetooth_and_radio_status_priority);
    exit(UNITY_END());
}
void loop() {}
#endif
