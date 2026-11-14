// Effect chain: voices -> CHORUS & MFX -> dry + delay + reverb.
// Reverb runs on the second core, one block behind the send (0.67 ms; unnoticeable).
#pragma once
#include "dsp_math.h"
#include "fx_params.h"

namespace j8 {

// Delay memory, 16-bit. Reads interpolate linearly.
struct Line {
    int16_t* b = nullptr;
    int32_t n = 0, w = 0;
    J8_INLINE void push(float x) {
        b[w] = (int16_t)(clampf(x, -1.f, 1.f) * 32767.f);
        if (++w == n) w = 0;
    }
    // d = samples behind the sample just written, 0 = newest
    J8_INLINE float at(int32_t d) const {
        int32_t i = w - 1 - d;
        if (i < 0) i += n;
        return (float)b[i];
    }
    J8_INLINE float read(float d) const {
        d = clampf(d, 0.f, (float)(n - 3));
        int32_t di = (int32_t)d;
        float f = d - (float)di;
        float a = at(di);
        return (a + (at(di + 1) - a) * f) * (1.f / 32767.f);
    }
};

struct Tone {               // low and high shelves from one-pole splits
    float zl = 0, zh = 0;
    J8_INLINE float tick(float x, float lo, float hi, float aL, float aH) {
        zl += (x - zl) * aL;
        zh += (x - zh) * aH;
        return x + zl * (lo - 1.f) + (x - zh) * (hi - 1.f);
    }
};

struct PitchHead {          // two crossfading read heads
    float phase = 0;
    J8_INLINE float tick(const Line& l, float base, float win, float inc) {
        phase += inc;
        if (phase >= 1.f) phase -= 1.f;
        if (phase < 0.f) phase += 1.f;
        float p2 = phase + 0.5f;
        if (p2 >= 1.f) p2 -= 1.f;
        float g1 = 1.f - 2.f * fabsf(phase - 0.5f);
        float g2 = 1.f - 2.f * fabsf(p2 - 0.5f);
        return l.read(base + phase * win) * g1 + l.read(base + p2 * win) * g2;
    }
};

struct Svf {                // TPT state-variable stage
    float s1 = 0, s2 = 0;
};

class Fx {
public:
    // Buffers come from the caller: PSRAM on the board, plain memory on a PC.
    // mfxLen and dlyLen are samples per channel; reverb memory is revLen samples.
    void init(float fs, int16_t* mfxL, int16_t* mfxR, int32_t mfxLen,
              int16_t* dlyL, int16_t* dlyR, int32_t dlyLen, int16_t* rev, int32_t revLen);

    void control(const FxPatch& p, float bpm);                 // once per block, audio task
    void reverbBlock();                                        // second core
    // In: mono block. Out: stereo block, including delay and reverb returns.
    void process(const float* in, float* L, float* R);

    int32_t maxMfxMs() const { return (int32_t)((float)mL_.n * 1000.f / fs_); }
    int32_t maxDelayMs() const { return (int32_t)((float)dL_.n * 1000.f / fs_); }

private:
    float fs_ = 48000, invFs_ = 1.f / 48000, bpm_ = 120;
    FxPatch p_{};
    int16_t mRun_[kMfxMaxParams] = {0};
    Line mL_, mR_, dL_, dR_;
    Rng rng_;

    // CHORUS & MFX
    int curType_ = -1;
    int32_t clearPos_ = -1;
    float mfxMix_ = 0;          // click-free on/off and type change
    Tone toneL_, toneR_;
    float lp1_ = 0, lp2_ = 0, hp1_ = 0;
    float lfo1_ = 0, lfo2_ = 0;
    float env_ = 0, hold_ = 0, holdCnt_ = 0;
    float timeL_ = 0, timeR_ = 0;
    float dampL_ = 0, dampR_ = 0;
    float fbL_ = 0, fbR_ = 0;
    float apState_[24] = {0};
    float apCoef_ = 0, phFb_ = 0;
    float modSmooth_ = 0;
    Svf svf_[4];
    PitchHead headA_, headB_;
    float rPhase_ = 0;
    float smGain1_ = 0;
    void resetMfx();
    void runMfx(const float* in, float* L, float* R);

    // Delay
    float dlySend_ = 0, dlyTime_ = 0, dlyDampL_ = 0;
    int32_t dlyTail_ = 0, dlyClear_ = -1;
    float send_[kBlock];

    // Reverb, runs at half rate
    float revMix_ = 0;
    int32_t revTail_ = 0;
    int16_t* rv_ = nullptr;
    int32_t rvLen_ = 0;
    float rvIn_[kBlock];        // send captured last block
    float rvOutL_[kBlock], rvOutR_[kBlock];
    float rvLastL_ = 0, rvLastR_ = 0;
    struct RvLine { int32_t off, len, w; };
    RvLine rvPre_, rvDiff_[4], rvTank_[8], rvPs1_, rvPs2_;
    float rvLfo_ = 0, rvDamp1_ = 0, rvDamp2_ = 0, rvBw_ = 0, rvLowA_ = 0, rvLowB_ = 0;
    float rvTail1_ = 0, rvTail2_ = 0;
    float rvPs1Out_ = 0, rvPs2Out_ = 0;
    float rvPh1_ = 0, rvPh2_ = 0;
    float rvScale_ = 0;
    int rvBuiltType_ = -1;
    void buildReverb(float size);
    J8_INLINE float rvRd(const RvLine& l, int32_t d) const {
        int32_t i = l.w - d; if (i < 0) i += l.len;
        return (float)rv_[l.off + i] * (1.f / 32767.f);
    }
    J8_INLINE void rvWr(RvLine& l, float x) {
        rv_[l.off + l.w] = (int16_t)(clampf(x, -1.f, 1.f) * 32767.f);
        if (++l.w == l.len) l.w = 0;
    }
};

}  // namespace j8
