// Portable maths helpers. No ESP-IDF dependency: also compiles on a PC for host/.
#pragma once
#include <cmath>
#include <cstdint>

#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define J8_HOT IRAM_ATTR
#define J8_DATA DRAM_ATTR
#else
#define J8_HOT
#define J8_DATA
#endif
#define J8_INLINE inline __attribute__((always_inline))

namespace j8 {

constexpr int kBlock = 32;     // samples per control block (1.5 kHz control rate at 48 kHz)
// Voices in each part in 2 PART mode. WHOLE mode gives all of them to one patch.
// Change this one number to 8 for two 8-voice parts, once the board's CPU figures allow it.
#ifndef J8_VOICES_PER_PART
#define J8_VOICES_PER_PART 4
#endif
constexpr int kVoicesPerPart = J8_VOICES_PER_PART;
constexpr int kNumVoices = 2 * kVoicesPerPart;
#ifndef J8_FILTER_OS
#define J8_FILTER_OS 1         // internal filter oversampling, 1 or 2
#endif
constexpr int kFilterOS = J8_FILTER_OS;
constexpr float kPi = 3.14159265358979f;

J8_INLINE float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
J8_INLINE float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// 2^x, exact at integers, max error about 0.0005 cent (fitted, checked in host tests).
J8_INLINE float pow2f(float x) {
    x = clampf(x, -30.f, 30.f);
    int i = (int)(x + 1024.f) - 1024;
    float f = x - (float)i;
    float q = -0.30685115f + f * (-0.06667901f + f * (-0.01086463f + f * -0.00189782f));
    float m = 1.f + f + f * (1.f - f) * q;
    union { float v; uint32_t u; } s;
    s.u = (uint32_t)(i + 127) << 23;
    return m * s.v;
}

// Tables, built once by initTables(). Kept in internal RAM on the ESP32-S3.
constexpr int kSatSize = 512;
constexpr float kSatRange = 4.f;
constexpr int kTanSize = 512;
extern float gSatTable[kSatSize + 2];
extern float gTanTable[kTanSize + 2];
void initTables();

// tanh(x) by table. OTA stages saturate on the input difference as a tanh, so this
// is the exact curve, just cheap.
J8_INLINE float sat(float x) {
    if (x >= kSatRange) return 1.f;
    if (x <= -kSatRange) return -1.f;
    float p = (x + kSatRange) * ((float)kSatSize / (2.f * kSatRange));
    int i = (int)p;
    float f = p - (float)i;
    return gSatTable[i] + (gSatTable[i + 1] - gSatTable[i]) * f;
}

// tan(pi * r) for r = fc / fs in [0, 0.5).
J8_INLINE float tanPi(float r) {
    float p = clampf(r, 0.f, 0.499f) * (2.f * (float)kTanSize);
    int i = (int)p;
    float f = p - (float)i;
    return gTanTable[i] + (gTanTable[i + 1] - gTanTable[i]) * f;
}

// sin(2*pi*p) for p in [0, 1), used for control-rate LFOs.
J8_INLINE float sinTurn(float p) {
    float x = p < 0.5f ? p * 4.f - 1.f : 3.f - p * 4.f;  // triangle, +-1
    float x2 = x * x;
    return -x * (1.5707963f + x2 * (-0.6459640f + x2 * (0.0796926f - 0.0046768f * x2)));
}

struct Rng {
    uint32_t s = 0x9E3779B9u;
    J8_INLINE uint32_t next() {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return s;
    }
    J8_INLINE float bipolar() { return (float)(int32_t)next() * (1.f / 2147483648.f); }
};

}  // namespace j8
