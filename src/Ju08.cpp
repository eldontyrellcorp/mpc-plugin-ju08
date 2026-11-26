// Ju-08 - a Jupiter-8 style polysynth for the Akai MPC, by Stephane Schmitz
//
// SPDX-License-Identifier: MIT
//
// The sound engine is the float engine of jupiter8_esp32 (engine.cpp, fx.cpp, params.cpp,
// presets.cpp, fx_params.cpp in this folder), the reference that the Mini-Ju08 firmware for the
// M-VAVE FM-1 was ported from: same parameter model, same 64 patches, same effects. The FM-1's
// sequencer is not part of it. This file is only the bridge to the MPC:
//
//   MPC parameters (0..1)  --syncParams()-->  engine.patch[2] / engine.global[]   (every process call)
//   MIDI notes, bend, wheel, pedal  ------->  engine.pushEvent()
//   engine: 8 voices -> CHORUS & MFX -> dry + delay + reverb  --> stereo out, in blocks of 32 samples
//
// Signal flow of a voice: VCO-1 + VCO-2 (sync, cross mod) -> mixer -> HPF -> 12/24 dB VCF -> VCA,
// with ENV-1, ENV-2 and the LFO, as on the panel pages.
//
// The MPC's parameters are the single source of truth: the engine's patch bytes are rewritten from
// them at the start of every process() call, a preset is "set these parameters", and the project
// state is the framework's parameter chunk. Nothing else is stored.
//
// Parameter order is permanent (see the "Identity is permanent" rule of the SDK): only append.
//   0        Volume
//   1..39    Upper patch (the 39 panel controls, in the engine's order)
//   40..61   CHORUS & MFX: on, type, 20 type-dependent slots
//   62..75   Delay: on + 13
//   76..96   Reverb: on + 20
//   97..120  Performance (key mode, arpeggio, portamento, bender, LFO mod, ...)
//   121..123 MIDI channel of each part, stereo layers
//   124..162 Lower patch
//   163      Lower Preset (choosing one loads that patch into the Lower part)

#include "mpc_plugin.h"

#include "engine.h"
#include "performances.h"

#include <cstdlib>
#include <cstring>
#include <new>

