# Ju-08

A Jupiter-8 style polysynth for Akai MPC standalone hardware (MPC OS 3.10.1 or later), built with the
MPC Plugin SDK. It is the synth of Mini-Ju08 (the firmware for the M-VAVE FM-1) without the sequencer,
with a touch UI drawn for the MPC's screen and the same 64 patches.

Instrument, 0 in / 2 out, MIDI in. Manufacturer "Stephane Schmitz", name "Ju-08", uid `SkWp`.

## What it is

- Two parts of four voices (Dual, Split, 2 Part), or eight voices on one patch (Whole, the default).
- Per voice: two VCOs with sync and cross mod, mixer, HPF, 12/24 dB VCF, two ADSRs, LFO; Solo / Unison /
  Poly 1 / Poly 2; polyphonic portamento, bender and LFO-mod wheel, arpeggiator, hold.
- CHORUS & MFX (17 types), delay (2-tap or ping-pong), reverb (five types and shimmer). Tempo-synced
  effects follow the MPC's tempo.
- The 64 patches, numbered 11-88, are the plugin's programs (the MPC's preset list). A program writes the
  Upper patch and the effects; the Lower part and the performance settings stay.

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

## MIDI

Notes (no velocity, as on the original), pitch bend, mod wheel (CC 1), sustain pedal (CC 64), all notes
off, program change 0-63, and the CC map of the original for the sound controls (CC 74 cutoff, 71
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
compilers' float rounding on a chaotic patch); `tests/bridge_test.cpp` covers programs, the lower preset,
key modes, effect type changes, state and MIDI.

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
