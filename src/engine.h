// The sound engine: voices, assigner, arpeggiator, LFOs. No hardware dependency.
//
// Threading: prepareBlock() runs on the audio task; render() may then run for
// separate voice ranges on two cores at once, each with its own output buffers.
// The UI writes single bytes into patch[] and global[] directly (byte writes are
// atomic and the engine re-reads them each block). Notes go through pushEvent(),
// a single-producer/single-consumer queue.
#pragma once
#include <atomic>
#include "dsp_math.h"
#include "params.h"
#include "fx.h"

namespace j8 {

constexpr uint8_t kAnyChannel = 0xFF;   // not from MIDI: goes to part 1

struct Event {
    enum Type : uint8_t { NoteOn, NoteOff, Bend, Mod, Sustain, AllOff } type;
    uint8_t a;
    int16_t b;
    uint8_t ch = kAnyChannel;   // MIDI channel 0-15
};

// One VCO. Band-limited with polyBLEP/polyBLAMP using one sample of lookahead.
struct Vco {
    float phase = 0, next = 0;
    bool high = true;
};

// Per-block values shared by all the voices of a layer.
struct LayerCtl {
    float lfo = 0;          // main LFO, -1..1
    float lfoPhase = 0;
    float lfoHold = 0;
    float modLfoPhase = 0;
    float modDepth = 0;     // bender-section mod after rise time
    float bendVco1 = 0, bendVco2 = 0, bendVcf = 0;  // octaves
    float modVcoOct = 0, modVcfOct = 0;
    // Smoothed copies of the controls that would otherwise zipper
    float cutS = 0, resS = 0, hpfS = 0, mixS = 0, lvlS = 0, xmS = 0, pwS = 0;
    bool first = true;
};

struct Voice {
    // Assigner state
    int8_t layer = 0;
    int8_t note = -1;
    bool gate = false;
    uint32_t age = 0;
    // Sound state
    bool active = false;
    bool fresh = true;
    Vco v1, v2;
    float env1 = 0, env2 = 0;
    uint8_t st1 = 3, st2 = 3;   // 0 attack 1 decay 2 sustain 3 release/idle
    float pitch = 60, glideFrom = 60;
    float lfoGain = 1, delayRun = 0;
    float z[4] = {0, 0, 0, 0};
    float hpZ = 0;
    float gPrev = 0.1f, hpPrev = 0.01f, vcaPrev = 0, inc1Prev = 0, inc2Prev = 0, invPrev = 1;
    float kPrev = 0, compPrev = 1, pwPrev = 0.5f;
    float o2Last = 0;
    float drift1 = 0, drift2 = 0, driftCut = 0;
    float tolP1 = 0, tolP2 = 0, tolCut = 0, tolEnv = 1;
    Rng rng;
};

class Engine {
public:
    Patch patch[2];           // 0 = upper, 1 = lower
    uint8_t global[G_COUNT];
    std::atomic<uint32_t> cpuLoadPermille{0};

    void init(float sampleRate);
    // Which parts listen to a MIDI channel: bit 0 = part 1 (Upper), bit 1 = part 2 (Lower).
    // Outside 2 PART mode the keyboard follows part 1's channel, and both bits are set together.
    int partMask(uint8_t ch) const;
    bool pushEvent(const Event& e);   // from one producer task
    void prepareBlock();              // first, on the audio task

    // Render voices [first, last) and ADD into the buffers (each kBlock long).
    void render(int first, int last, float* upper, float* lower);

    // Second core, once per block alongside its voices.
    void fxWorker() { if (tailActive_) fx.reverbBlock(); }
    Fx fx;

    // Mix buffers through the effects to stereo float. With split or dual key mode, stereoLayers puts
    // lower left and upper right, otherwise the output is mono as on the original.
    void finish(const float* upper, const float* lower, float* left, float* right);

    float voiceLevel(int i) const { return voices_[i].active ? voices_[i].env2 : 0.f; }
    bool voiceGate(int i) const { return voices_[i].gate; }
    int8_t voiceNote(int i) const { return voices_[i].note; }
    bool stereoLayers = false;
    float hostBpm = 0;        // > 0: the host's tempo, used for tempo-synced effects instead of G_TEMPO (Ju-08 plugin)

private:
    float fs_ = 48000, invFs_ = 1.f / 48000, blockDt_ = kBlock / 48000.f;
    Voice voices_[kNumVoices];
    LayerCtl ctl_[2];
    Rng lfoRng_;
    int32_t fxTail_ = 0;
    bool tailActive_ = false;

    // Event queue
    static constexpr int kQ = 128;
    Event q_[kQ];
    std::atomic<uint32_t> qHead_{0}, qTail_{0};

    // Keyboard
    struct Held { int8_t note; bool down; };
    Held held_[2][16];
    int nHeld_[2] = {0, 0};
    bool sustainPedal_[2] = {false, false};
    int bend14_[2] = {0, 0};
    uint8_t modWheel_[2] = {0, 0};
    uint8_t lastKeyMode_ = 255;
    uint8_t lastHold_[2] = {0, 0};
    uint8_t lastAssign_[2] = {255, 255};
    uint32_t ageCounter_ = 0;
    int nextVoice_[2] = {0, 0};

    // Arp
    int8_t arpKeys_[16];
    bool arpDown_[16];
    int nArp_ = 0;
    float arpPhase_ = 0;
    int arpPos_ = 0, arpDir_ = 1;
    int8_t arpSounding_ = -1;
    bool arpGate_ = false;
    uint8_t lastArpOn_ = 0;

    void handle(const Event& e);
    void keyDown(int note);
    void keyUp(int note);
    void layerKeyDown(int layer, int note);
    void layerKeyUp(int layer, int note);
    void layerRelease(int layer, int note);
    void soloUpdate(int layer, int note, bool retrig);
    void unisonUpdate(int layer, bool retrig);
    void allNotesOff();
    void partOff(int layer);
    void releaseUnheld(int layer);
    int layerFirst(int layer) const;
    int layerCount(int layer) const;
    int layerForKey(int note) const;
    bool arpEnabled() const { return global[G_ARP_ON] != 0; }
    bool arpOwnsKey(int note) const;
    void arpAdd(int note);
    void arpRemove(int note);
    void arpTick();
    int arpNext();
    void arpPlay(int note, bool on);
    void trigger(Voice& v, int note, bool glide);
    void updateLayer(int layer);
    void renderVoice(Voice& v, float* out);
    void refreshVoiceLayers();
};

}  // namespace j8