namespace {

using j8::P_COUNT;

// ---------------------------------------------------------------------------------------------
// Tables: ids, screen names and choice labels of the panel controls
// ---------------------------------------------------------------------------------------------
struct PanelDef
{
    const char* id;
    const char* name;                       // <= 12 characters, shown on the MPC
    std::vector<std::string> choices;       // empty: a continuous control, 0..255
    bool isSwitch;                          // two choices shown as Off / On
};

const PanelDef kPanel[P_COUNT] = {
    {"lfo_rate",   "LFO Rate",     {}, false},
    {"lfo_delay",  "LFO Delay",    {}, false},
    {"lfo_wave",   "LFO Wave",     {"Sine", "Saw", "Square", "Random"}, false},
    {"vm_lfo",     "VCO LFO Mod",  {}, false},
    {"vm_env",     "VCO Env Mod",  {}, false},
    {"vm_dest",    "VCO Mod Dest", {"VCO-1", "Both", "VCO-2"}, false},
    {"vm_pw",      "Pulse Width",  {}, false},
    {"vm_pwmode",  "PWM Source",   {"LFO", "Manual", "Env-1"}, false},
    {"xmod",       "Cross Mod",    {}, false},
    {"v1_range",   "VCO1 Range",   {"16'", "8'", "4'", "2'"}, false},
    {"v1_wave",    "VCO1 Wave",    {"Triangle", "Saw", "Pulse", "Square"}, false},
    {"v2_sync",    "VCO2 Sync",    {"Off", "On"}, true},
    {"v2_range",   "VCO2 Range",   {"16'", "8'", "4'", "2'"}, false},
    {"v2_low",     "VCO2 Low Frq", {"Normal", "Low"}, false},
    {"v2_tune",    "VCO2 Fine",    {}, false},
    {"v2_wave",    "VCO2 Wave",    {"Sine", "Saw", "Pulse", "Noise"}, false},
    {"mix",        "Source Mix",   {}, false},
    {"hpf",        "HPF Cutoff",   {}, false},
    {"vcf_cut",    "VCF Cutoff",   {}, false},
    {"vcf_res",    "VCF Reso",     {}, false},
    {"vcf_slope",  "VCF Slope",    {"-12 dB", "-24 dB"}, false},
    {"vcf_env",    "VCF Env Mod",  {}, false},
    {"vcf_envsel", "VCF Env Sel",  {"Env-1", "Env-2"}, false},
    {"vcf_lfo",    "VCF LFO Mod",  {}, false},
    {"vcf_key",    "VCF Key Flw",  {}, false},
    {"vca_level",  "VCA Level",    {}, false},
    {"vca_lfo",    "VCA LFO Mod",  {"0", "1", "2", "3"}, false},
    {"e1_a",       "Env1 Attack",  {}, false},
    {"e1_d",       "Env1 Decay",   {}, false},
    {"e1_s",       "Env1 Sustain", {}, false},
    {"e1_r",       "Env1 Release", {}, false},
    {"e1_key",     "Env1 Key Flw", {"Off", "On"}, true},
    {"e1_pol",     "Env1 Polar",   {"Normal", "Invert"}, false},
    {"e2_a",       "Env2 Attack",  {}, false},
    {"e2_d",       "Env2 Decay",   {}, false},
    {"e2_s",       "Env2 Sustain", {}, false},
    {"e2_r",       "Env2 Release", {}, false},
    {"e2_key",     "Env2 Key Flw", {"Off", "On"}, true},
    {"assign",     "Assign Mode",  {"Solo", "Unison", "Poly 1", "Poly 2"}, false},
};

struct GlobalDef
{
    uint8_t g;
    const char* id;
    const char* name;
    std::vector<std::string> choices;
    bool isSwitch;
    int max;                                // continuous controls: 255, the split point: 127
};

const GlobalDef kGlobals[] = {
    {j8::G_BAL_U,      "bal_u",      "Upper Level",  {}, false, 255},
    {j8::G_BAL_L,      "bal_l",      "Lower Level",  {}, false, 255},
    {j8::G_TUNE,       "tune",       "Master Tune",  {}, false, 255},
    {j8::G_KEYMODE,    "keymode",    "Key Mode",     {"Whole", "Dual", "Split", "2 Part"}, false, 0},
    {j8::G_SPLIT,      "split",      "Split Point",  {}, false, 127},
    {j8::G_HOLD_U,     "hold_u",     "Hold Upper",   {"Off", "On"}, true, 0},
    {j8::G_HOLD_L,     "hold_l",     "Hold Lower",   {"Off", "On"}, true, 0},
    {j8::G_ARP_ON,     "arp_on",     "Arpeggio",     {"Off", "On"}, true, 0},
    {j8::G_ARP_MODE,   "arp_mode",   "Arp Mode",     {"Up", "Down", "Up&Down", "Random"}, false, 0},
    {j8::G_ARP_RANGE,  "arp_range",  "Arp Range",    {"1 Oct", "2 Oct", "3 Oct", "4 Oct"}, false, 0},
    {j8::G_ARP_RATE,   "arp_rate",   "Arp Rate",     {}, false, 255},
    {j8::G_PORTA_MODE, "porta_mode", "Portamento",   {"Off", "On", "Upper"}, false, 0},
    {j8::G_PORTA_TIME, "porta_time", "Porta Time",   {}, false, 255},
    {j8::G_BEND_VCO,   "bend_vco",   "Bend VCO",     {}, false, 255},
    {j8::G_BEND_V1,    "bend_v1",    "Bend>VCO-1",   {"Off", "On"}, true, 0},
    {j8::G_BEND_V2,    "bend_v2",    "Bend>VCO-2",   {"Off", "On"}, true, 0},
    {j8::G_BEND_VCF,   "bend_vcf",   "Bend VCF",     {}, false, 255},
    {j8::G_BEND_VCFSW, "bend_vcfsw", "Bend>VCF",     {"Off", "On"}, true, 0},
    {j8::G_MOD_RISE,   "mod_rise",   "Mod Rise",     {}, false, 255},
    {j8::G_MOD_VCO,    "mod_vco",    "Mod VCO",      {}, false, 255},
    {j8::G_MOD_VCOSW,  "mod_vcosw",  "Mod>VCO",      {"Off", "On"}, true, 0},
    {j8::G_MOD_VCF,    "mod_vcf",    "Mod VCF",      {}, false, 255},
    {j8::G_MOD_VCFSW,  "mod_vcfsw",  "Mod>VCF",      {"Off", "On"}, true, 0},
    {j8::G_VINTAGE,    "vintage",    "Analog Feel",  {}, false, 255},
};
constexpr int kNumGlobals = (int)(sizeof(kGlobals) / sizeof(kGlobals[0]));

const char* const kDelayIds[j8::DL_COUNT] = {"dly_send", "dly_mode", "dly_sync", "dly_time", "dly_note", "dly_fb", "dly_damp",
    "dly_pan1", "dly_pan2", "dly_lv1", "dly_lv2", "dly_out", "dly_rsend"};
const char* const kDelayNames[j8::DL_COUNT] = {"Dly Send", "Dly Mode", "Dly Sync", "Dly Time", "Dly Note", "Dly Feedback", "Dly Hi Damp",
    "Dly Pan 1", "Dly Pan 2", "Dly Level 1", "Dly Level 2", "Dly Output", "Dly>Reverb"};
const char* const kReverbIds[j8::RV_COUNT] = {"rev_send", "rev_mode", "rev_type", "rev_pdly", "rev_time", "rev_lev",
    "shm_ldamp", "shm_hdamp", "shm_time", "shm_dens", "shm_pdly", "shm_mrate", "shm_mdepth", "shm_pch1", "shm_pch2",
    "shm_fb1", "shm_fb2", "shm_lv1", "shm_lv2", "shm_lev"};
const char* const kReverbNames[j8::RV_COUNT] = {"Rev Send", "Rev Mode", "Rev Type", "Rev PreDelay", "Rev Time", "Rev Level",
    "Shm Low Damp", "Shm Hi Damp", "Shm Time", "Shm Density", "Shm PreDelay", "Shm Mod Rate", "Shm Mod Dpth", "Shm Pitch 1", "Shm Pitch 2",
    "Shm Feedbk 1", "Shm Feedbk 2", "Shm Level 1", "Shm Level 2", "Shm Level"};

// What an MPC parameter drives.
struct Slot
{
    enum Kind : uint8_t { Volume, Patch, Global, MfxOn, MfxType, MfxSlot, DlyOn, Dly, RevOn, Rev, ChanU, ChanL, Stereo, LowerPreset };
    Kind kind;
    uint8_t part;       // Patch: 0 upper, 1 lower
    uint8_t id;         // Patch: P_*, Global: G_*, MfxSlot / Dly / Rev: position
};

constexpr int kNumPresets = 64;

// "DEEP SUB" -> "Deep Sub": the bank's names are in capitals for the FM-1's small screen.
std::string titleCase(const char* s)
{
    std::string out;
    bool start = true;
    for (; *s; ++s)
    {
        char c = *s;
        if (c >= 'A' && c <= 'Z' && !start) c = (char)(c - 'A' + 'a');
        start = !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
        out.push_back(c);
    }
    return out;
}

} // namespace

