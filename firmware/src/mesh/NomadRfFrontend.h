#pragma once
#include <cstddef>
#include <cstdint>

namespace nomad
{
struct CalibrationStep {
    uint8_t radio = 0;
    uint32_t frequencyKhz = 0;
    int8_t targetDbm = 0;
    int16_t measuredCentiDbm = 0;
    int8_t driveDbm = 0;
    uint8_t dac = 0;
};

inline bool validCalibration(const CalibrationStep &step, uint8_t radio, uint32_t frequencyKhz, int8_t targetDbm)
{
    const int error = step.measuredCentiDbm - int(targetDbm) * 100;
    return step.radio == radio && step.frequencyKhz == frequencyKhz && frequencyKhz != 0 && step.targetDbm == targetDbm &&
           step.driveDbm >= -17 && step.driveDbm <= 5 && step.dac >= 95 && step.dac <= 120 && error >= -50 && error <= 50;
}

class RfGate
{
    bool enabled = false;

  public:
    void setLocalEnable(bool value) { enabled = value; }
    bool allows(bool calibrated) const { return enabled && calibrated; }
    bool isEnabled() const { return enabled; }
};
} // namespace nomad

// Supply measured initializer entries using an explicit local calibration header.
#ifdef NOMAD_CALIBRATION_HEADER
#include NOMAD_CALIBRATION_HEADER
#endif
#ifndef NOMAD_CALIBRATION_STEPS
#define NOMAD_CALIBRATION_STEPS
#endif
inline constexpr nomad::CalibrationStep NOMAD_MEASURED_STEPS[] = {{}, NOMAD_CALIBRATION_STEPS};
