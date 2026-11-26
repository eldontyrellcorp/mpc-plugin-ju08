// The performances: 100 programs that set both parts. Each one is an Upper patch (with the effects), a Lower
// patch, the key mode (Dual: both patches on every key; Split: the Lower patch below the split point) and the
// performance settings (part levels, arpeggio, portamento, bender, wheel). Original sounds.
#pragma once
#include "params.h"

namespace j8 {

constexpr int kNumPerformances = 100;

struct Performance {
    char name[24];         // up to 20 characters (the MPC's list shows "P01 " before it)
    Patch upper, lower;        // the effects are the Upper patch's
    uint8_t global[G_COUNT];   // only the ids of kPerformanceGlobals are the performance's
    bool stereoLayers;         // Upper right, Lower left (heard when no effect is on)
};

// The performance settings a performance sets (the others: volume, tune, channels, tempo... stay as they are).
extern const uint8_t kPerformanceGlobals[];
extern const int kNumPerformanceGlobals;

// defaults: the globals a performance starts from (the plugin's defaults)
void buildPerformances(Performance* out, const uint8_t* defaults);

int performanceCount();
const char* performanceWithDuplicate();

// VCA LEVEL of each performance's two parts, from their measured loudness (tests/perf_levels.cpp --table)
extern const uint8_t kPerformanceLevel[kNumPerformances][2];

}  // namespace j8