// =============================================================================================
class Ju08 final : public mpc::Plugin
{
public:
    Ju08() : mpc::Plugin(makeInfo())
    {
        j8::initPatch(init_, "INIT");
        j8::defaultGlobals(globalDefaults_);
        globalDefaults_[j8::G_KEYMODE] = j8::KM_WHOLE;      // one patch on all 8 voices: the usual way on an MPC track
        globalDefaults_[j8::G_CH_U] = 0;                    // OMNI
        globalDefaults_[j8::G_CH_L] = 2;
        j8::applyFactory(bank_, kNumPresets);
        j8::buildPerformances(perf_, globalDefaults_);

        // ---- 0: master volume (the MPC nudges parameter 0 on every load: harmless here)
        add({Slot::Volume, 0, 0}, mpc::intParam("volume", "Volume", 0, 255, globalDefaults_[j8::G_VOLUME]));

        // ---- 1..39: upper patch
        addPatch(0);

        // ---- CHORUS & MFX
        idxMfxOn_ = add({Slot::MfxOn, 0, 0}, mpc::boolParam("mfx_on", "MFX On", false));
        {
            std::vector<std::string> types;
            for (int t = 0; t < j8::MFX_COUNT; ++t) types.push_back(j8::mfxName(t));
            idxMfxType_ = add({Slot::MfxType, 0, 0}, mpc::choiceParam("mfx_type", "MFX Type", types, 0));
        }
        idxMfxSlot_ = numParams();
        for (int i = 0; i < j8::kMfxMaxParams; ++i)
        {
            char id[16], name[16];
            std::snprintf(id, sizeof(id), "mfx_p%d", i + 1);
            std::snprintf(name, sizeof(name), "MFX %d", i + 1);
            float def = 0.0f;
            if (i < j8::mfxParamCount(0)) def = slotNorm(j8::mfxParam(0, i), j8::mfxParam(0, i).def);
            add({Slot::MfxSlot, 0, (uint8_t)i}, mpc::floatParam(id, name, 0.0f, 1.0f, def, "", 3));
        }

        // ---- delay and reverb
        idxDlyOn_ = add({Slot::DlyOn, 0, 0}, mpc::boolParam("dly_on", "Delay On", false));
        idxDly_ = numParams();
        for (int i = 0; i < j8::DL_COUNT; ++i) addFx({Slot::Dly, 0, (uint8_t)i}, kDelayIds[i], kDelayNames[i], j8::delayParam(i));
        idxRevOn_ = add({Slot::RevOn, 0, 0}, mpc::boolParam("rev_on", "Reverb On", false));
        idxRev_ = numParams();
        for (int i = 0; i < j8::RV_COUNT; ++i) addFx({Slot::Rev, 0, (uint8_t)i}, kReverbIds[i], kReverbNames[i], j8::reverbParam(i));

        // ---- performance controls
        idxGlobal_ = numParams();
        for (int i = 0; i < kNumGlobals; ++i)
        {
            const GlobalDef& g = kGlobals[i];
            const int def = globalDefaults_[g.g];
            if (g.choices.empty()) add({Slot::Global, 0, g.g}, mpc::intParam(g.id, g.name, 0, g.max, def));
            else if (g.isSwitch)   add({Slot::Global, 0, g.g}, mpc::boolParam(g.id, g.name, def != 0));
            else                   add({Slot::Global, 0, g.g}, mpc::choiceParam(g.id, g.name, g.choices, def));
        }
        {
            std::vector<std::string> ch = {"Omni"};
            for (int i = 1; i <= 16; ++i) ch.push_back(std::to_string(i));
            add({Slot::ChanU, 0, 0}, mpc::choiceParam("ch_u", "Upper MIDI Ch", ch, globalDefaults_[j8::G_CH_U]));
            add({Slot::ChanL, 0, 0}, mpc::choiceParam("ch_l", "Lower MIDI Ch", ch, globalDefaults_[j8::G_CH_L]));
        }
        idxStereo_ = add({Slot::Stereo, 0, 0}, mpc::boolParam("stereo", "Stereo Layers", false));

        // ---- lower patch, and the selector that loads a preset into it
        addPatch(1);
        {
            std::vector<std::string> names = {"- - -"};
            for (int i = 0; i < kNumPresets; ++i) names.push_back(presetName(i));
            idxLowerPreset_ = add({Slot::LowerPreset, 0, 0}, mpc::choiceParam("lower_preset", "Lower Preset", names, 0));
        }

        // ---- the 64 patches as built-in programs. Their values are applied by selectProgram() below, which
        //      writes the Upper patch and the effects and leaves everything else alone.
        for (int i = 0; i < kNumPresets; ++i) addProgram(presetName(i).c_str(), {});
        // ---- then the 100 performances (P01-P100): both patches, the key mode and the performance settings
        for (int i = 0; i < j8::kNumPerformances; ++i) addProgram(performanceName(i).c_str(), {});

        built_ = true;
        lastMfxType_.store(0, std::memory_order_relaxed);
        selectProgram(0);
    }

