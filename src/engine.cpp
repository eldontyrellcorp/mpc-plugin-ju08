// The Arduino IDE has no per-file compiler flags, so the speed settings live here.
#pragma GCC optimize("O2", "fast-math")
#include "engine.h"
#include <cstring>

namespace j8 {

J8_DATA float gSatTable[kSatSize + 2];
J8_DATA float gTanTable[kTanSize + 2];

void initTables() {
    for (int i = 0; i < kSatSize + 2; i++) {
        double x = -(double)kSatRange + (double)i * (2.0 * kSatRange / kSatSize);
        gSatTable[i] = (float)std::tanh(x);
    }
    for (int i = 0; i < kTanSize + 2; i++) {
        double r = (double)i / (2.0 * kTanSize);
        if (r > 0.4995) r = 0.4995;
        gTanTable[i] = (float)std::tan(3.14159265358979 * r);
    }
}

// ---------------------------------------------------------------- band limiting
J8_INLINE float thisBlep(float t) { return 0.5f * t * t; }
J8_INLINE float nextBlep(float t) { float u = 1.f - t; return -0.5f * u * u; }
J8_INLINE float nextBlamp(float t) {
    float t1 = 0.5f * t, t2 = t1 * t1, t4 = t2 * t2;
    return 0.1875f - t1 + 1.5f * t2 - t4;
}
J8_INLINE float thisBlamp(float t) { return nextBlamp(1.f - t); }

// Time since a discontinuity, in samples. invInc can lag a fast pitch change by a few
// samples, so this is kept inside the one-sample range the BLEP is defined over.
J8_INLINE float fracT(float t) { return t > 1.f ? 1.f : (t < 0.f ? 0.f : t); }

// Unified waveform ids for the oscillator core
enum OscWave : uint8_t { W_TRI, W_SAW, W_PULSE, W_SINE, W_NOISE };

J8_INLINE float triAt(float p) {            // +1 at 0, -1 at 0.5
    return p < 0.5f ? 1.f - 4.f * p : 4.f * p - 3.f;
}
J8_INLINE float sineFromTri(float t) {      // analog-style shaper: not a pure sine
    return t * (1.5f - 0.5f * t * t);
}

J8_INLINE float waveAt(OscWave w, float p, bool high) {
    switch (w) {
        case W_TRI: return triAt(p);
        case W_SAW: return 2.f * p - 1.f;
        case W_PULSE: return high ? 1.f : -1.f;
        case W_SINE: return sineFromTri(triAt(p));
        default: return 0.f;
    }
}

// Events inside the segment phase -> p. `tail` is the time in samples from the
// end of the segment to the end of this sample (non-zero when a sync reset follows).
J8_INLINE void segmentEvents(Vco& o, OscWave w, float& p, float inc, float invInc, float pw,
                             float tail, float& cur) {
    switch (w) {
        case W_SAW:
            if (p >= 1.f) {
                float t = fracT((p - 1.f) * invInc + tail);
                cur -= 2.f * thisBlep(t);
                o.next -= 2.f * nextBlep(t);
                p -= 1.f;
            }
            break;
        case W_PULSE:
            if (o.high && p >= pw && p < 1.f) {
                float t = fracT((p - pw) * invInc + tail);
                cur -= 2.f * thisBlep(t);
                o.next -= 2.f * nextBlep(t);
                o.high = false;
            }
            if (p >= 1.f) {
                if (o.high) {   // very narrow low part: fall happened in this sample too
                    float t = fracT((p - pw) * invInc + tail);
                    cur -= 2.f * thisBlep(t);
                    o.next -= 2.f * nextBlep(t);
                    o.high = false;
                }
                float t = fracT((p - 1.f) * invInc + tail);
                cur += 2.f * thisBlep(t);
                o.next += 2.f * nextBlep(t);
                p -= 1.f;
                o.high = true;
                if (p >= pw) {
                    float t2 = fracT((p - pw) * invInc + tail);
                    cur -= 2.f * thisBlep(t2);
                    o.next -= 2.f * nextBlep(t2);
                    o.high = false;
                }
            }
            break;
        case W_TRI:
        case W_SINE: {
            float slope = (w == W_TRI) ? 8.f * inc : 0.f;   // shaped sine has no corners
            if (p >= 0.5f && p - inc < 0.5f && slope != 0.f) {
                float t = fracT((p - 0.5f) * invInc + tail);
                cur += slope * thisBlamp(t);
                o.next += slope * nextBlamp(t);
            }
            if (p >= 1.f) {
                if (slope != 0.f) {
                    float t = fracT((p - 1.f) * invInc + tail);
                    cur -= slope * thisBlamp(t);
                    o.next -= slope * nextBlamp(t);
                }
                p -= 1.f;
            }
            break;
        }
        default: if (p >= 1.f) p -= 1.f; break;
    }
}

// Free-running step. Returns the output for this sample.
J8_INLINE float oscTick(Vco& o, OscWave w, float inc, float invInc, float pw) {
    float cur = o.next;
    o.next = 0.f;
    float p = o.phase + inc;
    segmentEvents(o, w, p, inc, invInc, pw, 0.f, cur);
    o.phase = p;
    o.next += waveAt(w, p, o.high);
    return cur;
}

// Hard-sync step: the master wrapped s samples before the end of this sample.
J8_INLINE float oscTickSync(Vco& o, OscWave w, float inc, float invInc, float pw, float s) {
    float cur = o.next;
    o.next = 0.f;
    float p = o.phase + inc * (1.f - s);
    segmentEvents(o, w, p, inc, invInc, pw, s, cur);
    float before = waveAt(w, p, o.high);
    o.high = true;
    float after = waveAt(w, 0.f, true);
    float d = after - before;
    cur += d * thisBlep(s);
    o.next += d * nextBlep(s);
    p = inc * s;
    o.phase = 0.f;
    segmentEvents(o, w, p, inc, invInc, pw, 0.f, cur);
    o.phase = p;
    o.next += waveAt(w, p, o.high);
    return cur;
}

// ---------------------------------------------------------------- init
void Engine::init(float sampleRate) {
    initTables();
    fs_ = sampleRate;
    invFs_ = 1.f / sampleRate;
    blockDt_ = (float)kBlock * invFs_;
    defaultGlobals(global);
    initPatch(patch[0], "INIT");
    initPatch(patch[1], "INIT");
    // Fixed per-voice "component tolerances", scaled by the ANALOG TOLERANCE control
    static const float kP1[8] = {0.6f, -1.1f, 0.3f, 1.4f, -0.5f, 0.9f, -1.3f, 0.2f};
    static const float kP2[8] = {-0.8f, 0.5f, 1.2f, -0.2f, 1.0f, -1.4f, 0.4f, -0.6f};
    static const float kC[8] = {4.f, -7.f, 2.f, 9.f, -5.f, 6.f, -9.f, 1.f};  // cents
    static const float kE[8] = {1.01f, 0.98f, 1.02f, 0.99f, 1.03f, 0.97f, 1.0f, 1.02f};
    for (int i = 0; i < kNumVoices; i++) {
        Voice& v = voices_[i];
        v.tolP1 = kP1[i % 8]; v.tolP2 = kP2[i % 8]; v.tolCut = kC[i % 8]; v.tolEnv = kE[i % 8];
        v.rng.s = 0x12345u * (uint32_t)(i + 1) + 77u;
        v.v1.phase = (float)i * 0.137f;
        v.v2.phase = (float)i * 0.291f;
    }
    refreshVoiceLayers();
}

bool Engine::pushEvent(const Event& e) {
    uint32_t h = qHead_.load(std::memory_order_relaxed);
    uint32_t n = (h + 1) % kQ;
    if (n == qTail_.load(std::memory_order_acquire)) return false;
    q_[h] = e;
    qHead_.store(n, std::memory_order_release);
    return true;
}

// ---------------------------------------------------------------- key assign
int Engine::layerFirst(int layer) const {
    return (global[G_KEYMODE] == KM_WHOLE || layer == 0) ? 0 : kNumVoices / 2;
}
int Engine::layerCount(int) const {
    return global[G_KEYMODE] == KM_WHOLE ? kNumVoices : kNumVoices / 2;
}
int Engine::layerForKey(int note) const { return note < global[G_SPLIT] ? 1 : 0; }

void Engine::refreshVoiceLayers() {
    for (int l = 0; l < 2; l++) {
        if (global[G_KEYMODE] == KM_WHOLE && l == 1) continue;
        for (int i = 0; i < layerCount(l); i++) voices_[layerFirst(l) + i].layer = (int8_t)l;
    }
}

void Engine::allNotesOff() {
    for (auto& v : voices_) { v.gate = false; if (v.st1 != 3) v.st1 = 3; if (v.st2 != 3) v.st2 = 3; }
    nHeld_[0] = nHeld_[1] = 0;
    nArp_ = 0;
    arpSounding_ = -1;
    arpGate_ = false;
}

void Engine::trigger(Voice& v, int note, bool glide) {
    int layer = v.layer;
    uint8_t pm = global[G_PORTA_MODE];
    bool porta = global[G_PORTA_TIME] > 0 && (pm == PM_ON || (pm == PM_UPPER && layer == 0));
    // Polyphonic portamento: every voice glides from the last pitch it played.
    v.glideFrom = porta ? v.pitch : (float)note;
    v.pitch = v.glideFrom;
    v.note = (int8_t)note;
    v.gate = true;
    v.st1 = 0;
    v.st2 = 0;
    v.age = ++ageCounter_;
    v.delayRun = 0;
    if (!v.active) {
        v.active = true;
        v.env1 = v.env2 = 0;
        v.z[0] = v.z[1] = v.z[2] = v.z[3] = 0;
        v.hpZ = 0;
        v.vcaPrev = 0;
        v.fresh = true;
    }
}

void Engine::soloUpdate(int layer, int note, bool retrig) {
    Voice& v = voices_[layerFirst(layer)];
    for (int i = 1; i < layerCount(layer); i++) {
        Voice& o = voices_[layerFirst(layer) + i];
        if (o.gate) { o.gate = false; o.st1 = 3; o.st2 = 3; }
    }
    if (retrig || !v.gate) trigger(v, note, true);
    else v.note = (int8_t)note;   // key released back onto a held note: pitch moves, no new envelope
}

void Engine::unisonUpdate(int layer, bool retrig) {
    // All voices on one key; they divide between keys as more are held.
    int nv = layerCount(layer), first = layerFirst(layer), nh = nHeld_[layer];
    if (nh == 0) return;
    int n = nh < nv ? nh : nv;
    for (int i = 0; i < nv; i++) {
        Voice& v = voices_[first + i];
        int key = held_[layer][nh - n + (i % n)].note;
        if (!v.gate || (retrig && v.note != key) || (retrig && n == 1)) trigger(v, key, true);
        else v.note = (int8_t)key;
    }
}

void Engine::layerKeyDown(int layer, int note) {
    int& nh = nHeld_[layer];
    Held* h = held_[layer];
    for (int i = 0; i < nh; i++) {
        if (h[i].note == note) {
            for (int j = i; j + 1 < nh; j++) h[j] = h[j + 1];
            nh--;
            break;
        }
    }
    if (nh >= 16) { for (int j = 0; j + 1 < nh; j++) h[j] = h[j + 1]; nh--; }
    h[nh].note = (int8_t)note;
    h[nh].down = true;
    nh++;

    uint8_t as = patch[layer].v[P_ASSIGN];
    if (as == AS_SOLO) { soloUpdate(layer, note, true); return; }
    if (as == AS_UNISON) { unisonUpdate(layer, true); return; }

    int first = layerFirst(layer), nv = layerCount(layer);
    Voice* pick = nullptr;
    // The same key sounding already: re-use its voice.
    for (int i = 0; i < nv; i++) {
        Voice& v = voices_[first + i];
        if (v.note == note && v.gate) { pick = &v; break; }
    }
    if (!pick && as == AS_POLY2) {
        // POLY-2: a key returns to the voice it used last, when that voice is free.
        for (int i = 0; i < nv; i++) {
            Voice& v = voices_[first + i];
            if (v.note == note && !v.gate) { pick = &v; break; }
        }
    }
    if (!pick) {
        // Rotate through the voices so release tails ring on.
        int start = nextVoice_[layer];
        for (int i = 0; i < nv; i++) {
            Voice& v = voices_[first + (start + i) % nv];
            if (!v.gate) { pick = &v; nextVoice_[layer] = (start + i + 1) % nv; break; }
        }
    }
    if (!pick) {
        // All held: take the oldest.
        Voice* oldest = &voices_[first];
        for (int i = 1; i < nv; i++)
            if (voices_[first + i].age < oldest->age) oldest = &voices_[first + i];
        pick = oldest;
    }
    trigger(*pick, note, false);
}

void Engine::layerRelease(int layer, int note) {
    uint8_t as = patch[layer].v[P_ASSIGN];
    int nh = nHeld_[layer];
    if (as == AS_SOLO) {
        if (nh > 0) { soloUpdate(layer, held_[layer][nh - 1].note, false); return; }
    } else if (as == AS_UNISON) {
        if (nh > 0) { unisonUpdate(layer, false); return; }
    }
    int first = layerFirst(layer), nv = layerCount(layer);
    for (int i = 0; i < nv; i++) {
        Voice& v = voices_[first + i];
        if (v.gate && (as == AS_UNISON || as == AS_SOLO || v.note == note)) {
            v.gate = false;
            v.st1 = 3;
            v.st2 = 3;
        }
    }
}

void Engine::layerKeyUp(int layer, int note) {
    int& nh = nHeld_[layer];
    Held* h = held_[layer];
    bool holding = sustainPedal_[layer] || global[layer == 0 ? G_HOLD_U : G_HOLD_L];
    for (int i = 0; i < nh; i++) {
        if (h[i].note == note) {
            if (holding) { h[i].down = false; return; }
            for (int j = i; j + 1 < nh; j++) h[j] = h[j + 1];
            nh--;
            break;
        }
    }
    layerRelease(layer, note);
}

bool Engine::arpOwnsKey(int note) const {
    if (!arpEnabled()) return false;
    if (global[G_KEYMODE] == KM_SPLIT) return note < global[G_SPLIT];  // arp plays the lower zone
    return true;
}

void Engine::keyDown(int note) {
    if (arpOwnsKey(note)) { arpAdd(note); return; }
    switch (global[G_KEYMODE]) {
        case KM_WHOLE: layerKeyDown(0, note); break;
        case KM_DUAL: layerKeyDown(0, note); layerKeyDown(1, note); break;
        default: layerKeyDown(layerForKey(note), note); break;
    }
}

void Engine::keyUp(int note) {
    if (arpOwnsKey(note)) { arpRemove(note); return; }
    switch (global[G_KEYMODE]) {
        case KM_WHOLE: layerKeyUp(0, note); break;
        case KM_DUAL: layerKeyUp(0, note); layerKeyUp(1, note); break;
        default: layerKeyUp(layerForKey(note), note); break;
    }
}

// ---------------------------------------------------------------- arpeggiator
void Engine::arpAdd(int note) {
    for (int i = 0; i < nArp_; i++) if (arpKeys_[i] == note) { arpDown_[i] = true; return; }
    if (nArp_ >= 16) return;
    int i = nArp_++;
    while (i > 0 && arpKeys_[i - 1] > note) { arpKeys_[i] = arpKeys_[i - 1]; arpDown_[i] = arpDown_[i - 1]; i--; }
    arpKeys_[i] = (int8_t)note;
    arpDown_[i] = true;
}

void Engine::arpRemove(int note) {
    bool holding = sustainPedal_[0] || global[G_HOLD_U] || global[G_HOLD_L];
    for (int i = 0; i < nArp_; i++) {
        if (arpKeys_[i] != note) continue;
        if (holding) { arpDown_[i] = false; return; }
        for (int j = i; j + 1 < nArp_; j++) { arpKeys_[j] = arpKeys_[j + 1]; arpDown_[j] = arpDown_[j + 1]; }
        nArp_--;
        return;
    }
}

int Engine::arpNext() {
    int span = nArp_ * (global[G_ARP_RANGE] + 1);
    if (span <= 0) return -1;
    int idx;
    switch (global[G_ARP_MODE]) {
        case ARP_UP:   arpPos_ = (arpPos_ + 1) % span; idx = arpPos_; break;
        case ARP_DOWN: arpPos_ = (arpPos_ + span - 1) % span; idx = arpPos_; break;
        case ARP_UPDOWN:
            if (span == 1) { arpPos_ = 0; }
            else {
                arpPos_ += arpDir_;
                if (arpPos_ >= span) { arpPos_ = span - 2; arpDir_ = -1; }
                else if (arpPos_ < 0) { arpPos_ = 1; arpDir_ = 1; }
            }
            idx = arpPos_;
            break;
        default: idx = (int)(lfoRng_.next() % (uint32_t)span); break;
    }
    if (idx >= span) idx = span - 1;
    return arpKeys_[idx % nArp_] + 12 * (idx / nArp_);
}

void Engine::arpPlay(int note, bool on) {
    uint8_t km = global[G_KEYMODE];
    int a = 0, b = 0;
    if (km == KM_DUAL) { a = 0; b = 1; } else if (km == KM_SPLIT) { a = 1; b = 1; }
    for (int l = a; l <= b; l++) {
        if (on) layerKeyDown(l, note);
        else {
            int& nh = nHeld_[l];
            Held* h = held_[l];
            for (int i = 0; i < nh; i++) if (h[i].note == note) {
                for (int j = i; j + 1 < nh; j++) h[j] = h[j + 1];
                nh--;
                break;
            }
            layerRelease(l, note);
        }
    }
}

void Engine::arpTick() {
    bool on = arpEnabled();
    if (!on) {
        if (lastArpOn_) { if (arpSounding_ >= 0) arpPlay(arpSounding_, false); arpSounding_ = -1; arpGate_ = false; nArp_ = 0; }
        lastArpOn_ = 0;
        return;
    }
    lastArpOn_ = 1;
    arpPhase_ += mapArpRate(global[G_ARP_RATE]) * blockDt_;
    if (arpGate_ && arpPhase_ >= 0.5f) {
        if (arpSounding_ >= 0) arpPlay(arpSounding_, false);
        arpSounding_ = -1;
        arpGate_ = false;
    }
    if (arpPhase_ >= 1.f) {
        arpPhase_ -= 1.f;
        int n = arpNext();
        if (n >= 0 && n < 128) {
            arpPlay(n, true);
            arpSounding_ = (int8_t)n;
            arpGate_ = true;
        }
    }
}

// ---------------------------------------------------------------- events
int Engine::partMask(uint8_t ch) const {
    auto hears = [&](uint8_t setting) { return setting == 0 || setting == ch + 1; };
    if (global[G_KEYMODE] == KM_2PART) {
        if (ch == kAnyChannel) return 1;
        return (hears(global[G_CH_U]) ? 1 : 0) | (hears(global[G_CH_L]) ? 2 : 0);
    }
    return (ch == kAnyChannel || hears(global[G_CH_U])) ? 3 : 0;
}

void Engine::releaseUnheld(int l) {
    for (int i = nHeld_[l] - 1; i >= 0; i--) {
        if (!held_[l][i].down) {
            int note = held_[l][i].note;
            for (int j = i; j + 1 < nHeld_[l]; j++) held_[l][j] = held_[l][j + 1];
            nHeld_[l]--;
            layerRelease(l, note);
        }
    }
}

void Engine::partOff(int l) {
    int first = layerFirst(l), nv = layerCount(l);
    for (int i = 0; i < nv; i++) {
        Voice& v = voices_[first + i];
        if (v.gate) { v.gate = false; v.st1 = 3; v.st2 = 3; }
    }
    nHeld_[l] = 0;
    if (l == 0) { nArp_ = 0; arpSounding_ = -1; arpGate_ = false; }
}

void Engine::handle(const Event& e) {
    const int mask = partMask(e.ch);
    if (!mask) return;
    const bool twoPart = global[G_KEYMODE] == KM_2PART;
    switch (e.type) {
        case Event::NoteOn:
            if (!twoPart) { keyDown(e.a); break; }
            // The arpeggiator belongs to part 1.
            if (mask & 1) { if (arpEnabled()) arpAdd(e.a); else layerKeyDown(0, e.a); }
            if (mask & 2) layerKeyDown(1, e.a);
            break;
        case Event::NoteOff:
            if (!twoPart) { keyUp(e.a); break; }
            if (mask & 1) { if (arpEnabled()) arpRemove(e.a); else layerKeyUp(0, e.a); }
            if (mask & 2) layerKeyUp(1, e.a);
            break;
        case Event::Bend:
            for (int l = 0; l < 2; l++) if (mask & (1 << l)) bend14_[l] = e.b;
            break;
        case Event::Mod:
            for (int l = 0; l < 2; l++) if (mask & (1 << l)) modWheel_[l] = e.a;
            break;
        case Event::Sustain:
            for (int l = 0; l < 2; l++) {
                if (!(mask & (1 << l))) continue;
                bool was = sustainPedal_[l];
                sustainPedal_[l] = e.a >= 64;
                if (was && !sustainPedal_[l]) {
                    if (!global[l == 0 ? G_HOLD_U : G_HOLD_L]) releaseUnheld(l);
                    if (l == 0) for (int i = nArp_ - 1; i >= 0; i--) if (!arpDown_[i]) arpRemove(arpKeys_[i]);
                }
            }
            break;
        case Event::AllOff:
            if (!twoPart || e.ch == kAnyChannel) { allNotesOff(); break; }
            for (int l = 0; l < 2; l++) if (mask & (1 << l)) partOff(l);
            break;
    }
}

// ---------------------------------------------------------------- per block
void Engine::updateLayer(int l) {
    LayerCtl& c = ctl_[l];
    const uint8_t* v = patch[l].v;

    // Main LFO
    float inc = mapLfoRate(v[P_LFO_RATE]) * blockDt_;
    float prev = c.lfoPhase;
    c.lfoPhase += inc;
    bool wrapped = false;
    if (c.lfoPhase >= 1.f) { c.lfoPhase -= 1.f; wrapped = true; }
    (void)prev;
    switch (v[P_LFO_WAVE]) {
        case LFO_SINE: c.lfo = sinTurn(c.lfoPhase); break;
        case LFO_SAW: c.lfo = 1.f - 2.f * c.lfoPhase; break;
        case LFO_SQUARE: c.lfo = c.lfoPhase < 0.5f ? 1.f : -1.f; break;
        default:
            if (wrapped) c.lfoHold = lfoRng_.bipolar();
            c.lfo = c.lfoHold;
            break;
    }

    // Bender section
    c.modLfoPhase += 6.f * blockDt_;
    if (c.modLfoPhase >= 1.f) c.modLfoPhase -= 1.f;
    float wheel = (float)modWheel_[l] / 127.f;
    float rise = mapRiseTime(global[G_MOD_RISE]);
    float step = rise > 0.001f ? blockDt_ / rise : 1.f;
    if (c.modDepth < wheel) c.modDepth = wheel < c.modDepth + step ? wheel : c.modDepth + step;
    else c.modDepth = wheel;
    float bend = (float)bend14_[l] / 8192.f;
    float bo = bend * (float)global[G_BEND_VCO] / 255.f;           // full sens = +-1 octave
    c.bendVco1 = global[G_BEND_V1] ? bo : 0.f;
    c.bendVco2 = global[G_BEND_V2] ? bo : 0.f;
    c.bendVcf = global[G_BEND_VCFSW] ? bend * 4.f * (float)global[G_BEND_VCF] / 255.f : 0.f;
    float ml = sinTurn(c.modLfoPhase) * c.modDepth;
    c.modVcoOct = global[G_MOD_VCOSW] ? ml * 0.5f * (float)global[G_MOD_VCO] / 255.f : 0.f;
    c.modVcfOct = global[G_MOD_VCFSW] ? ml * 2.f * (float)global[G_MOD_VCF] / 255.f : 0.f;

    // Parameter smoothing, about 8 ms
    float cut = (float)v[P_VCF_CUT], res = mapReso(v[P_VCF_RES]), hp = (float)v[P_HPF];
    float mix = (float)v[P_MIX] / 255.f, lvl = (float)v[P_VCA_LEVEL] / 255.f;
    float xm = mapXmodOct(v[P_V1_XMOD]), pw = (float)v[P_VM_PW] / 255.f;
    lvl = lvl * lvl;
    if (c.first) {
        c.cutS = cut; c.resS = res; c.hpfS = hp; c.mixS = mix; c.lvlS = lvl; c.xmS = xm; c.pwS = pw;
        c.first = false;
    } else {
        const float a = 0.08f;
        c.cutS += (cut - c.cutS) * a; c.resS += (res - c.resS) * a; c.hpfS += (hp - c.hpfS) * a;
        c.mixS += (mix - c.mixS) * a; c.lvlS += (lvl - c.lvlS) * a; c.xmS += (xm - c.xmS) * a;
        c.pwS += (pw - c.pwS) * a;
    }
}

void Engine::prepareBlock() {
    uint8_t km = global[G_KEYMODE];
    if (km != lastKeyMode_) {
        allNotesOff();
        lastKeyMode_ = km;
        refreshVoiceLayers();
    }
    for (int l = 0; l < 2; l++) {
        uint8_t h = global[l == 0 ? G_HOLD_U : G_HOLD_L];
        if (lastHold_[l] && !h && !sustainPedal_[l]) {
            releaseUnheld(l);
            for (int i = nArp_ - 1; i >= 0; i--) if (!arpDown_[i]) arpRemove(arpKeys_[i]);
        }
        lastHold_[l] = h;
        if (patch[l].v[P_ASSIGN] != lastAssign_[l]) {
            if (lastAssign_[l] != 255) {
                // Assign mode changed: free the voices so the allocator starts clean.
                int first = layerFirst(l), nv = layerCount(l);
                if (km != KM_WHOLE || l == 0) {
                    for (int i = 0; i < nv; i++) {
                        Voice& v = voices_[first + i];
                        if (v.gate) { v.gate = false; v.st1 = 3; v.st2 = 3; }
                    }
                    nHeld_[l] = 0;
                }
            }
            lastAssign_[l] = patch[l].v[P_ASSIGN];
        }
    }
    uint32_t t = qTail_.load(std::memory_order_relaxed);
    uint32_t h = qHead_.load(std::memory_order_acquire);
    while (t != h) {
        handle(q_[t]);
        t = (t + 1) % kQ;
    }
    qTail_.store(t, std::memory_order_release);
    arpTick();
    updateLayer(0);
    updateLayer(1);
    fx.control(patch[0].fx, hostBpm > 0.f ? hostBpm : 40.f + (float)global[G_TEMPO]);
}

// ---------------------------------------------------------------- envelope
// RC-style exponential segments, one step per block; the VCA ramps linearly between
// block values so even the 1 ms attack is smooth.
J8_INLINE float envCoef(float tau, float dt) {
    // exp(-dt/tau) = 2^(-dt/(tau*ln2))
    return pow2f(-1.442695f * dt / tau);
}

// tauScale: tolerance and key-follow multiplier
static J8_HOT float stepEnv(float e, uint8_t& st, uint8_t aV, uint8_t dV, uint8_t sV, uint8_t rV,
                            float tScale, float dt) {
    const float sus = (float)sV / 255.f;
    switch (st) {
        case 0: {
            // Charge toward 1.25 and end at 1.0, as an RC attack with a threshold does.
            float T = mapAttack(aV) * tScale;
            float tau = T * 0.62133f;       // 1 / ln(1.25 / 0.25)
            float a = envCoef(tau, dt);
            e = 1.25f + (e - 1.25f) * a;
            if (e >= 1.f) { e = 1.f; st = 1; }
            break;
        }
        case 1: {
            float T = mapDecay(dV) * tScale;
            float a = envCoef(T * 0.21715f, dt);    // T = time to reach 1% of the span
            e = sus + (e - sus) * a;
            if (e - sus < 0.0005f) st = 2;
            break;
        }
        case 2:
            e += (sus - e) * 0.05f;
            break;
        default: {
            float T = mapDecay(rV) * tScale;
            float a = envCoef(T * 0.21715f, dt);
            e *= a;
            break;
        }
    }
    return e;
}

// ---------------------------------------------------------------- one voice
// Level into the OTA stages. Sets how early the filter saturates; 0.5 keeps full-level
// sawtooths mostly clean at low resonance and audibly driven at high resonance.
static constexpr float kVcfInput = 0.5f;
static constexpr float kVcfMakeup = 2.0f;

J8_HOT void Engine::renderVoice(Voice& v, float* out) {
    const int l = v.layer;
    const uint8_t* P = patch[l].v;
    const LayerCtl& c = ctl_[l];
    const float dt = blockDt_;
    const float vintage = (float)global[G_VINTAGE] / 128.f;

    // Key follow on envelope times: longer below C4, shorter above.
    const float keyRel = ((float)v.note - 60.f) / 12.f;
    const float keyT = pow2f(-keyRel * 0.5f);
    const float t1 = v.tolEnv * (P[P_E1_KEY] ? keyT : 1.f);
    const float t2 = v.tolEnv * (P[P_E2_KEY] ? keyT : 1.f);
    v.env1 = stepEnv(v.env1, v.st1, P[P_E1_A], P[P_E1_D], P[P_E1_S], P[P_E1_R], t1, dt);
    v.env2 = stepEnv(v.env2, v.st2, P[P_E2_A], P[P_E2_D], P[P_E2_S], P[P_E2_R], t2, dt);

    if (!v.gate && v.st2 == 3 && v.env2 < 0.00005f) {
        v.active = false;
        v.env2 = v.env1 = 0;
        return;
    }

    const float e1 = P[P_E1_POL] ? -v.env1 : v.env1;

    // LFO delay: amplitude ramps to full over the delay time after key-on.
    float delayT = mapLfoDelay(P[P_LFO_DELAY]);
    if (v.delayRun < delayT) {
        v.delayRun += dt;
        v.lfoGain = v.delayRun / (delayT > 1e-4f ? delayT : 1e-4f);
        if (v.lfoGain > 1.f) v.lfoGain = 1.f;
    } else v.lfoGain = 1.f;
    const float lfo = c.lfo * v.lfoGain;

    // Portamento: RC glide
    uint8_t pm = global[G_PORTA_MODE];
    bool porta = global[G_PORTA_TIME] > 0 && (pm == PM_ON || (pm == PM_UPPER && l == 0));
    if (porta) {
        float a = envCoef(mapPortaTime(global[G_PORTA_TIME]) * 0.4f, dt);
        v.pitch = (float)v.note + (v.pitch - (float)v.note) * a;
    } else v.pitch = (float)v.note;

    // Slow pitch drift (random walk with a leak), in cents.
    {
        v.drift1 += v.rng.bipolar() * 0.03f - v.drift1 * 0.0006f;
        v.drift2 += v.rng.bipolar() * 0.03f - v.drift2 * 0.0006f;
        v.driftCut += v.rng.bipolar() * 0.05f - v.driftCut * 0.0006f;
    }

    // Pitch, in octaves relative to A4
    const float tune = ((float)global[G_TUNE] - 128.f) * (50.f / 127.f / 1200.f);
    const float base = (v.pitch - 69.f) / 12.f + tune;
    const int dest = P[P_VM_DEST];
    const float lfoO = lfo * mapVcoLfoOct(P[P_VM_LFO]) + c.modVcoOct;
    const float envO = e1 * mapVcoEnvOct(P[P_VM_ENV]);
    const float m1 = dest == DEST_VCO2 ? 0.f : lfoO + envO;
    const float m2 = dest == DEST_VCO1 ? 0.f : lfoO + envO;
    const float cent = 1.f / 1200.f;
    const float o1 = base + (float)((int)P[P_V1_RANGE] - 1) + m1 + c.bendVco1 +
                     (v.tolP1 + v.drift1) * vintage * cent;
    float o2;
    if (P[P_V2_LOW]) {
        // LOW FREQ: fixed-rate, ignores the key. Range steps in octaves up from 0.25 Hz.
        o2 = -10.7814f + 2.f * (float)P[P_V2_RANGE] + mapV2TuneCents(P[P_V2_TUNE]) * 4.f * cent + m2;
    } else {
        o2 = base + (float)((int)P[P_V2_RANGE] - 1) + m2 + c.bendVco2 +
             (mapV2TuneCents(P[P_V2_TUNE]) + (v.tolP2 + v.drift2) * vintage) * cent;
    }
    const float maxInc = 0.45f;
    float inc1 = clampf(440.f * pow2f(o1) * invFs_, 1e-6f, maxInc);
    float inc2 = clampf(440.f * pow2f(o2) * invFs_, 1e-6f, maxInc);

    // VCF cutoff
    const float env4f = P[P_VCF_ENVSEL] ? v.env2 : e1;
    const float keyAmt = (float)P[P_VCF_KEY] / 255.f * 1.2f;
    const float cutBase = 16.f * pow2f(c.cutS / 255.f * 10.2877f);   // log2(20000/16)
    const float cutO = env4f * mapVcfEnvOct(P[P_VCF_ENV]) +
                       lfo * mapVcfLfoOct(P[P_VCF_LFO]) + c.modVcfOct + c.bendVcf +
                       ((float)v.note - 60.f) / 12.f * keyAmt +
                       (v.tolCut + v.driftCut) * vintage * cent;
    float fc = cutBase * pow2f(cutO);
    const float fsF = fs_ * (float)kFilterOS;
    fc = clampf(fc, 8.f, 0.46f * fsF);
    const float gT = tanPi(fc / fsF);
    const float gNew = gT / (1.f + gT);

    const float kNew = c.resS;
    const float g2 = gNew * gNew;
    const float invNew = 1.f / (1.f + kNew * g2 * g2);
    // Passband level drop at high resonance is partly made up by raising the input.
    const float compNew = 1.f + 0.35f * kNew;

    // HPF, one pole, no resonance
    float hpFc = c.hpfS < 0.5f ? 6.f : 16.f * pow2f(c.hpfS / 255.f * 7.6439f);
    const float hT = tanPi(hpFc * invFs_);
    const float hpNew = hT / (1.f + hT);

    // VCA
    static const float kTrem[4] = {0.f, 0.3f, 0.6f, 1.f};
    const float trem = 1.f - kTrem[P[P_VCA_LFO] & 3] * (0.5f - 0.5f * lfo);
    const float vcaNew = v.env2 * c.lvlS * trem;

    // Pulse width
    float pwNew = 0.5f;
    switch (P[P_VM_PWMODE]) {
        case PW_MAN: pwNew = 0.5f - 0.45f * c.pwS; break;
        case PW_LFO: pwNew = 0.5f - 0.45f * c.pwS * (0.5f + 0.5f * lfo); break;
        default: pwNew = 0.5f - 0.45f * c.pwS * clampf(v.env1, 0.f, 1.f); break;
    }

    // A voice starting from silence begins on its new values, not a ramp from old ones.
    if (v.fresh) {
        v.fresh = false;
        v.inc1Prev = inc1; v.inc2Prev = inc2; v.gPrev = gNew; v.invPrev = invNew;
        v.kPrev = kNew; v.compPrev = compNew; v.hpPrev = hpNew; v.pwPrev = pwNew;
    }

    // Per-sample ramps
    const float rs = 1.f / (float)kBlock;
    const float dInc1 = (inc1 - v.inc1Prev) * rs, dInc2 = (inc2 - v.inc2Prev) * rs;
    const float dG = (gNew - v.gPrev) * rs, dInv = (invNew - v.invPrev) * rs;
    const float dK = (kNew - v.kPrev) * rs, dComp = (compNew - v.compPrev) * rs;
    const float dHp = (hpNew - v.hpPrev) * rs, dVca = (vcaNew - v.vcaPrev) * rs;
    const float dPw = (pwNew - v.pwPrev) * rs;
    float i1 = v.inc1Prev, i2 = v.inc2Prev, G = v.gPrev, inv = v.invPrev, K = v.kPrev, comp = v.compPrev;
    float hpG = v.hpPrev, vca = v.vcaPrev, pw = v.pwPrev;

    static const OscWave kW1[4] = {W_TRI, W_SAW, W_PULSE, W_PULSE};
    static const OscWave kW2[4] = {W_SINE, W_SAW, W_PULSE, W_NOISE};
    const OscWave w1 = kW1[P[P_V1_WAVE] & 3];
    const OscWave w2 = kW2[P[P_V2_WAVE] & 3];
    const bool sq1 = (P[P_V1_WAVE] & 3) == V1_SQUARE;
    const bool pulse1 = w1 == W_PULSE, pulse2 = w2 == W_PULSE;
    const bool sync = P[P_V2_SYNC] != 0 && w2 != W_NOISE;
    const float xm = c.xmS;
    const bool xmOn = xm > 0.0005f;
    const float mix2 = c.mixS, mix1 = 1.f - c.mixS;
    float o2Last = v.o2Last;
    float z1 = v.z[0], z2 = v.z[1], z3 = v.z[2], z4 = v.z[3];
    float hpz = v.hpZ;
    const bool tap2 = P[P_VCF_SLOPE] == 0;

    // OTA drive: input difference scaled before tanh. Lower = cleaner.
    const float drv = 0.55f, invDrv = 1.f / drv;

    float inv1 = 1.f / (i1 > 1e-6f ? i1 : 1e-6f);
    float inv2 = 1.f / (i2 > 1e-6f ? i2 : 1e-6f);
    int recip = 0;

    for (int n = 0; n < kBlock; n++) {
        i1 += dInc1; i2 += dInc2; G += dG; inv += dInv; K += dK; comp += dComp;
        hpG += dHp; vca += dVca; pw += dPw;
        // 1/inc only matters on a BLEP; refresh it a few times per block.
        if ((recip++ & 7) == 0) {
            inv1 = 1.f / i1;
            inv2 = 1.f / i2;
        }

        // --- VCO-1, with cross mod from VCO-2's last output
        float inc1s = i1, inv1s = inv1;
        if (xmOn) {
            float mul = pow2f(xm * o2Last);
            inc1s = i1 * mul;
            if (inc1s > maxInc) inc1s = maxInc;
            inv1s = 1.f / inc1s;
        }
        const float phaseBefore = v.v1.phase;
        float s1 = oscTick(v.v1, w1, inc1s, inv1s, sq1 ? 0.5f : pw);
        const bool wrapped = v.v1.phase < phaseBefore;

        // --- VCO-2, hard-synced to VCO-1
        float s2;
        if (w2 == W_NOISE) {
            s2 = v.rng.bipolar() * 0.7f;
        } else if (sync && wrapped) {
            float s = v.v1.phase * inv1s;
            if (s > 1.f) s = 1.f;
            s2 = oscTickSync(v.v2, w2, i2, inv2, pw, s);
        } else {
            s2 = oscTick(v.v2, w2, i2, inv2, pw);
        }
        o2Last = s2;

        // --- Mixer into the HPF (one-pole TPT highpass)
        // Pulse DC is removed here, as AC coupling would, so PWM does not move the level.
        float x = (s1 - (pulse1 ? (sq1 ? 0.f : 2.f * pw - 1.f) : 0.f)) * mix1 +
                  (s2 - (pulse2 ? 2.f * pw - 1.f : 0.f)) * mix2;
        float hv = (x - hpz) * hpG;
        float hl = hv + hpz;
        hpz = hl + hv;
        x = (x - hl) * kVcfInput;

        // --- VCF: four OTA integrators in series, zero-delay feedback from stage 4.
        float y2 = 0.f, y4 = 0.f;
        for (int os = 0; os < kFilterOS; os++) {
            const float oneG = 1.f - G;
            const float S = oneG * (z4 + G * (z3 + G * (z2 + G * z1)));
            const float u = (x * comp - K * S) * inv;
            float vv, y;
            vv = G * invDrv * sat((u - z1) * drv); y = vv + z1; z1 = y + vv;
            vv = G * invDrv * sat((y - z2) * drv); y = vv + z2; z2 = y + vv; y2 = y;
            vv = G * invDrv * sat((y - z3) * drv); y = vv + z3; z3 = y + vv;
            vv = G * invDrv * sat((y - z4) * drv); y = vv + z4; z4 = y + vv; y4 = y;
        }
        out[n] += (tap2 ? y2 : y4) * vca * kVcfMakeup;
    }

    v.inc1Prev = inc1; v.inc2Prev = inc2; v.gPrev = gNew; v.invPrev = invNew;
    v.kPrev = kNew; v.compPrev = compNew; v.hpPrev = hpNew; v.vcaPrev = vcaNew; v.pwPrev = pwNew;
    v.o2Last = o2Last;
    v.z[0] = z1; v.z[1] = z2; v.z[2] = z3; v.z[3] = z4;
    v.hpZ = hpz;
}

J8_HOT void Engine::render(int first, int last, float* upper, float* lower) {
    for (int i = first; i < last; i++) {
        Voice& v = voices_[i];
        if (!v.active) continue;
        renderVoice(v, v.layer == 0 ? upper : lower);
    }
}

J8_HOT void Engine::finish(const float* upper, const float* lower, float* left, float* right) {
    const float vol = (float)global[G_VOLUME] / 255.f;
    const float gu = vol * (float)global[G_BAL_U] / 255.f * 0.60f;
    const float gl = vol * (float)global[G_BAL_L] / 255.f * 0.60f;
    const FxPatch& f = patch[0].fx;
    const bool anyFx = f.mfxOn || f.dlyOn || f.revOn || fxTail_ > 0;
    if (f.mfxOn || f.dlyOn || f.revOn) fxTail_ = (int32_t)(45.f / blockDt_);   // effects finish ringing out and clear themselves
    else if (fxTail_ > 0) fxTail_--;
    tailActive_ = anyFx;
    if (anyFx) {
        float mono[kBlock];
        for (int i = 0; i < kBlock; i++) mono[i] = upper[i] * gu + lower[i] * gl;
        fx.process(mono, left, right);
        for (int i = 0; i < kBlock; i++) { left[i] = sat(left[i]); right[i] = sat(right[i]); }
        return;
    }
    const bool split = stereoLayers && global[G_KEYMODE] != KM_WHOLE;
    for (int i = 0; i < kBlock; i++) {
        float u = upper[i] * gu, d = lower[i] * gl;
        if (split) {
            left[i] = sat(d);
            right[i] = sat(u);
        } else {
            float m = sat(u + d);
            left[i] = m;
            right[i] = m;
        }
    }
}

}  // namespace j8
