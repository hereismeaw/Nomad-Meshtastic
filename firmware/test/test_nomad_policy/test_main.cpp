// Tests PacketOrigins and fanRequired in src/mesh/NomadRadioPolicy.h.
// A frame first seen on one PHY must never be relayed onto the other PHY,
// including across the 32-bit clock wrap. Fan override must not defeat post-TX
// cooling. These guard cross-preset loops and loss of cooling when the operator
// selects OFF.
#include "mesh/NomadRadioPolicy.h"
#include "mesh/NomadRfFrontend.h"
#ifdef NOMAD_POLICY_STANDALONE
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

void test_nomad_origin_and_cooling_policy()
{
    nomad::PacketDispatcher dispatcher;
    CHECK(dispatcher.sent(10, 42, 0, 100));
    CHECK(dispatcher.receive(20, 81, 1, 101));
    CHECK(!dispatcher.responseAllowed(10, 42, 20, 81, true, 102));
    CHECK(!dispatcher.receive(10, 42, 1, 102));
    for (unsigned i = 0; i < 1000; ++i)
        CHECK(dispatcher.receive(30, i, 1, 103));
    CHECK(dispatcher.find(10, 42, 104) == 0);
    CHECK(dispatcher.receive(20, 82, 0, 105));
    CHECK(dispatcher.responseAllowed(10, 42, 20, 82, true, 106));
    CHECK(!dispatcher.responseAllowed(10, 999, 20, 82, true, 106));
    CHECK(dispatcher.responseAllowed(10, 999, 20, 82, false, 106));
    nomad::RfGate gate;
    CHECK(!gate.allows(false));
    CHECK(!gate.allows(true));
    gate.setLocalEnable(true);
    CHECK(!gate.allows(false));
    CHECK(gate.allows(true));
    gate.setLocalEnable(false);
    CHECK(!gate.allows(true));
    // Synthetic test-only measurements must never become production calibration entries.
    nomad::CalibrationStep step{0, 923125, 30, 3000, 5, 95};
    CHECK(nomad::validCalibration(step, 0, 923125, 30));
    CHECK(!nomad::validCalibration(step, 1, 923125, 30));
    CHECK(!nomad::validCalibration(step, 0, 923375, 30));
    CHECK(!nomad::validCalibration(step, 0, 923125, 27));
    step.measuredCentiDbm = 2800;
    CHECK(!nomad::validCalibration(step, 0, 923125, 30));
    CHECK(!nomad::validCalibration({}, 0, 0, 0));
    nomad::PacketOrigins origins;
    CHECK(origins.find(1, 10, 0) == -1);
    CHECK(origins.receive(1, 10, 0, 100));
    CHECK(origins.receive(2, 10, 1, 100));
    CHECK(origins.find(1, 10, 110) == 0);
    CHECK(origins.find(2, 10, 110) == 1);
    CHECK(!origins.receive(1, 10, 1, 120));
    CHECK(origins.receive(1, 10, 0, 120));
    CHECK(origins.find(1, 10, 600100) == -1);
    nomad::PacketOrigins wrap;
    CHECK(wrap.receive(5, 99, 1, 0xffffff00));
    CHECK(wrap.find(5, 99, 100) == 1);
    CHECK(!wrap.receive(5, 99, 0, 100));
    for (unsigned i = 0; i < 1000; ++i)
        CHECK(wrap.receive(8, i, i % 2, i));
    CHECK(wrap.find(8, 999, 1000) == 1);
    CHECK(wrap.find(8, 0, 1000) == -1);
    CHECK(!nomad::fanRequired(0, false, false, 0));
    CHECK(nomad::fanRequired(1, false, false, 0));
    CHECK(nomad::fanRequired(2, true, false, 0));
    CHECK(nomad::fanRequired(2, false, true, 59999));
    CHECK(!nomad::fanRequired(2, false, true, 60000));
}

#ifdef NOMAD_POLICY_STANDALONE
int main()
{
    test_nomad_origin_and_cooling_policy();
    std::cout << "Nomad origin and cooling policy: PASS\n";
    return 0;
}
#else
void setup()
{
    UNITY_BEGIN();
    RUN_TEST(test_nomad_origin_and_cooling_policy);
    exit(UNITY_END());
}
void loop() {}
#endif