    ~Ju08() override { freeFxMemory(); }

    // ---------------------------------------------------------------- framework callbacks
    void prepare(double sampleRate, int /*maxBlockSize*/) override
    {
        const float fs = (float)sampleRate;
        // Effect memory, full size as on a board with PSRAM: MFX 2.7 s, delay 1.35 s a channel, reverb 36000 samples.
        const int32_t mfx = (int32_t)(2.7f * fs), dly = (int32_t)(1.35f * fs), rev = 36000;
        if (mfx != mfxLen_ || dly != dlyLen_)
        {
            freeFxMemory();
            mem_[0] = (int16_t*)std::calloc((size_t)mfx, sizeof(int16_t));
            mem_[1] = (int16_t*)std::calloc((size_t)mfx, sizeof(int16_t));
            mem_[2] = (int16_t*)std::calloc((size_t)dly, sizeof(int16_t));
            mem_[3] = (int16_t*)std::calloc((size_t)dly, sizeof(int16_t));
            mem_[4] = (int16_t*)std::calloc((size_t)rev, sizeof(int16_t));
            mfxLen_ = mfx; dlyLen_ = dly;
        }
        ok_ = mem_[0] && mem_[1] && mem_[2] && mem_[3] && mem_[4];
        if (!ok_) return;
        for (int i = 0; i < 2; ++i) { std::memset(mem_[i], 0, (size_t)mfx * 2); std::memset(mem_[2 + i], 0, (size_t)dly * 2); }
        std::memset(mem_[4], 0, (size_t)rev * 2);
        engine_.~Engine();                  // a fresh engine (it holds atomics, so it cannot be assigned)
        new (&engine_) j8::Engine();
        engine_.init(fs);
        engine_.fx.init(fs, mem_[0], mem_[1], mfx, mem_[2], mem_[3], dly, mem_[4], rev);
        avail_ = 0;
        syncParams();
    }

    void reset() override
    {
        // Called on resume and after a project or preset load: silence the keys, keep the sound.
        engine_.pushEvent(j8::Event{j8::Event::AllOff, 0, 0, j8::kAnyChannel});
        avail_ = 0;
    }

