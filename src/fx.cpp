// The Arduino IDE has no per-file compiler flags, so the speed settings live here.
#pragma GCC optimize("O2", "fast-math")
// Effect algorithms. The parameter set follows Roland's manual; the DSP is my own model
// of what each effect is described as doing. Constants marked "estimate" have no source.
#include "fx.h"
#include <cstring>

namespace j8 {

J8_INLINE float dbGain(int db) { return pow2f((float)db * 0.1660964f); }
J8_INLINE float onePoleA(float hz, float invFs) {    // 1 - exp(-2 pi f / fs)
    return 1.f - pow2f(-9.0647203f * hz * invFs);
}
J8_INLINE float dampA(int idx, float invFs) {       // feedback high cut; last value is bypass
    static const float t[17] = {200,250,315,400,500,630,800,1000,1250,1600,2000,2500,3150,4000,5000,6300,8000};
    return idx >= 17 ? 1.f : onePoleA(t[idx < 0 ? 0 : idx], invFs);
}
J8_INLINE void panGains(int pan, float& gl, float& gr) {
    float pos = ((float)pan + 64.f) / 127.f;     // 0 left, 1 right
    gl = sinTurn(pos * 0.25f);                   // sinTurn(p) follows cos(2 pi p)
    gr = sinTurn((1.f - pos) * 0.25f);
}
J8_INLINE float triFrom(float p) { return p < 0.5f ? 4.f * p - 1.f : 3.f - 4.f * p; }

void Fx::init(float fs, int16_t* mfxL, int16_t* mfxR, int32_t mfxLen,
              int16_t* dlyL, int16_t* dlyR, int32_t dlyLen, int16_t* rev, int32_t revLen) {
    fs_ = fs;
    invFs_ = 1.f / fs;
    mL_.b = mfxL; mR_.b = mfxR; mL_.n = mR_.n = mfxLen; mL_.w = mR_.w = 0;
    dL_.b = dlyL; dR_.b = dlyR; dL_.n = dR_.n = dlyLen; dL_.w = dR_.w = 0;
    rv_ = rev;
    rvLen_ = revLen;
    fxDefaults(p_);
    memset(send_, 0, sizeof(send_));
    memset(rvIn_, 0, sizeof(rvIn_));
    memset(rvOutL_, 0, sizeof(rvOutL_));
    memset(rvOutR_, 0, sizeof(rvOutR_));
    buildReverb(1.f);
}

void Fx::resetMfx() {
    toneL_ = Tone(); toneR_ = Tone();
    lp1_ = lp2_ = hp1_ = 0;
    lfo1_ = lfo2_ = 0;
    env_ = hold_ = holdCnt_ = 0;
    timeL_ = timeR_ = 0;
    dampL_ = dampR_ = fbL_ = fbR_ = 0;
    memset(apState_, 0, sizeof(apState_));
    phFb_ = 0;
    modSmooth_ = 0;
    for (auto& s : svf_) s = Svf();
    headA_ = PitchHead(); headB_ = PitchHead();
    rPhase_ = 0;
    clearPos_ = 0;       // memory is wiped a little per block while the effect is muted
}

void Fx::control(const FxPatch& p, float bpm) {
    p_ = p;
    bpm_ = bpm;
    if (p_.mfxType >= MFX_COUNT) p_.mfxType = MFX_CHO1;
    if (curType_ < 0) { curType_ = p_.mfxType; resetMfx(); }
    // The running effect keeps its own parameters until it has faded out.
    if (p_.mfxType == curType_) {
        const int np = mfxParamCount(curType_);
        for (int i = 0; i < np; i++) {
            const FxParamDef& d = mfxParam(curType_, i);
            mRun_[i] = p_.m[i] < d.lo ? d.lo : (p_.m[i] > d.hi ? d.hi : p_.m[i]);
        }
    }
    if (revTail_ > 0 && !p_.revOn) revTail_--;
    if (p_.revOn) revTail_ = (int32_t)(40.f * fs_ / (float)kBlock);
}

static float syncedMs(int sync, int ms, int note, float bpm) {
    return sync ? noteBeats(note) * 60000.f / bpm : (float)ms;
}

// ---------------------------------------------------------------- CHORUS & MFX
J8_HOT void Fx::runMfx(const float* in, float* L, float* R) {
    const int16_t* m = mRun_;
    const float msToS = fs_ * 0.001f;
    switch (curType_) {
        case MFX_CHO1: {
            // Rates and sweep ranges below are commonly quoted Juno measurements; not verified here.
            static const float kRate[5] = {0.513f, 0.863f, 9.75f, 0.6f, 0.95f};
            static const float kMin[5] = {1.66f, 1.66f, 3.30f, 2.0f, 2.0f};
            static const float kMax[5] = {5.35f, 5.35f, 3.70f, 6.0f, 7.5f};
            int mode = m[C1_MODE];
            float inc = kRate[mode] * invFs_;
            float c = 0.5f * (kMin[mode] + kMax[mode]) * msToS, dep = 0.5f * (kMax[mode] - kMin[mode]) * msToS;
            float bal = (float)m[C1_BAL] * 0.01f, noise = (float)m[C1_NOISE] * (0.0009f / 127.f);
            float lev = (float)m[C1_LEV] * 0.01f, a = onePoleA(9000.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                lfo1_ += inc; if (lfo1_ >= 1.f) lfo1_ -= 1.f;
                float t = triFrom(lfo1_);
                lp1_ += (in[i] - lp1_) * a;
                mL_.push(lp1_);
                float n = rng_.bipolar() * noise;
                float wl = mL_.read(c + dep * t), wr = mL_.read(c - dep * t);
                lp2_ += (wl - lp2_) * a; hp1_ += (wr - hp1_) * a;
                // Dry and wet add incoherently, so the mix is lifted to hold the perceived level.
                L[i] = (in[i] * (1.f - bal) + (lp2_ + n) * bal) * lev * 1.35f;
                R[i] = (in[i] * (1.f - bal) + (hp1_ - n) * bal) * lev * 1.35f;
            }
            break;
        }
        case MFX_CHO2: {
            // One BBD, wet added on one side and subtracted on the other. Estimate: 0.8 Hz, 2.5-7 ms.
            float inc = 0.8f * invFs_, c = 4.75f * msToS, dep = 2.25f * msToS;
            float wet = (float)m[C2_INT] / 127.f, lo = dbGain(m[C2_LO]), hi = dbGain(m[C2_HI]);
            float lev = (float)m[C2_LEV] * 0.01f, a = onePoleA(5000.f, invFs_);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                lfo1_ += inc; if (lfo1_ >= 1.f) lfo1_ -= 1.f;
                lp1_ += (in[i] - lp1_) * a;
                mL_.push(lp1_);
                float w = sat(mL_.read(c + dep * sinTurn(lfo1_)) * 1.3f) * wet * 0.8f;
                L[i] = toneL_.tick(in[i] + w, lo, hi, aL, aH) * lev;
                R[i] = toneR_.tick(in[i] - w, lo, hi, aL, aH) * lev;
            }
            break;
        }
        case MFX_CHO3: {
            // Two BBDs swept in opposite directions and cross-mixed. All mode values are estimates.
            static const float kRate[7] = {0.25f, 0.25f, 0.25f, 0.5f, 0.5f, 0.5f, 0.5f};
            static const float kDep[7] = {0.20f, 0.35f, 0.55f, 0.75f, 0.95f, 1.1f, 1.3f};
            static const float kCross[7] = {0.15f, 0.25f, 0.35f, 0.45f, 0.5f, 0.55f, 0.6f};
            int mode = m[C3_MODE];
            float inc = kRate[mode] * invFs_, dep = kDep[mode] * msToS, c = 3.5f * msToS, x = kCross[mode];
            float lo = dbGain(m[C3_LO]), hi = dbGain(m[C3_HI]), lev = (float)m[C3_LEV] * 0.01f;
            float a = onePoleA(11000.f, invFs_), aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                lfo1_ += inc; if (lfo1_ >= 1.f) lfo1_ -= 1.f;
                float t = triFrom(lfo1_);
                lp1_ += (in[i] - lp1_) * a;
                mL_.push(lp1_);
                float w1 = mL_.read(c + dep * t), w2 = mL_.read(c - dep * t);
                float l = in[i] + 0.6f * w1 - x * w2, r = in[i] + 0.6f * w2 - x * w1;
                L[i] = toneL_.tick(l * 0.8f, lo, hi, aL, aH) * lev;
                R[i] = toneR_.tick(r * 0.8f, lo, hi, aL, aH) * lev;
            }
            break;
        }
        case MFX_DLY1: {
            float target = syncedMs(m[D1_SYNC], m[D1_TIME], m[D1_NOTE], bpm_) * msToS;
            if (timeL_ <= 0.f) timeL_ = target;
            float fb = (float)m[D1_FB] * 0.01f, bal = (float)m[D1_BAL] * 0.01f, lev = (float)m[D1_LEV] * 0.01f;
            float lo = dbGain(m[D1_LO]), hi = dbGain(m[D1_HI]);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            // The read point glides, so time changes bend pitch like tape.
            const float slew = 12.f * invFs_;
            for (int i = 0; i < kBlock; i++) {
                timeL_ += (target - timeL_) * slew;
                float y = mL_.read(timeL_);
                mL_.push(in[i] + sat(y * fb));
                float w = toneL_.tick(y, lo, hi, aL, aH);
                float o = (in[i] * (1.f - bal) + w * bal) * lev;
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_DLY2: {
            // Tap 2 at the set time, tap 1 halfway (halfway is an estimate).
            float t2 = syncedMs(m[D2_SYNC], m[D2_TIME], m[D2_NOTE], bpm_) * msToS;
            if (timeL_ <= 0.f) timeL_ = t2;
            float fb = (float)m[D2_FB] * 0.01f, bal = (float)m[D2_BAL] * 0.01f, lev = (float)m[D2_LEV] * 0.01f;
            float a = dampA(m[D2_DAMP], invFs_);
            float g1l, g1r, g2l, g2r;
            panGains(m[D2_PAN1], g1l, g1r);
            panGains(m[D2_PAN2], g2l, g2r);
            float v1 = (float)m[D2_LV1] / 127.f, v2 = (float)m[D2_LV2] / 127.f;
            float lo = dbGain(m[D2_LO]), hi = dbGain(m[D2_HI]);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            const float slew = 12.f * invFs_;
            for (int i = 0; i < kBlock; i++) {
                timeL_ += (t2 - timeL_) * slew;
                float y1 = mL_.read(timeL_ * 0.5f) * v1, yraw = mL_.read(timeL_), y2 = yraw * v2;
                dampL_ += (yraw - dampL_) * a;
                mL_.push(in[i] + sat(dampL_ * fb));
                float wl = toneL_.tick(y1 * g1l + y2 * g2l, lo, hi, aL, aH);
                float wr = toneR_.tick(y1 * g1r + y2 * g2r, lo, hi, aL, aH);
                L[i] = (in[i] * (1.f - bal) + wl * bal) * lev;
                R[i] = (in[i] * (1.f - bal) + wr * bal) * lev;
            }
            break;
        }
        case MFX_DLY3: {
            float tl = syncedMs(m[D3_LSYNC], m[D3_LTIME], m[D3_LNOTE], bpm_) * msToS;
            float tr = syncedMs(m[D3_RSYNC], m[D3_RTIME], m[D3_RNOTE], bpm_) * msToS;
            if (timeL_ <= 0.f) { timeL_ = tl; timeR_ = tr; }
            float rate = m[D3_RSYNC2] ? bpm_ / 60.f / noteBeats(m[D3_RNOTE2]) : (float)m[D3_RATE] * 0.01f;
            float inc = rate * invFs_, dep = (float)m[D3_DEPTH] / 127.f * 6.f * msToS;   // 6 ms peak: estimate
            float ph = (float)m[D3_PHASE] / 360.f;
            float fb = (float)m[D3_FB] * 0.01f, bal = (float)m[D3_BAL] * 0.01f, lev = (float)m[D3_LEV] * 0.01f;
            float a = dampA(m[D3_DAMP], invFs_);
            bool cross = m[D3_FBMODE] != 0;
            float lo = dbGain(m[D3_LO]), hi = dbGain(m[D3_HI]);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            const float slew = 12.f * invFs_;
            for (int i = 0; i < kBlock; i++) {
                timeL_ += (tl - timeL_) * slew; timeR_ += (tr - timeR_) * slew;
                lfo1_ += inc; if (lfo1_ >= 1.f) lfo1_ -= 1.f;
                float p2 = lfo1_ + ph; if (p2 >= 1.f) p2 -= 1.f;
                float yl = mL_.read(timeL_ + dep * (1.f + sinTurn(lfo1_)));
                float yr = mR_.read(timeR_ + dep * (1.f + sinTurn(p2)));
                dampL_ += (yl - dampL_) * a; dampR_ += (yr - dampR_) * a;
                mL_.push(in[i] + sat((cross ? dampR_ : dampL_) * fb));
                mR_.push(in[i] + sat((cross ? dampL_ : dampR_) * fb));
                float wl = toneL_.tick(yl, lo, hi, aL, aH), wr = toneR_.tick(yr, lo, hi, aL, aH);
                L[i] = (in[i] * (1.f - bal) + wl * bal) * lev;
                R[i] = (in[i] * (1.f - bal) + wr * bal) * lev;
            }
            break;
        }
        case MFX_RDLY: {
            // Two reversed heads, each playing a slice backward with a triangular fade.
            float T = syncedMs(m[RD_SYNC], m[RD_TIME], m[RD_NOTE], bpm_) * msToS;
            T = clampf(T, 64.f, (float)mL_.n * 0.48f);
            float inc = 1.f / T;
            float fb = (float)m[RD_FB] * 0.01f, bal = (float)m[RD_BAL] * 0.01f, lev = (float)m[RD_LEV] * 0.01f;
            float lo = dbGain(m[RD_LO]), hi = dbGain(m[RD_HI]);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                rPhase_ += inc; if (rPhase_ >= 1.f) rPhase_ -= 1.f;
                float p2 = rPhase_ + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
                float g1 = 1.f - 2.f * fabsf(rPhase_ - 0.5f), g2 = 1.f - 2.f * fabsf(p2 - 0.5f);
                // Delay grows at 2 samples per sample, so the read point runs backward.
                float y = mL_.read(2.f * rPhase_ * T) * g1 + mL_.read(2.f * p2 * T) * g2;
                mL_.push(in[i] + sat(y * fb));
                float w = toneL_.tick(y, lo, hi, aL, aH);
                float o = (in[i] * (1.f - bal) + w * bal) * lev;
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_OD: {
            float drive = 1.f + (float)m[OD_DIST] * 0.25f;
            float aH = onePoleA(720.f, invFs_);                  // mid-hump emphasis, estimate
            float aT = onePoleA(800.f + 7000.f * (float)m[OD_TONE] / 127.f, invFs_);
            float lev = (float)m[OD_LEV] * 0.01f * (0.6f - 0.3f * (float)m[OD_DIST] / 127.f);
            for (int i = 0; i < kBlock; i++) {
                hp1_ += (in[i] - hp1_) * aH;
                float x = (in[i] - hp1_) * drive + in[i];
                float y = sat(x + 0.1f) - 0.0997f;               // slight asymmetry for even harmonics
                lp1_ += (y - lp1_) * aT;
                float o = lp1_ * lev;
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_FUZZ: {
            float drive = 2.f + (float)m[FZ_DRIVE] * 0.8f;
            float aT = onePoleA(500.f + 80.f * (float)m[FZ_TONE], invFs_);
            float lev = (float)m[FZ_LEV] * 0.01f * 0.3f;
            for (int i = 0; i < kBlock; i++) {
                float x = in[i] * drive;
                float y = clampf(x + 0.3f, -0.6f, 1.f) * 1.25f - 0.25f;   // hard, lopsided clip
                y += 0.25f * fabsf(sat(in[i] * drive * 0.5f));            // rectified octave content
                lp1_ += (y - lp1_) * aT;
                hp1_ += (lp1_ - hp1_) * 0.002f;
                float o = (lp1_ - hp1_) * lev;
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_FATTEN: {
            float odd = (float)m[FT_ODD] * 0.01f * 0.25f, even = (float)m[FT_EVEN] * 0.01f * 0.3f;
            float lev = (float)m[FT_LEV] * 0.01f;
            for (int i = 0; i < kBlock; i++) {
                float x = clampf(in[i] * 3.f, -1.f, 1.f), x2 = x * x;
                float y = in[i] + odd * x * x2 * (3.f - 2.f * x2) + even * (x2 - env_);
                env_ += (x2 - env_) * 0.001f;    // keeps the even-harmonic DC out
                y = sat(y);
                L[i] = y * lev; R[i] = y * lev;
            }
            break;
        }
        case MFX_BITC: {
            // RATE 0 = full rate, 127 = about 700 Hz. BITS 20 = clean. Both directions are my reading.
            float period = pow2f((float)m[BC_RATE] / 127.f * 6.1f);
            float levels = pow2f((float)m[BC_BIT]);
            float aF = onePoleA(16000.f * pow2f(-(float)m[BC_FILT] / 127.f * 5.f), invFs_);
            float lo = dbGain(m[BC_LO]), hi = dbGain(m[BC_HI]), lev = (float)m[BC_LEV] * 0.01f;
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                holdCnt_ += 1.f;
                if (holdCnt_ >= period) {
                    holdCnt_ -= period;
                    float q = clampf(in[i], -1.f, 1.f) * levels;
                    hold_ = (float)(int32_t)(q + (q >= 0.f ? 0.5f : -0.5f)) / levels;
                }
                lp1_ += (hold_ - lp1_) * aF;
                float o = toneL_.tick(lp1_, lo, hi, aL, aH) * lev;
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_LOFI: {
            // Compressor, then degrade, then post filter, in the order the manual gives. Tables are estimates.
            static const float kPeriod[9] = {1.f, 1.5f, 2.f, 2.5f, 3.f, 4.f, 5.f, 6.5f, 8.f};
            static const float kBits[9] = {14.f, 12.f, 11.f, 10.f, 9.f, 8.f, 7.f, 6.f, 5.f};
            int li = clampf((float)m[LF_LOFI], 1.f, 9.f) - 1;
            float period = kPeriod[li], levels = pow2f(kBits[li]);
            float ratio = 1.f / (float)m[LF_COMP];
            float bal = (float)m[LF_BAL] * 0.01f, lev = (float)m[LF_LEV] * 0.01f;
            float lo = dbGain(m[LF_LO]), hi = dbGain(m[LF_HI]);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            float aF = onePoleA(150.f * pow2f((float)(m[LF_COFF] - 1) * 0.42f), invFs_);
            int post = m[LF_FILT];
            float aAtk = onePoleA(100.f, invFs_), aRel = onePoleA(6.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                float x = in[i];
                if (m[LF_COMP] > 1) {
                    float a = fabsf(x);
                    env_ += (a - env_) * (a > env_ ? aAtk : aRel);
                    float over = env_ > 0.2f ? env_ / 0.2f : 1.f;
                    float g = 1.f / (1.f + (over - 1.f) * (1.f - ratio));
                    x *= g * (1.f + (1.f - ratio) * 1.5f);
                }
                holdCnt_ += 1.f;
                if (holdCnt_ >= period) {
                    holdCnt_ -= period;
                    float q = clampf(x, -1.f, 1.f) * levels;
                    hold_ = (float)(int32_t)(q + (q >= 0.f ? 0.5f : -0.5f)) / levels;
                }
                float y = hold_;
                if (post) {
                    lp1_ += (y - lp1_) * aF;
                    y = post == 1 ? lp1_ : y - lp1_;
                }
                float w = toneL_.tick(y, lo, hi, aL, aH);
                float o = (in[i] * (1.f - bal) + w * bal) * lev;
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_PHA1:
        case MFX_PHA2: {
            bool multi = curType_ == MFX_PHA2;
            int stages = multi ? (m[P2_STAGES] + 1) * 4 : 4;
            float rate, depth, center, reso, mix, lev, lo, hi;
            float gl = 1.f, gr = 1.f;
            if (multi) {
                rate = m[P2_SYNC] ? bpm_ / 60.f / noteBeats(m[P2_NOTE]) : (float)m[P2_RATE] * 0.01f;
                depth = (float)m[P2_DEPTH] / 127.f * 2.5f;       // octaves either way: estimate
                center = 100.f * pow2f((float)m[P2_MANUAL] / 127.f * 5.9f);
                reso = (float)m[P2_RESO] / 127.f * 0.9f;
                mix = (float)m[P2_MIX] / 127.f;
                lo = dbGain(m[P2_LO]); hi = dbGain(m[P2_HI]);
                lev = (float)m[P2_LEV] * 0.01f;
                if (m[P2_PAN] != 0) { panGains(m[P2_PAN], gl, gr); gl *= 1.414f; gr *= 1.414f; }
            } else {
                rate = 0.05f * pow2f((float)m[P1_SPEED] * 0.0737f);   // 0.05 to 8 Hz: estimate
                depth = (float)m[P1_DEPTH] / 127.f * 1.8f;
                center = 700.f;
                reso = 0.15f;
                mix = 1.f;
                lo = dbGain(m[P1_LO]); hi = dbGain(m[P1_HI]);
                lev = (float)m[P1_LEV] * 0.01f;
            }
            lfo1_ += rate * (float)kBlock * invFs_;
            if (lfo1_ >= 1.f) lfo1_ -= 1.f;
            float fc = clampf(center * pow2f(depth * sinTurn(lfo1_)), 30.f, 0.45f * fs_);
            float t = tanPi(fc * invFs_);
            float target = (t - 1.f) / (t + 1.f);
            float dA = (target - apCoef_) * (1.f / (float)kBlock);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            float wetG = mix * 0.7f, dryG = 0.7f;
            for (int i = 0; i < kBlock; i++) {
                apCoef_ += dA;
                float x = in[i] + phFb_ * reso;
                for (int s = 0; s < stages; s++) {
                    float y = apCoef_ * x + apState_[s];
                    apState_[s] = x - apCoef_ * y;
                    x = y;
                }
                phFb_ = sat(x);
                float o = toneL_.tick(in[i] * dryG + x * wetG, lo, hi, aL, aH) * lev;
                L[i] = o * gl; R[i] = o * gr;
            }
            break;
        }
        case MFX_FILT: {
            float rate = m[SF_SYNC] ? bpm_ / 60.f / noteBeats(m[SF_NOTE]) : (float)m[SF_RATE] * 0.01f;
            lfo1_ += rate * (float)kBlock * invFs_;
            if (lfo1_ >= 1.f) lfo1_ -= 1.f;
            float w;
            switch (m[SF_WAVE]) {
                case 0: w = triFrom(lfo1_); break;
                case 1: w = lfo1_ < 0.5f ? 1.f : -1.f; break;
                case 2: w = sinTurn(lfo1_); break;
                case 3: w = 2.f * lfo1_ - 1.f; break;
                default: w = 1.f - 2.f * lfo1_; break;
            }
            // ATTACK slows the jumps of square and saw; higher value = faster (my reading).
            float slew = (m[SF_WAVE] == 0 || m[SF_WAVE] == 2) ? 1.f : 0.01f + 0.99f * (float)m[SF_ATK] / 127.f;
            modSmooth_ += (w - modSmooth_) * slew;
            float oct = m[SF_MOD] ? modSmooth_ * (float)m[SF_DEPTH] / 127.f * 4.f : 0.f;
            float fc = clampf(20.f * pow2f((float)m[SF_CUT] / 127.f * 9.97f + oct), 20.f, 0.45f * fs_);
            float g = tanPi(fc * invFs_);
            float q = 0.6f + (float)m[SF_RES] * 0.12f;
            static const int kStages[3] = {1, 2, 4};              // 12, 24, 48 dB/oct: estimate
            int stages = kStages[m[SF_SLOPE] & 3];
            int type = m[SF_TYPE];
            float gain = dbGain(m[SF_GAIN]) * (float)m[SF_LEV] * 0.01f;
            float kLast = 1.f / q, kFlat = 1.414f;
            for (int i = 0; i < kBlock; i++) {
                float x = in[i];
                for (int s = 0; s < stages; s++) {
                    Svf& f = svf_[s];
                    float k = s == stages - 1 ? kLast : kFlat;
                    float a1 = 1.f / (1.f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                    float v3 = x - f.s2;
                    float v1 = a1 * f.s1 + a2 * v3;
                    float v2 = f.s2 + a2 * f.s1 + a3 * v3;
                    f.s1 = 2.f * v1 - f.s1;
                    f.s2 = 2.f * v2 - f.s2;
                    switch (type) {
                        case 0: x = v2; break;
                        case 1: x = v1 * k; break;
                        case 2: x = x - k * v1 - v2; break;
                        default: x = x - k * v1; break;
                    }
                }
                float o = sat(x * gain);
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_PCH1: {
            float semis = (float)m[PS_CRS] + (float)m[PS_FINE] * 0.01f;
            float ratio = pow2f(semis / 12.f);
            const float win = 0.05f * fs_;                        // 50 ms grains: estimate
            float inc = (1.f - ratio) / win;
            float base = syncedMs(m[PS_SYNC], m[PS_TIME], m[PS_NOTE], bpm_) * msToS;
            float fb = (float)m[PS_FB] * 0.01f, bal = (float)m[PS_BAL] * 0.01f, lev = (float)m[PS_LEV] * 0.01f;
            float lo = dbGain(m[PS_LO]), hi = dbGain(m[PS_HI]);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                mL_.push(in[i] + sat(lp1_ * fb));
                lp1_ = headA_.tick(mL_, base, win, inc);
                float w = toneL_.tick(lp1_, lo, hi, aL, aH);
                float o = (in[i] * (1.f - bal) + w * bal) * lev;
                L[i] = o; R[i] = o;
            }
            break;
        }
        case MFX_PCH2: {
            const float win = 0.05f * fs_;
            float i1 = (1.f - pow2f(((float)m[V2_1CRS] + (float)m[V2_1FIN] * 0.01f) / 12.f)) / win;
            float i2 = (1.f - pow2f(((float)m[V2_2CRS] + (float)m[V2_2FIN] * 0.01f) / 12.f)) / win;
            float b1 = syncedMs(m[V2_1SYNC], m[V2_1TIME], m[V2_1NOTE], bpm_) * msToS;
            float b2 = syncedMs(m[V2_2SYNC], m[V2_2TIME], m[V2_2NOTE], bpm_) * msToS;
            float f1 = (float)m[V2_1FB] * 0.01f, f2 = (float)m[V2_2FB] * 0.01f;
            float l1 = (float)m[V2_1LEV] / 127.f, l2 = (float)m[V2_2LEV] / 127.f;
            float g1l, g1r, g2l, g2r;
            panGains(m[V2_1PAN], g1l, g1r);
            panGains(m[V2_2PAN], g2l, g2r);
            float bal = (float)m[V2_BAL] * 0.01f, lev = (float)m[V2_LEV] * 0.01f;
            float lo = dbGain(m[V2_LO]), hi = dbGain(m[V2_HI]);
            float aL = onePoleA(200.f, invFs_), aH = onePoleA(4000.f, invFs_);
            for (int i = 0; i < kBlock; i++) {
                mL_.push(in[i] + sat(lp1_ * f1));
                mR_.push(in[i] + sat(lp2_ * f2));
                lp1_ = headA_.tick(mL_, b1, win, i1);
                lp2_ = headB_.tick(mR_, b2, win, i2);
                float a = lp1_ * l1, b = lp2_ * l2;
                float wl = toneL_.tick(a * g1l + b * g2l, lo, hi, aL, aH);
                float wr = toneR_.tick(a * g1r + b * g2r, lo, hi, aL, aH);
                L[i] = (in[i] * (1.f - bal) + wl * bal) * lev;
                R[i] = (in[i] * (1.f - bal) + wr * bal) * lev;
            }
            break;
        }
        default:
            for (int i = 0; i < kBlock; i++) { L[i] = in[i]; R[i] = in[i]; }
            break;
    }
}

// ---------------------------------------------------------------- reverb
// Plate-style tank (figure-eight of allpasses and delays, after Dattorro 1997), at half
// the audio rate. Room, hall and plate differ by size, damping and input diffusion.
namespace { struct RvType { float size, rtMax, damp, diff; }; }
static const RvType kRvTypes[5] = {       // all estimates
    {0.40f, 1.5f, 0.45f, 0.60f},          // ROOM 1
    {0.55f, 2.5f, 0.35f, 0.65f},          // ROOM 2
    {0.85f, 5.0f, 0.30f, 0.72f},          // HALL 1
    {1.15f, 9.0f, 0.22f, 0.75f},          // HALL 2
    {0.75f, 6.0f, 0.08f, 0.75f},          // PLATE
};

static constexpr float kRtFit = 2.2f;

void Fx::buildReverb(float size) {
    if (!rv_ || rvLen_ < 29000) { rv_ = nullptr; return; }   // no memory: reverb stays silent
    static const int kDiff[4] = {115, 86, 305, 223};            // at 24 kHz
    static const int kTank[8] = {542, 3590, 1450, 2998, 732, 3398, 2141, 2549};
    int32_t off = 0;
    auto add = [&](RvLine& l, int len) { l.off = off; l.len = len < 4 ? 4 : len; l.w = 0; off += l.len; };
    add(rvPre_, 2500);                                          // 100 ms at 24 kHz
    add(rvPs1_, 3000);
    add(rvPs2_, 3000);
    for (int i = 0; i < 4; i++) add(rvDiff_[i], kDiff[i] + 1);
    for (int i = 0; i < 8; i++) add(rvTank_[i], (int)((float)kTank[i] * size) + 40);
    rvScale_ = size;
    memset(rv_, 0, (size_t)rvLen_ * 2);
    rvTail1_ = rvTail2_ = rvDamp1_ = rvDamp2_ = rvBw_ = rvLowA_ = rvLowB_ = 0;
}

J8_HOT void Fx::reverbBlock() {
    if (revTail_ == 0 || !rv_) {
        memset(rvOutL_, 0, sizeof(rvOutL_));
        memset(rvOutR_, 0, sizeof(rvOutR_));
        return;
    }
    const int16_t* r = p_.r;
    const bool shim = r[RV_MODE] == RVM_SHIMMER;
    const RvType& ty = kRvTypes[(r[RV_TYPE] >= 0 && r[RV_TYPE] < 5) ? r[RV_TYPE] : 0];
    const float fsR = fs_ * 0.5f;
    float wantSize = shim ? 1.15f : ty.size;
    if (rvBuiltType_ != (int)(wantSize * 100.f)) {
        // Size changes mean new line lengths; this clears the tail.
        rvBuiltType_ = (int)(wantSize * 100.f);
        buildReverb(wantSize);
    }
    float rt, damp, lowDamp = 0.f, diff, predelay, outLev;
    float modDepth = 6.f, modRate = 0.8f;
    if (shim) {
        rt = (float)r[RS_TIME] * 0.1f;
        damp = clampf(0.25f + (float)r[RS_HDAMP] * 0.012f, 0.f, 0.9f);
        lowDamp = clampf((float)r[RS_LDAMP] * 0.012f + 0.1f, 0.f, 0.8f);
        diff = 0.35f + 0.04f * (float)r[RS_DENS];
        predelay = (float)r[RS_PDLY] * 0.001f * fsR;
        outLev = (float)r[RS_LEV] * 0.01f;
        modRate = 0.1f + (float)r[RS_MRATE] * 0.05f;
        modDepth = (float)r[RS_MDEPTH] * 0.25f;
    } else {
        // TIME 1-100 is an exponential law up to the type's maximum (estimate).
        rt = ty.rtMax * pow2f(((float)r[RV_TIME] - 100.f) * 0.05f);
        damp = ty.damp;
        diff = ty.diff;
        predelay = (float)r[RV_PDLY] * 0.001f * fsR;
        outLev = (float)r[RV_LEV] * 0.01f;
    }
    if (rt < 0.05f) rt = 0.05f;
    // A half loop takes about 0.36 s * size at this rate. Damping shortens the broadband decay,
    // so the loop gain aims longer (factor fitted to the RT60 test in host/test_fx.cpp).
    float halfLoop = 0.36f * rvScale_;
    float decay = clampf(pow2f(-9.966f * halfLoop / (rt * kRtFit)), 0.f, 0.9995f);
    float bw = 0.85f;
    float lfoInc = modRate / fsR;

    // Shimmer pitch shifters
    const float psWin = 0.05f * fsR;
    float psInc1 = 0, psInc2 = 0, fb1 = 0, fb2 = 0, lv1 = 0, lv2 = 0;
    if (shim) {
        psInc1 = (1.f - pow2f((float)r[RS_PCH1] / 12.f)) / psWin;
        psInc2 = (1.f - pow2f((float)r[RS_PCH2] / 12.f)) / psWin;
        fb1 = (float)r[RS_FB1] * 0.01f;
        fb2 = (float)r[RS_FB2] * 0.01f;
        lv1 = (float)r[RS_LV1] * 0.01f;
        lv2 = (float)r[RS_LV2] * 0.01f;
    }

    // Output taps, as fractions of each tank line (Dattorro's table, scaled).
    auto tap = [&](const RvLine& l, float frac) { return rvRd(l, (int32_t)(frac * (float)(l.len - 40)) + 1); };

    for (int n = 0, i = 0; n < kBlock / 2; n++, i += 2) {
        float x = 0.5f * (rvIn_[i] + rvIn_[i + 1]);
        if (shim) x += sat(rvPs1Out_ * fb1);
        rvWr(rvPre_, x);
        float in = rvRd(rvPre_, (int32_t)predelay + 1);
        rvBw_ += (in - rvBw_) * bw;
        float y = rvBw_;
        // Input diffusion
        const float d1[4] = {diff, diff, diff * 0.85f, diff * 0.85f};
        for (int k = 0; k < 4; k++) {
            float z = rvRd(rvDiff_[k], rvDiff_[k].len - 1);
            float v = y - d1[k] * z;
            rvWr(rvDiff_[k], v);
            y = z + d1[k] * v;
        }
        rvLfo_ += lfoInc;
        if (rvLfo_ >= 1.f) rvLfo_ -= 1.f;
        float mo = sinTurn(rvLfo_) * modDepth;

        // Left half
        float a = y + rvTail2_ * decay;
        {
            RvLine& l = rvTank_[0];
            float dl = (float)(l.len - 20) + mo;
            int32_t di = (int32_t)dl;
            float f = dl - (float)di;
            float z = rvRd(l, di) * (1.f - f) + rvRd(l, di + 1) * f;
            float v = a + 0.7f * z;
            rvWr(l, v);
            a = z - 0.7f * v;
        }
        rvWr(rvTank_[1], a);
        a = rvRd(rvTank_[1], rvTank_[1].len - 1);
        rvDamp1_ += (a - rvDamp1_) * (1.f - damp);
        a = rvDamp1_;
        if (lowDamp > 0.f) { rvLowA_ += (a - rvLowA_) * 0.01f; a -= rvLowA_ * lowDamp; }
        a *= decay;
        {
            RvLine& l = rvTank_[2];
            float z = rvRd(l, l.len - 1);
            float v = a - 0.5f * z;
            rvWr(l, v);
            a = z + 0.5f * v;
        }
        rvWr(rvTank_[3], a);
        float tailA = rvRd(rvTank_[3], rvTank_[3].len - 1);

        // Right half
        float b = y + rvTail1_ * decay;
        {
            RvLine& l = rvTank_[4];
            float dl = (float)(l.len - 20) - mo;
            int32_t di = (int32_t)dl;
            float f = dl - (float)di;
            float z = rvRd(l, di) * (1.f - f) + rvRd(l, di + 1) * f;
            float v = b + 0.7f * z;
            rvWr(l, v);
            b = z - 0.7f * v;
        }
        rvWr(rvTank_[5], b);
        b = rvRd(rvTank_[5], rvTank_[5].len - 1);
        rvDamp2_ += (b - rvDamp2_) * (1.f - damp);
        b = rvDamp2_;
        if (lowDamp > 0.f) { rvLowB_ += (b - rvLowB_) * 0.01f; b -= rvLowB_ * lowDamp; }
        b *= decay;
        {
            RvLine& l = rvTank_[6];
            float z = rvRd(l, l.len - 1);
            float v = b - 0.5f * z;
            rvWr(l, v);
            b = z + 0.5f * v;
        }
        rvWr(rvTank_[7], b);
        float tailB = rvRd(rvTank_[7], rvTank_[7].len - 1);

        rvTail1_ = sat(tailA);
        rvTail2_ = sat(tailB);

        // Stereo outputs
        float oL = 0.6f * (tap(rvTank_[5], 0.09f) + tap(rvTank_[5], 0.93f) - tap(rvTank_[6], 0.61f) +
                           tap(rvTank_[7], 0.60f) - tap(rvTank_[1], 0.60f) - tap(rvTank_[2], 0.06f));
        float oR = 0.6f * (tap(rvTank_[1], 0.12f) + tap(rvTank_[1], 0.92f) - tap(rvTank_[2], 0.41f) +
                           tap(rvTank_[3], 0.67f) - tap(rvTank_[5], 0.66f) - tap(rvTank_[6], 0.09f));
        if (shim) {
            float mono = 0.5f * (oL + oR);
            rvWr(rvPs1_, mono);
            rvWr(rvPs2_, mono + sat(rvPs2Out_ * fb2));
            auto ps = [&](const RvLine& l, float& ph, float inc) {
                ph += inc;
                if (ph >= 1.f) ph -= 1.f;
                if (ph < 0.f) ph += 1.f;
                float p2 = ph + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
                float g1 = 1.f - 2.f * fabsf(ph - 0.5f), g2 = 1.f - 2.f * fabsf(p2 - 0.5f);
                return rvRd(l, (int32_t)(ph * psWin) + 2) * g1 + rvRd(l, (int32_t)(p2 * psWin) + 2) * g2;
            };
            rvPs1Out_ = ps(rvPs1_, rvPh1_, psInc1);
            rvPs2Out_ = ps(rvPs2_, rvPh2_, psInc2);
            float add = rvPs1Out_ * lv1 + rvPs2Out_ * lv2;
            oL += add; oR += add;
        }
        oL *= outLev; oR *= outLev;
        // Back up to full rate
        rvOutL_[i] = 0.5f * (rvLastL_ + oL); rvOutL_[i + 1] = oL;
        rvOutR_[i] = 0.5f * (rvLastR_ + oR); rvOutR_[i + 1] = oR;
        rvLastL_ = oL; rvLastR_ = oR;
    }
}

// ---------------------------------------------------------------- top level
J8_HOT void Fx::process(const float* in, float* L, float* R) {
    // CHORUS & MFX: fades out before a type change or switch-off, then wipes its memory
    // a little per block so old audio can never replay, then fades back in if wanted.
    const bool typeChange = p_.mfxType != curType_;
    bool wanted = p_.mfxOn && !typeChange && clearPos_ < 0;
    if (clearPos_ >= 0) {
        int32_t n = mL_.n - clearPos_;
        if (n > 2048) n = 2048;
        memset(mL_.b + clearPos_, 0, (size_t)n * 2);
        memset(mR_.b + clearPos_, 0, (size_t)n * 2);
        clearPos_ += n;
        if (clearPos_ >= mL_.n) clearPos_ = -1;
    }
    if (mfxMix_ > 0.f || wanted) {
        float g = mfxMix_;
        float next = clampf(mfxMix_ + (wanted ? 0.05f : -0.05f), 0.f, 1.f);
        float dg = (next - g) * (1.f / (float)kBlock);
        runMfx(in, L, R);
        for (int i = 0; i < kBlock; i++) {
            g += dg;
            L[i] = in[i] + (L[i] - in[i]) * g;
            R[i] = in[i] + (R[i] - in[i]) * g;
        }
        mfxMix_ = next;
        if (mfxMix_ == 0.f && !wanted) {
            if (typeChange) curType_ = p_.mfxType;
            resetMfx();
        }
    } else {
        if (typeChange) { curType_ = p_.mfxType; resetMfx(); }
        for (int i = 0; i < kBlock; i++) { L[i] = in[i]; R[i] = in[i]; }
    }

    float pre[kBlock];
    for (int i = 0; i < kBlock; i++) pre[i] = 0.5f * (L[i] + R[i]);

    // Delay. Switching off stops the send; echoes ring out, then the memory is wiped.
    const int16_t* d = p_.d;
    float rvFromDelay = 0.f;
    float dlyRv[kBlock];
    if (p_.dlyOn) {
        if (dlyClear_ < 0) dlyTail_ = (int32_t)(30.f * fs_ / (float)kBlock);
    } else if (dlyTail_ > 0) {
        if (--dlyTail_ == 0) dlyClear_ = 0;
    }
    if (dlyClear_ >= 0) {
        int32_t n = dL_.n - dlyClear_;
        if (n > 2048) n = 2048;
        memset(dL_.b + dlyClear_, 0, (size_t)n * 2);
        memset(dR_.b + dlyClear_, 0, (size_t)n * 2);
        dlyClear_ += n;
        if (dlyClear_ >= dL_.n) { dlyClear_ = -1; dlyDampL_ = 0; dlyTime_ = 0; }
    }
    const bool dlyRun = dlyTail_ > 0 && dlyClear_ < 0;
    if (dlyRun) {
        float T = syncedMs(d[DL_SYNC], d[DL_TIME], d[DL_NOTE], bpm_) * fs_ * 0.001f;
        if (dlyTime_ <= 0.f) dlyTime_ = T;
        float target = p_.dlyOn ? (float)d[DL_SEND] / 127.f : 0.f;
        float fb = (float)d[DL_FB] * 0.01f;
        float a = dampA(d[DL_DAMP], invFs_);
        float g1l, g1r, g2l, g2r;
        panGains(d[DL_PAN1], g1l, g1r);
        panGains(d[DL_PAN2], g2l, g2r);
        float v1 = (float)d[DL_LV1] / 127.f, v2 = (float)d[DL_LV2] / 127.f;
        float lev = (float)d[DL_LEV] * 0.01f;
        rvFromDelay = (float)d[DL_RSEND] / 127.f;
        const bool ping = d[DL_MODE] == DLM_PING;
        const float slew = 12.f * invFs_;
        float g = dlySend_, dg = (target - g) * (1.f / (float)kBlock);
        for (int i = 0; i < kBlock; i++) {
            g += dg;
            dlyTime_ += (T - dlyTime_) * slew;
            float x = pre[i] * g;
            float t1, t2;
            if (ping) {
                // One bounce per TIME: input enters left, each echo crosses to the other side.
                t1 = dL_.read(dlyTime_);
                t2 = dR_.read(dlyTime_);
                dlyDampL_ += (t2 - dlyDampL_) * a;
                dL_.push(x + sat(dlyDampL_ * fb));
                dR_.push(t1);
            } else {
                // Second tap at TIME, first tap halfway (estimate).
                t1 = dL_.read(dlyTime_ * 0.5f);
                t2 = dL_.read(dlyTime_);
                dlyDampL_ += (t2 - dlyDampL_) * a;
                dL_.push(x + sat(dlyDampL_ * fb));
            }
            float w1 = t1 * v1, w2 = t2 * v2;
            float wl = (w1 * g1l + w2 * g2l) * lev, wr = (w1 * g1r + w2 * g2r) * lev;
            L[i] += wl;
            R[i] += wr;
            dlyRv[i] = 0.5f * (wl + wr);
        }
        dlySend_ = target;
    }

    // Reverb: send for the next block, and this block's return. Switching off stops the send only.
    float target = p_.revOn ? (float)p_.r[RV_SEND] / 127.f : 0.f;
    float g = revMix_, dg = (target - g) * (1.f / (float)kBlock);
    for (int i = 0; i < kBlock; i++) {
        g += dg;
        float s = pre[i];
        if (dlyRun) s += dlyRv[i] * rvFromDelay;
        rvIn_[i] = s * g * 0.5f;
        L[i] += rvOutL_[i];
        R[i] += rvOutR_[i];
    }
    revMix_ = target;
}

}  // namespace j8
