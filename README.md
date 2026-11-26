# Ju-08

A Jupiter-8 style polysynth for Akai MPC standalone hardware (MPC OS 3.10.1 or later), built with the
MPC Plugin SDK. It is the synth of Mini-Ju08 (the firmware for the M-VAVE FM-1) without the sequencer,
with a touch UI drawn for the MPC's screen, the same 64 patches, and 100 performances that play both parts.

Instrument, 0 in / 2 out, MIDI in. Manufacturer "Stephane Schmitz", name "Ju-08", uid `SkWp`.

## What it is

- Two parts of four voices (Dual, Split, 2 Part), or eight voices on one patch (Whole, the default).
- Per voice: two VCOs with sync and cross mod, mixer, HPF, 12/24 dB VCF, two ADSRs, LFO; Solo / Unison /
  Poly 1 / Poly 2; polyphonic portamento, bender and LFO-mod wheel, arpeggiator, hold.
- CHORUS & MFX (17 types), delay (2-tap or ping-pong), reverb (five types and shimmer). Tempo-synced
  effects follow the MPC's tempo.
- 164 programs (the MPC's preset list):
  - **11-88:** the 64 patches. One of them writes the Upper patch and the effects; the Lower part and the
    performance settings stay as they are.
  - **P01-P100:** the performances. One of them writes both patches, the effects, the key mode and the
    performance settings ([Performances](#performances)).

### Where the sound comes from

The engine in `src/` (`engine.cpp`, `fx.cpp`, `params.cpp`, `presets.cpp`, `fx_params.cpp`) is the float
engine of `jupiter8_esp32`, the reference that Mini-Ju08's fixed-point engine was ported from and tested
against. The MPC has a floating-point unit, so the float original is the better fit: it keeps the full
effect memory (MFX 2.7 s, delay 1.35 s) and the band-limited triangle that the FM-1 port had to cut. The
parameter model, patches and effects are the same. Two lines differ from the original: `Engine::hostBpm`
(`engine.h`, `engine.cpp`), which lets the MPC's tempo drive the synced effects.

`src/Ju08.cpp` is the bridge: MPC parameters to the engine's patch bytes, MIDI to the engine's events,
programs, state.

## Screen pages

The pages look like the front panel of the 1981 polysynth the sound comes from: a black panel with white
lettering, vertical faders with a 0-10 scale, coloured switches with a red lamp above each, and the
orange stripe along the bottom. No maker's name or logo is used.

A page has four sections. A section is one Q-Link bank of four knobs: from the top knob down, they hold
the section's faders from left to right, then its rows of switches from top to bottom (the last row is
the one on the orange stripe). On an MPC with four knobs the highlighted section is the one they hold.
Touch a control to bring its section under the knobs; drag a fader up and down; double-tap (or Enter)
for the large value knob. The MFX pages are a grid of four by four, with the knob next to each control.

| Page | Sections |
|---|---|
| VCO | LFO, VCO modulator, VCO-1, VCO-2 |
| VCF | Mixer / HPF (with key assign and part level), VCF, VCF modulation, glide / tune |
| ENV | ENV-1, ENV-2, envelope modes, VCA |
| PLAY | Key mode, arpeggio, output, lower part / MIDI |
| MFX, MFX 2 | Effect on, type, and the parameters of the selected type (they change with the type) |
| DELAY | Delay, time, taps, tone / output |
| REVERB, SHIMMER | Reverb, standard section; the shimmer mode's controls |
| CTRL | Bender to VCO and VCF, wheel LFO to VCO and VCF |
| L VCO, L VCF, L ENV | The Lower part's patch (marked LOWER under the section names), used in Dual, Split and 2 Part |

Pictures of all pages at their default values are in `preview/`.

**Two patches at once:** PLAY > KEY MODE to DUAL or SPLIT, then PLAY > LOAD PRESET > LOWER (or L VCF)
picks the Lower part's patch; the MPC's preset list picks the Upper one. The effects are the Upper
patch's. 2 PART gives each part its own MIDI channel (PLAY page).

## Performances

The programs after 88, P01-P100, use both parts at once. Each performance sets:
- **the patches:** the Upper patch with the effects (in Dual and Split the effects are the Upper patch's), and the
  Lower patch;
- **the key mode:** Dual (both patches on every key, four voices each) or Split (the Lower patch below the split
  point, the Upper one from it up);
- **the performance settings:** the part levels, the split point, the arpeggio, the portamento, the bender and the
  wheel. The settings a performance does not use go back to their defaults, so an arpeggio or a glide never stays
  on from the previous one. The volume, tune, analog feel, MIDI channels and tempo stay as they are;
- **Stereo Layers:** on (the Lower part left and the Upper part right, heard when no effect is on).

The level of each part was set by measuring it (`tests/perf_levels.cpp`). Every performance sounds about as loud
as the patches, within 4 dB of their median, and in each one both parts can be heard.

They use the panel's modulation throughout:
- **filter LFO:** slow sweeps, random sample-and-hold, square-wave chopping;
- **filter envelope:** normal and inverted (ENV-1 polarity), resonant envelopes;
- **key follow:** VCF key follow, the two parts set differently;
- **pulse width:** PWM from the LFO and from ENV-1;
- **sync and cross mod:** sync swept by an envelope, cross mod for bells and metal;
- **LFO on pitch:** delayed vibrato, a square-wave trill, sirens;
- **VCA:** VCA tremolo;
- **the wheel:** WHEEL FILTER SPLIT and WHEEL LEAD open the filter with the mod wheel, and WAH LEAD too.

| # | Performance | Key mode | What it is |
|---|---|---|---|
| P01 | Brass Strings | Dual | brass section over a vibrato string ensemble |
| P02 | Pad And Bells | Dual | cross-mod bells over a warm pad |
| P03 | Octave PWM | Dual | PWM pads at 4' and 16', LFOs at different rates |
| P04 | Sync Stack | Dual | envelope-swept sync over a slow saw pad |
| P05 | Filter Waves | Dual | two resonant pads, filters swept by LFOs at different rates (sine and saw) |
| P06 | PWM Ensemble | Dual | two pulse-width pads drifting against each other |
| P07 | Glass Choir | Dual | glassy square/sine pad over a filtered noise breath |
| P08 | String Machine | Dual | strings at 8' and 4', delayed vibrato |
| P09 | Brass Swell Duo | Dual | slow swelling brass over a fast one an octave down |
| P10 | Saw Wall | Dual | wide detuned saws at 8' and 16' |
| P11 | Inverted Sweep | Dual | one filter envelope inverted, one normal: the sweeps cross |
| P12 | Key Track Pad | Dual | a pulse pad that brightens up the keyboard, a dark triangle pad that doesn't |
| P13 | Tremolo Keys | Dual | electric piano with VCA tremolo over a soft pad |
| P14 | Sample And Hold | Dual | random-stepped resonant filter over a soft pad, ping-pong delay |
| P15 | Trill Pad | Dual | square-LFO pitch trill over a string pad |
| P16 | Organ And Pad | Dual | drawbar-style organ layered with strings |
| P17 | Bell Pad | Dual | tube bells over a very slow pad |
| P18 | Clav And Brass | Dual | resonant clav with brass stabs |
| P19 | Hollow Env PWM | Dual | pulse width swept by ENV-1, over a triangle/sine pad |
| P20 | Resonant Dream | Dual | slow resonant 24 dB sweep over a triangle pad |
| P21 | Space Choir | Dual | LFO-swept pad and resonant noise wind, shimmer |
| P22 | Vintage Poly | Dual | key-tracked saw poly with a square layer |
| P23 | Twin Vibrato Str | Dual | two string layers, vibratos at different rates and delays |
| P24 | Octave Brass | Dual | brass at 4' and 8' |
| P25 | Soft Horns | Dual | mellow horns with strings |
| P26 | Crystal Pad | Dual | envelope-swept sync sparkle over a PWM pad |
| P27 | Pluck Over Sweep | Dual | a pluck over a slowly LFO-swept resonant pad |
| P28 | Dark Dual | Dual | dark LFO-filtered pad over a 16' sine pad |
| P29 | Phase Pad | Dual | saw and PWM pads through the phaser |
| P30 | Dreamland | Dual | slow layers with chorus and shimmer |
| P31 | Bass And Piano | Split C3 | bass below C3, electric piano above |
| P32 | Bass And Brass | Split C3 | resonant saw bass, brass section |
| P33 | Bass And Strings | Split C3 | pluck bass, string ensemble |
| P34 | Acid And Pad | Split C3 | resonant 24 dB acid bass (slow filter LFO), warm pad |
| P35 | Sync Bass Lead | Split C3 | sync bass, sync lead with glide |
| P36 | Sub And Glide | Split C3 | sub bass, gliding saw lead |
| P37 | Funk Split | Split C3 | square bass, clav |
| P38 | Organ Split | Split C3 | organ pedal bass, organ |
| P39 | Wobble And Stab | Split C3 | LFO filter wobble bass, stab chords |
| P40 | Bass And Bells | Split C3 | round bass, cross-mod bells |
| P41 | Drone And Pad | Split C3 | slow resonant drone, pad |
| P42 | Bass And Vibes | Split C3 | soft bass, vibes with tremolo |
| P43 | Unison And Pad | Split C3 | unison bass, PWM pad |
| P44 | Cello And Flute | Split G3 | bowed cello below G3, breathy flute above |
| P45 | Bass And Reso Keys | Split C3 | bass, resonant pulse keys |
| P46 | Xmod And Glass | Split C3 | cross-mod bass, glass bells |
| P47 | Random Bass Lead | Split C3 | random-filter bass, gliding lead |
| P48 | Strings And Lead | Split C4 | strings below C4, square lead with glide above |
| P49 | Pad And PWM Lead | Split C4 | pad, PWM lead with glide |
| P50 | Bass And Choir | Split C3 | sine bass, glass choir |
| P51 | Techno Split | Split C3 | resonant bass, resonant square stabs |
| P52 | House Organ | Split C3 | sub bass, organ stabs |
| P53 | Pizz And Strings | Split C4 | pizzicato below, strings above |
| P54 | Sub And Sync Brass | Split C3 | sub bass, sync brass |
| P55 | Wheel Filter Split | Split C3 | the mod wheel opens both resonant filters |
| P56 | Arp Split | Split C4 | arpeggiated pluck below C4, pad above |
| P57 | Arp Cascade | Split C4 | up-and-down bell arpeggio over 3 octaves, strings |
| P58 | Arp Bassline | Split C3 | one-octave bass arpeggio, brass |
| P59 | Arp Random | Split C4 | random resonant pluck arpeggio over 4 octaves, dark pad |
| P60 | Arp Dual | Dual | the arpeggio plays both patches: a pluck and a 16' square |
| P61 | Arp Filter Sweep | Split C4 | arpeggio through a slowly LFO-swept resonant filter |
| P62 | Arp Echoes | Split C4 | descending square arpeggio with ping-pong echoes |
| P63 | Arp Sync | Split C4 | sync pluck arpeggio, strings |
| P64 | Arp And Glide Lead | Split C4 | bass arpeggio below, gliding lead above |
| P65 | Arp Twinkle | Split C4 | fast music-box arpeggio over 4 octaves, shimmer |
| P66 | Arp Reso | Split C4 | resonant 24 dB pluck arpeggio, swelling brass |
| P67 | Arp Keys | Split C4 | clav arpeggio, organ |
| P68 | Arp Xmod | Split C4 | random cross-mod arpeggio, dark pad |
| P69 | Arp Bell Dual | Dual | bells and 16' sines arpeggiated together |
| P70 | Arp Slow Triangle | Split C4 | slow up-and-down triangle arpeggio, strings |
| P71 | Unison Stack | Dual | two unison leads (eight voices on one note), glide |
| P72 | Sync Duo | Dual | sync lead with an octave-down saw, glide |
| P73 | Wah Lead | Dual | resonant envelope wah (the wheel opens it more) over a sine |
| P74 | Vibrato Duet | Dual | square and triangle leads, different vibratos |
| P75 | Scream Lead | Dual | sync and cross mod into overdrive |
| P76 | Wheel Lead | Dual | the wheel opens the filters (with a rise time) |
| P77 | Square Duo | Dual | two detuned square/pulse leads |
| P78 | Flute And Strings | Dual | breathy flute with soft strings |
| P79 | Soft Solo | Dual | triangle lead over a 16' sine |
| P80 | Brass Lead | Dual | brassy mono lead, a pulse layer an octave down |
| P81 | Rising Tide | Dual | a slow noise sweep over a slow pad |
| P82 | Space Station | Dual | random-filtered resonant noise over a low drone |
| P83 | Helicopter | Dual | square-LFO chopped noise over a filtered rumble |
| P84 | Alien Talk | Dual | random LFO on pitch and filter over a cross-mod drone |
| P85 | Twin Sirens | Dual | two pitch LFO sirens at different rates and shapes |
| P86 | Wind And Bells | Dual | cross-mod bells in LFO-filtered wind |
| P87 | Thunder | Dual | noise crack and a long resonant rumble |
| P88 | Computer Blips | Dual | random fast arpeggio of pitch-envelope blips |
| P89 | Chopped Pad | Dual | square LFO on filter and VCA: a rhythmic pad |
| P90 | Pulsar | Dual | fast and slow PWM through the phaser |
| P91 | Metal Rain | Dual | high cross-mod metal over resonant noise plucks |
| P92 | Deep Space | Dual | very slow LFO filters, shimmer |
| P93 | Sweeping Strings | Dual | LFO-swept strings with inverted-envelope strings |
| P94 | Breathing Pad | Dual | slow filter and VCA LFO on two pads |
| P95 | Robot Choir | Dual | square-LFO filter talk over a glassy choir |
| P96 | Drone Machine | Dual | VCO-2 at low frequency cross-modulating, a resonant drone, long echoes |
| P97 | FM Towers | Dual | heavy cross mod on both parts |
| P98 | Bowed Metal | Dual | slow cross-mod bells over a resonant 24 dB pad |
| P99 | Cosmic Arp | Dual | random 4-octave arpeggio of bells and PWM plucks, shimmer |
| P100 | Grand Finale | Dual | big brass and strings, ensemble chorus and hall |

The arpeggio plays the Lower zone in Split, and both patches in Dual. Hold notes to hear it. After a
performance, choosing a patch 11-88 changes only the Upper patch. To go back to one patch on all eight voices, set
PLAY > KEY MODE to WHOLE.

To change a performance:
1. Edit its values in `src/performances.cpp`.
2. Run `build/tests/perf_levels --table > src/performance_levels.cpp`, which sets its levels again.

## MIDI

Notes (no velocity, as on the original), pitch bend, mod wheel (CC 1), sustain pedal (CC 64), all notes
off, program change (0-63: patches 11-88; 64-127: performances P01-P64), and the CC map of the original for the sound controls (CC 74 cutoff, 71
resonance, 73/75/79/72 ENV-2, ...; see the README of jupiter8_esp32). CC 7 and the effect tempo are left
to the MPC. A CC moves the parameter, but the MPC's screen may not show it until the page is redrawn.

## Build

```
SDK=~/mpc/mpc-plugin-builder-sdk
python3 $SDK/tools/build.py . --target native     # needed once: tools/make_skin.py reads build/params.json
python3 tools/make_skin.py                         # draws skins/ and preview/ (needs Pillow)
python3 $SDK/tools/build.py .                      # native + arm32 + arm64, validator
tests/run.sh
```

The result is `build/Stephane Schmitz - VST - Ju-08/`. Copy that folder into a folder named
`VST2 Plugins` at the root of a drive (internal, SD card or USB). On the MPC: Preferences > Enhancements
(Beta) > Enable Enhancements and External Plugin Support; the plugin then appears under External
Plugins. After replacing the folder, restart the MPC so it reloads the skin.

`skins/` is generated: change `tools/make_skin.py` (layout tables at `patch_pages()` / `all_pages()`,
placing in `layout()`, drawing in `fader_strip()` / `draw_fader()` / `button()`), not the JSON.

## Rules for later versions

The uid, the parameter ids and the parameter order are permanent once projects use the plugin: append
new parameters after "Lower Preset" (index 163). The order is listed at the top of `src/Ju08.cpp`.

## Checked, and not checked

Checked on a PC: the build for both ARM families passes the SDK's validator; the SDK's host simulator
passes (MPC load sequence, block sizes 32-512, parameter sweep, state round trip); every program
rendered through the plugin matches the engine driven directly (largest difference 1.4e-3, from two
compilers' float rounding on a chaotic patch); `tests/bridge_test.cpp` covers programs, performances, the lower
preset, key modes, effect type changes, state and MIDI; `tests/perf_levels.cpp` plays the 100 performances (both
parts sound, loudness within 4 dB of the patches').

Not checked: anything on an MPC. In particular the skin has never been drawn by real firmware (it uses
only widgets the SDK documents: film-strip knobs, buttons, labels, images, the colour-bar slider, the
knob overlay), and the CPU load on the 32-bit models is an estimate: from the original's
instruction counts, 8 voices with effects should take roughly 5-10 % of one core.

Known limits:
- The MPC's list popup (`MenuOverlay`) comes up empty on real firmware, in the SDK's own examples too, so
  every control opens the large value knob instead; lists are stepped with it, the data wheel or the Q-Link.
- Empty MFX slots have an empty parameter name (that is what hides them on the MFX pages); the SDK's host
  simulator warns about these 16 names.
- When the effect type or the lower preset changes, the plugin asks the MPC to refresh the page. If a
  firmware does not, the values show again after leaving the page and coming back.
- Parameter changes are read once per audio block of the MPC, notes at 0.7 ms resolution.
- The arpeggiator's rate is in Hz as on the original, not tied to the MPC's tempo.

## Licences

MIT (`LICENSE`). The sound engine is the float engine of `jupiter8_esp32`, by the same author; nothing of the
Mini-Ju08 firmware or of Felucca / Mini-Tone (GPL) is included. The MPC Plugin SDK framework that is compiled in
(`mpc_plugin.h`, `mpc_vst2_abi.h`) is MIT. The panel lettering in the images is drawn with Inter Tight
(SIL OFL 1.1, from Mini-Ju08's `assets/fonts`); no font file is shipped. No Steinberg, Akai or Roland
material is included. The effect type names follow the parameter lists the original took from Roland's
manual; change them in `src/fx_params.cpp` before publishing the plugin.