    void process(mpc::ProcessContext& ctx) override
    {
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : nullptr;
        const int n = ctx.numFrames;
        if (!ok_ || outL == nullptr) return;

        syncParams();
        engine_.hostBpm = ctx.transport.tempoValid ? (float)ctx.transport.bpm : 120.0f;   // tempo-synced effects follow the MPC

        // The engine works in blocks of 32 samples. Events are handed over at the start of the block
        // they fall in (0.7 ms resolution); what is left of a block is kept for the next call.
        int ev = 0, pos = 0;
        while (pos < n)
        {
            if (avail_ == 0)
            {
                while (ev < ctx.numMidiEvents && ctx.midi[ev].sampleOffset <= pos) handleMidi(ctx.midi[ev++]);
                renderBlock();
            }
            int take = avail_ < n - pos ? avail_ : n - pos;
            const int rd = j8::kBlock - avail_;
            std::memcpy(outL + pos, bufL_ + rd, sizeof(float) * (size_t)take);
            if (outR) std::memcpy(outR + pos, bufR_ + rd, sizeof(float) * (size_t)take);
            avail_ -= take;
            pos += take;
        }
        while (ev < ctx.numMidiEvents) handleMidi(ctx.midi[ev++]);
    }

    // A parameter moved. Called from any thread: only atomics and byte writes in here.
    void parameterChanged(int index, float plain) override
    {
        if (!built_) return;
        if (index == idxMfxType_)
        {
            const int type = mpc::clamp((int)std::lround(plain), 0, (int)j8::MFX_COUNT - 1);
            const int was = lastMfxType_.exchange(type, std::memory_order_relaxed);
            if (was != type && bulk_.load(std::memory_order_relaxed) == 0)
            {
                // A new effect type starts from its own defaults, as on the hardware.
                bulk_.fetch_add(1, std::memory_order_relaxed);
                for (int i = 0; i < j8::kMfxMaxParams; ++i)
                    setNormalised(idxMfxSlot_ + i, i < j8::mfxParamCount(type) ? slotNorm(j8::mfxParam(type, i), j8::mfxParam(type, i).def) : 0.0f);
                bulk_.fetch_sub(1, std::memory_order_relaxed);
                requestDisplayUpdate();     // the slots have new names and values
            }
        }
        else if (index == idxLowerPreset_)
        {
            const int sel = (int)std::lround(plain);
            if (sel >= 1 && sel <= kNumPresets && bulk_.load(std::memory_order_relaxed) == 0)
            {
                loadPatch(1, bank_[sel - 1], false);
                requestDisplayUpdate();
            }
        }
    }

    bool formatValue(int index, float plain, std::string& out) override
    {
        if (index < 0 || index >= (int)slots_.size()) return false;
        const Slot& s = slots_[(size_t)index];
        char buf[32];
        buf[0] = 0;
        const int v = (int)std::lround(plain);
        switch (s.kind)
        {
            case Slot::Volume:
                j8::formatParam((int)P_COUNT + (int)j8::G_VOLUME, (uint8_t)v, buf, sizeof(buf));
                break;
            case Slot::Patch:
                if (!kPanel[s.id].choices.empty()) return false;
                j8::formatParam(s.id, (uint8_t)v, buf, sizeof(buf));
                break;
            case Slot::Global:
                if (param(index).kind != mpc::Param::Kind::Int) return false;
                j8::formatParam((int)P_COUNT + s.id, (uint8_t)v, buf, sizeof(buf));
                break;
            case Slot::MfxSlot:
            {
                const int type = lastMfxType_.load(std::memory_order_relaxed);
                if (s.id < j8::mfxParamCount(type))
                {
                    const j8::FxParamDef& d = j8::mfxParam(type, s.id);
                    j8::formatFx(d, slotValue(d, getNormalised(index)), buf, sizeof(buf));
                }
                break;
            }
            case Slot::Dly:
            case Slot::Rev:
            {
                if (param(index).kind != mpc::Param::Kind::Int) return false;
                const j8::FxParamDef& d = s.kind == Slot::Dly ? j8::delayParam(s.id) : j8::reverbParam(s.id);
                j8::formatFx(d, v, buf, sizeof(buf));
                break;
            }
            default:
                return false;
        }
        out = buf;
        return true;
    }

    // ---------------------------------------------------------------- used by the dispatcher below
    // A built-in program. 11-88: the patch goes to the Upper part, with its effects; the Lower part and the
    // performance settings stay as they are. P01-P100: a performance sets both parts, the effects, the key mode
    // and the performance settings (part levels, split point, arpeggio, portamento, bender, wheel).
    void selectProgram(int i)
    {
        if (i < 0 || i >= kNumPresets + j8::kNumPerformances) return;
        currentProgram_ = i;
        if (i < kNumPresets)
        {
            loadPatch(0, bank_[i], true);
            return;
        }
        const j8::Performance& pf = perf_[i - kNumPresets];
        bulkBegin();
        loadPatch(0, pf.upper, true);
        loadPatch(1, pf.lower, false);
        for (int k = 0; k < j8::kNumPerformanceGlobals; ++k)
        {
            const uint8_t g = j8::kPerformanceGlobals[k];
            for (int n = 0; n < kNumGlobals; ++n)
                if (kGlobals[n].g == g) set(idxGlobal_ + n, (float)pf.global[g]);
        }
        set(idxStereo_, pf.stereoLayers ? 1.0f : 0.0f);
        set(idxLowerPreset_, 0.0f);
        bulkEnd();
    }
    void bulkBegin() { bulk_.fetch_add(1, std::memory_order_relaxed); }
    void bulkEnd()   { bulk_.fetch_sub(1, std::memory_order_relaxed); }

    // The MFX slots are named after the selected effect's parameters; unused slots have an empty name,
    // which hides them on the MFX pages.
    bool dynamicName(int index, char* out) const
    {
        if (out == nullptr || index < idxMfxSlot_ || index >= idxMfxSlot_ + j8::kMfxMaxParams) return false;
        const int type = lastMfxType_.load(std::memory_order_relaxed), i = index - idxMfxSlot_;
        mpc::copyString(out, 32, i < j8::mfxParamCount(type) ? j8::mfxParam(type, i).name : "");
        return true;
    }

private:
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo info;
        info.name = "Ju-08";
        info.vendor = "Stephane Schmitz";
        info.uniqueId = MPC_FOURCC('S', 'k', 'W', 'p');     // must equal "uid" in plugin.json
        info.version = 1100;
        info.isInstrument = true;
        info.numInputs = 0;
        info.numOutputs = 2;
        return info;
    }

    static std::string performanceName(int i)
    {
        char num[8];
        std::snprintf(num, sizeof(num), "P%02d ", i + 1);
        return std::string(num) + titleCase(perf_[i].name);
    }

    static std::string presetName(int i)
    {
        // Numbered 11-88 by bank and patch, as on the panel.
        return std::to_string(i / 8 + 1) + std::to_string(i % 8 + 1) + " " + titleCase(bank_[i].name);
    }

    int add(const Slot& s, const mpc::Param& p)
    {
        slots_.push_back(s);
        return addParam(p);
    }

    void addPatch(int part)
    {
        idxPatch_[part] = numParams();
        for (int i = 0; i < P_COUNT; ++i)
        {
            const PanelDef& d = kPanel[i];
            const std::string id = std::string(part == 0 ? "u_" : "l_") + d.id;
            const std::string name = std::string(part == 0 ? "" : "L ") + d.name;
            const int def = init_.v[i];
            const Slot s{Slot::Patch, (uint8_t)part, (uint8_t)i};
            if (d.choices.empty()) add(s, mpc::intParam(id.c_str(), name.c_str(), 0, 255, def));
            else if (d.isSwitch)   add(s, mpc::boolParam(id.c_str(), name.c_str(), def != 0));
            else                   add(s, mpc::choiceParam(id.c_str(), name.c_str(), d.choices, def));
        }
    }

    void addFx(const Slot& s, const char* id, const char* name, const j8::FxParamDef& d)
    {
        if (d.kind == j8::FxKind::OnOff) add(s, mpc::boolParam(id, name, d.def != 0));
        else if (d.kind == j8::FxKind::Labels)
        {
            std::vector<std::string> c;
            for (int i = d.lo; i <= d.hi; ++i) c.push_back(d.labels[i]);
            add(s, mpc::choiceParam(id, name, c, d.def));
        }
        else add(s, mpc::intParam(id, name, d.lo, d.hi, d.def));
    }

    // An MFX slot is 0..1 for the MPC and lo..hi of the selected effect's parameter for the engine.
    static float slotNorm(const j8::FxParamDef& d, int value)
    {
        return d.hi == d.lo ? 0.0f : mpc::clamp((float)(value - d.lo) / (float)(d.hi - d.lo), 0.0f, 1.0f);
    }
    static int slotValue(const j8::FxParamDef& d, float norm)
    {
        return d.lo + (int)std::lround(mpc::clamp(norm, 0.0f, 1.0f) * (float)(d.hi - d.lo));
    }

    // Write a patch into the MPC parameters of a part (and, for the Upper part, the effects).
    void loadPatch(int part, const j8::Patch& p, bool withFx)
    {
        bulkBegin();
        for (int i = 0; i < P_COUNT; ++i) set(idxPatch_[part] + i, (float)p.v[i]);
        if (withFx)
        {
            const j8::FxPatch& f = p.fx;
            const int type = f.mfxType < j8::MFX_COUNT ? f.mfxType : 0;
            set(idxMfxType_, (float)type);
            set(idxMfxOn_, f.mfxOn ? 1.0f : 0.0f);
            for (int i = 0; i < j8::kMfxMaxParams; ++i)
                setNormalised(idxMfxSlot_ + i, i < j8::mfxParamCount(type) ? slotNorm(j8::mfxParam(type, i), f.m[i]) : 0.0f);
            set(idxDlyOn_, f.dlyOn ? 1.0f : 0.0f);
            for (int i = 0; i < j8::DL_COUNT; ++i) set(idxDly_ + i, (float)f.d[i]);
            set(idxRevOn_, f.revOn ? 1.0f : 0.0f);
            for (int i = 0; i < j8::RV_COUNT; ++i) set(idxRev_ + i, (float)f.r[i]);
        }
        bulkEnd();
    }

    // MPC parameters -> engine. Audio thread, once per process call.
    void syncParams()
    {
        const int type = mpc::clamp(getInt(idxMfxType_), 0, (int)j8::MFX_COUNT - 1);
        j8::FxPatch& fx = engine_.patch[0].fx;
        const int n = numParams();
        for (int i = 0; i < n; ++i)
        {
            const Slot& s = slots_[(size_t)i];
            switch (s.kind)
            {
                case Slot::Volume:  engine_.global[j8::G_VOLUME] = (uint8_t)getInt(i); break;
                case Slot::Patch:   engine_.patch[s.part].v[s.id] = (uint8_t)getInt(i); break;
                case Slot::Global:  engine_.global[s.id] = (uint8_t)getInt(i); break;
                case Slot::MfxOn:   fx.mfxOn = getBool(i); break;
                case Slot::MfxType: fx.mfxType = (uint8_t)type; break;
                case Slot::MfxSlot: fx.m[s.id] = s.id < j8::mfxParamCount(type) ? (int16_t)slotValue(j8::mfxParam(type, s.id), getNormalised(i)) : (int16_t)0; break;
                case Slot::DlyOn:   fx.dlyOn = getBool(i); break;
                case Slot::Dly:     fx.d[s.id] = (int16_t)getInt(i); break;
                case Slot::RevOn:   fx.revOn = getBool(i); break;
                case Slot::Rev:     fx.r[s.id] = (int16_t)getInt(i); break;
                case Slot::ChanU:   engine_.global[j8::G_CH_U] = (uint8_t)getInt(i); break;
                case Slot::ChanL:   engine_.global[j8::G_CH_L] = (uint8_t)getInt(i); break;
                case Slot::Stereo:  engine_.stereoLayers = getBool(i); break;
                case Slot::LowerPreset: break;
            }
        }
    }

    void renderBlock()
    {
        float up[j8::kBlock], lo[j8::kBlock];
        std::memset(up, 0, sizeof(up));
        std::memset(lo, 0, sizeof(lo));
        engine_.prepareBlock();
        engine_.render(0, j8::kNumVoices, up, lo);
        engine_.fxWorker();                 // the reverb, which had a core of its own on the ESP32
        engine_.finish(up, lo, bufL_, bufR_);
        avail_ = j8::kBlock;
    }

    void handleMidi(const mpc::MidiEvent& e)
    {
        const uint8_t ch = e.channel();
        if (e.isNoteOn())            engine_.pushEvent(j8::Event{j8::Event::NoteOn, (uint8_t)e.note(), 0, ch});
        else if (e.isNoteOff())      engine_.pushEvent(j8::Event{j8::Event::NoteOff, (uint8_t)e.note(), 0, ch});
        else if (e.isPitchBend())    engine_.pushEvent(j8::Event{j8::Event::Bend, 0, (int16_t)e.pitchBend(), ch});
        else if (e.isController())
        {
            const int cc = e.controller(), val = e.controllerValue();
            if (cc == 1)                    engine_.pushEvent(j8::Event{j8::Event::Mod, (uint8_t)val, 0, ch});
            else if (cc == 64)              engine_.pushEvent(j8::Event{j8::Event::Sustain, (uint8_t)val, 0, ch});
            else if (cc == 120 || cc == 123) engine_.pushEvent(j8::Event{j8::Event::AllOff, 0, 0, ch});
            else controlChange(ch, cc, val);
        }
        else if (e.isProgramChange() && e.data[1] < kNumPresets)
        {
            // Patches 11-88 on the part(s) that listen to this channel.
            const int mask = engine_.partMask(ch);
            if (mask & 1) selectProgram(e.data[1]);
            if ((mask & 2) && engine_.global[j8::G_KEYMODE] == j8::KM_2PART) loadPatch(1, bank_[e.data[1]], false);
        }
        else if (e.isProgramChange())
        {
            // 64-127: performances P01-P64, on the Upper part's channel
            if (engine_.partMask(ch) & 1) selectProgram(e.data[1]);
        }
    }

    // The CC map of the original (README of jupiter8_esp32): sound controls follow the part's channel.
    // Master volume (CC 7) and the effect tempo (CC 109) are left to the MPC.
    void controlChange(uint8_t ch, int cc, int val)
    {
        const int mask = engine_.partMask(ch);
        if (!mask) return;
        if (cc == 91 || cc == 94) { if (mask & 1) set((cc == 91 ? idxRev_ + j8::RV_SEND : idxDly_ + j8::DL_SEND), (float)val); return; }
        if (cc == 93) { if (mask & 1) set(idxMfxOn_, val >= 64 ? 1.0f : 0.0f); return; }
        if (cc == 7 || cc == 109) return;
        const int id = j8::ccToParam((uint8_t)cc);
        if (id < 0) return;
        const int steps = j8::paramInfo(id).steps;
        if (id < P_COUNT)
        {
            const float v = steps ? (float)mpc::clamp(val * steps / 128, 0, steps - 1) : (float)(val * 255 / 127);
            for (int l = 0; l < 2; ++l) if (mask & (1 << l)) set(idxPatch_[l] + id, v);
            return;
        }
        const int g = id - P_COUNT;
        for (int i = 0; i < kNumGlobals; ++i)
        {
            if (kGlobals[i].g != g) continue;
            const int max = kGlobals[i].choices.empty() ? kGlobals[i].max : (int)kGlobals[i].choices.size() - 1;
            set(idxGlobal_ + i, steps ? (float)mpc::clamp(val * steps / 128, 0, steps - 1) : (float)(val * max / 127));
            return;
        }
    }

    void freeFxMemory()
    {
        for (int16_t*& m : mem_) { std::free(m); m = nullptr; }
        mfxLen_ = dlyLen_ = 0;
        ok_ = false;
    }

    static j8::Patch bank_[kNumPresets];
    static j8::Performance perf_[j8::kNumPerformances];

    j8::Engine engine_;
    j8::Patch init_;
    uint8_t globalDefaults_[j8::G_COUNT];
    std::vector<Slot> slots_;
    int idxPatch_[2] = {0, 0};
    int idxMfxOn_ = 0, idxMfxType_ = 0, idxMfxSlot_ = 0, idxDlyOn_ = 0, idxDly_ = 0, idxRevOn_ = 0, idxRev_ = 0;
    int idxGlobal_ = 0, idxLowerPreset_ = 0, idxStereo_ = 0;
    bool built_ = false;
    std::atomic<int> lastMfxType_{0};
    std::atomic<int> bulk_{0};              // > 0 while a patch, a program or a project state is being written

    int16_t* mem_[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
    int32_t mfxLen_ = 0, dlyLen_ = 0;
    bool ok_ = false;

    float bufL_[j8::kBlock] = {0}, bufR_[j8::kBlock] = {0};
    int avail_ = 0;                         // samples of the last engine block not yet given to the MPC
};

j8::Patch Ju08::bank_[kNumPresets];
j8::Performance Ju08::perf_[j8::kNumPerformances];

// ---------------------------------------------------------------------------------------------
// Entry point. The framework's dispatcher does everything except three things this plugin needs:
// a program writes only the Upper patch and the effects, a project state must be restored without
// the "new effect type -> defaults" and "lower preset -> load" reactions, and the MFX slots have names
// that depend on the selected effect.
// ---------------------------------------------------------------------------------------------
static VstIntPtr ju08Dispatcher(AEffect* e, VstInt32 opcode, VstInt32 index, VstIntPtr value, void* ptr, float opt)
{
    Ju08* p = static_cast<Ju08*>(static_cast<mpc::Plugin*>(e->object));
    if (p != nullptr)
    {
        switch (opcode)
        {
            case effSetProgram:
                p->selectProgram((int)value);
                return 0;
            case effSetChunk:
            {
                p->bulkBegin();
                const VstIntPtr r = mpc::detail::dispatcher(e, opcode, index, value, ptr, opt);
                p->bulkEnd();
                return r;
            }
            case effGetParamName:
                if (p->dynamicName((int)index, static_cast<char*>(ptr))) return 0;
                break;
            default:
                break;
        }
    }
    return mpc::detail::dispatcher(e, opcode, index, value, ptr, opt);
}

extern "C" MPC_EXPORT AEffect* VSTPluginMain(audioMasterCallback host)
{
    if (host != nullptr && host(nullptr, audioMasterVersion, 0, 0, nullptr, 0.0f) == 0) return nullptr;
    AEffect* e = mpc::detail::createEffect(new Ju08(), host);
    if (e != nullptr) e->dispatcher = &ju08Dispatcher;
    return e;
}
