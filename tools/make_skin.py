#!/usr/bin/env python3
"""make_skin.py - draws the Ju-08 touch UI for the MPC and writes it to skins/.

    python3 tools/make_skin.py            (after a native build: it reads build/params.json)

Writes skins/TUI.json, skins/Q-Links.json and every image they use, plus preview/*.png: a picture
of each page at the default values, to check the layout without an MPC.

The look is the front panel of a 1981 eight-voice analogue polysynth: a black panel with white
silk-screen, vertical faders with a 0-10 scale, coloured switches with a red lamp above each, the
orange stripe along the bottom that the switches sit on, aluminium trims. No maker's name or logo.

A page has four sections; a section is one Q-Link column (its four knobs, top to bottom, are its
controls in the order of all_pages(): the faders from left to right, then the switch rows from top
to bottom). The MFX pages keep a four-by-four grid, because the plugin names their controls.
Faders, switches and panels are drawn here; values and the MFX names are text drawn by the MPC.

Needs Pillow. Format reference: docs/ui-reference.md of the MPC Plugin SDK.
"""

import json
import math
import os
import random
import re
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "skins")
PREVIEW = os.path.join(ROOT, "preview")
SS = 3                      # supersampling of everything drawn here

# ------------------------------------------------------------------------------------------ layout
W, H = 1280, 628            # a tab's area
COL_X = [8, 326, 644, 962]
COL_W = 310
CELL_Y = [46, 191, 336, 481]
CELL_H = 141
PAD = 12                    # inside a cell
CTRL_W = COL_W - 2 * PAD    # 286
FRAMES = 41                 # of a fader's film strip (the strip must stay under 16384 px tall)

# panel pages
HEAD_Y, HEAD_H = 8, 30      # a section's title
TOP = 46                    # the faders start here
STRIPE_Y, STRIPE_H = 516, 98
TRIM_Y = STRIPE_Y + STRIPE_H            # the aluminium trim under the stripe
ROW_H = 98                  # a row of switches: its name, then the switches
BT_Y, BT_H = 28, 64         # a switch inside a row (15 px of lamp, then the key)
GLASS_Y, GLASS_H = 30, 60   # a display window inside a row
ZONE_LONG = STRIPE_Y - 4 - TOP          # a fader's height: down to the stripe ...
ZONE_SHORT = ZONE_LONG - ROW_H - 6      # ... or to a row of switches above it
FADER_TOP, FADER_BOTTOM = 28, 52        # above the fader's travel: its value; below: its name
CAP_W, CAP_H = 40, 30
STRIP_W = 52

# ------------------------------------------------------------------------------------------ colours
PANEL = (16, 16, 17)
ALU = (176, 174, 168)
STRIPE = (226, 96, 34)
STRIPE_INK = (20, 12, 6)
SILK = (236, 234, 226)
FOCUS = (255, 255, 255)
CAPS = {"cream": (228, 222, 204), "yellow": (232, 190, 56), "orange": (232, 104, 36), "grey": (148, 148, 150), "teal": (64, 164, 152)}
BLACK_CAPS = ["orange", "cream", "yellow", "teal", "grey"]      # switches on the black panel, in turn
STRIPE_CAPS = ["cream", "yellow", "grey", "teal"]               # switches on the orange stripe
VALUE_ARGB = "ffffe2b8"
DISPLAY_ARGB = "ffff5a48"


def argb(rgb, a=255):
    return "%02x%02x%02x%02x" % (a, rgb[0], rgb[1], rgb[2])


def mix(a, b, t):
    return tuple(int(round(a[i] + (b[i] - a[i]) * t)) for i in range(3))


# ------------------------------------------------------------------------------------------ fonts
def find_font():
    cands = [
        (os.path.expanduser("~/Mini-Ju08/Mini-Ju08/assets/fonts/InterTight[wght].ttf"), "var"),
        ("/usr/share/fonts/truetype/roboto/unhinted/RobotoCondensed-Bold.ttf", None),
        ("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf", None),
    ]
    for path, kind in cands:
        if os.path.isfile(path):
            return path, kind
    raise SystemExit("no usable font found")


FONT_PATH, FONT_KIND = find_font()
_fonts = {}


def font(size, weight=700):
    key = (size, weight)
    if key not in _fonts:
        f = ImageFont.truetype(FONT_PATH, size)
        if FONT_KIND == "var":
            try:
                f.set_variation_by_axes([weight])
            except Exception:
                pass
        _fonts[key] = f
    return _fonts[key]


def text(d, xy, s, size, fill, weight=700, anchor="la", spacing=0.0):
    """Text at SS scale, with optional letter spacing (in px at 1x)."""
    f = font(int(size * SS), weight)
    x, y = xy[0] * SS, xy[1] * SS
    if not spacing:
        d.text((x, y), s, font=f, fill=fill, anchor=anchor)
        return
    widths = [d.textlength(c, font=f) for c in s]
    total = sum(widths) + spacing * SS * (len(s) - 1)
    if anchor[0] == "m":
        x -= total / 2
    elif anchor[0] == "r":
        x -= total
    for c, w in zip(s, widths):
        d.text((x, y), c, font=f, fill=fill, anchor="l" + anchor[1])
        x += w + spacing * SS


def canvas(w, h, fill=(0, 0, 0, 0)):
    return Image.new("RGBA", (w * SS, h * SS), fill)


def down(img):
    return img.resize((img.width // SS, img.height // SS), Image.LANCZOS)


def rr(d, box, r, fill=None, outline=None, width=1):
    x0, y0, x1, y1 = [v * SS for v in box]
    d.rounded_rectangle((x0, y0, x1, y1), radius=r * SS, fill=fill, outline=outline, width=int(width * SS))


def vgrad(w, h, top, bottom):
    """A vertical gradient image at SS scale."""
    img = Image.new("RGBA", (w * SS, h * SS))
    px = img.load()
    for y in range(h * SS):
        c = mix(top, bottom, y / max(1, h * SS - 1)) + (255,)
        for x in range(w * SS):
            px[x, y] = c
    return img


def masked(grad, w, h, r):
    m = Image.new("L", (w * SS, h * SS), 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, w * SS - 1, h * SS - 1), radius=r * SS, fill=255)
    grad.putalpha(m)
    return grad


# ------------------------------------------------------------------------------------------ panel
def brushed(w, h, base, amount, seed):
    """Brushed metal / anodised panel: fine horizontal streaks (1x scale image)."""
    rnd = random.Random(seed)
    img = Image.new("RGB", (w, h), base)
    px = img.load()
    for y in range(h):
        row = rnd.uniform(-amount, amount)
        run, v = 0, 0.0
        for x in range(w):
            if run == 0:
                run, v = rnd.randint(20, 160), rnd.uniform(-amount, amount) * 0.6
            run -= 1
            k = row + v
            px[x, y] = tuple(max(0, min(255, int(c + k))) for c in base)
    return img


def panel_background(stripe):
    """The page: black panel, aluminium trims, and (on panel pages) the orange stripe the switches sit on."""
    img = brushed(W, H, PANEL, 3.0, 1)
    img.paste(brushed(W, 4, ALU, 14.0, 2), (0, 0))
    img.paste(brushed(W, H - TRIM_Y, ALU, 14.0, 3), (0, TRIM_Y))
    d = ImageDraw.Draw(img)
    d.line((0, 4, W, 4), fill=(0, 0, 0))
    d.line((0, TRIM_Y, W, TRIM_Y), fill=(250, 250, 250))
    if stripe:
        img.paste(brushed(W, STRIPE_H, STRIPE, 4.0, 4), (0, STRIPE_Y))
        d.line((0, STRIPE_Y, W, STRIPE_Y), fill=mix(STRIPE, (255, 255, 255), 0.25))
    else:
        img.paste(brushed(W, 8, STRIPE, 4.0, 4), (0, TRIM_Y - 8))
    return img.resize((W * SS, H * SS), Image.NEAREST).convert("RGBA")


def section_title(d, x, title, tag):
    """A section's name between two rules, as silk-screened on the panel."""
    cy = HEAD_Y + HEAD_H / 2
    f = font(19 * SS, 800)
    tw = (sum(d.textlength(ch, font=f) for ch in title) + 1.6 * SS * (len(title) - 1)) / SS
    text(d, (x + COL_W / 2, cy), title, 19, SILK, weight=800, anchor="mm", spacing=1.6)
    for a, b in ((x + 8, x + COL_W / 2 - tw / 2 - 10), (x + COL_W / 2 + tw / 2 + 10, x + COL_W - 8)):
        if b - a > 6:
            d.line((a * SS, cy * SS, b * SS, cy * SS), fill=SILK, width=int(1.6 * SS))
    if tag:
        text(d, (x + COL_W - 10, cy + 15), tag, 11, mix(SILK, PANEL, 0.35), weight=700, anchor="rm", spacing=1.0)


# ------------------------------------------------------------------------------------------ faders
def fader_cap():
    """The slider cap: a dark ribbed block with a white line across."""
    w, h = CAP_W, CAP_H
    cap = masked(vgrad(w, h, (74, 74, 78), (22, 22, 24)), w, h, 3)
    cd = ImageDraw.Draw(cap)
    rr(cd, (0, 0, w - 0.4, h - 0.4), 3, outline=(4, 4, 5), width=1.2)
    for k in (-10, -6, 6, 10):
        cd.line((4 * SS, (h / 2 + k) * SS, (w - 4) * SS, (h / 2 + k) * SS), fill=(12, 12, 13), width=int(1.4 * SS))
        cd.line((4 * SS, (h / 2 + k + 1.2) * SS, (w - 4) * SS, (h / 2 + k + 1.2) * SS), fill=(96, 96, 100), width=int(0.8 * SS))
    cd.line((3 * SS, h / 2 * SS, (w - 3) * SS, h / 2 * SS), fill=(250, 248, 240), width=int(3 * SS))
    return cap


def fader_strip(name, h):
    """The moving part of a vertical fader: the cap from the bottom (frame 0) to the top. The slot is in the page."""
    w = STRIP_W
    cap = fader_cap()
    shadow = Image.new("RGBA", (w * SS, (CAP_H + 16) * SS), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle(((w - CAP_W) / 2 * SS + SS, 7 * SS, (w + CAP_W) / 2 * SS + SS, (CAP_H + 9) * SS), radius=3 * SS, fill=(0, 0, 0, 170))
    shadow = shadow.filter(ImageFilter.GaussianBlur(2.2 * SS))
    strip = Image.new("RGBA", (w, h * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        fr = canvas(w, h)
        y = (h - CAP_H) * (1 - f / (FRAMES - 1))
        fr.alpha_composite(shadow, (0, int((y - 4) * SS)))
        fr.alpha_composite(cap, (int((w - CAP_W) / 2 * SS), int(y * SS)))
        strip.paste(down(fr), (0, f * h))
    strip.save(os.path.join(OUT, name), optimize=True)


def wrap_label(d, s, width, size):
    """A fader's name in up to three lines that fit its width; returns (lines, size)."""
    while True:
        f = font(size * SS, 700)
        fits = lambda t: d.textlength(t, font=f) + 0.5 * SS * len(t) <= width * SS
        lines, cur = [], ""
        for word in s.split(" "):
            if cur and fits(cur + " " + word):
                cur += " " + word
            else:
                if cur:
                    lines.append(cur)
                cur = word
        lines.append(cur)
        if (len(lines) <= 3 and all(fits(t) for t in lines)) or size <= 9:
            return lines, size
        size -= 1


def draw_fader(d, x, y, pitch, zone_h, c):
    """The fixed part of a fader, into the page: slot, the 0-10 scale, its name underneath."""
    cx = x + pitch / 2
    top, bot = y + FADER_TOP + CAP_H / 2, y + zone_h - FADER_BOTTOM - CAP_H / 2      # the cap's centre at 10 and at 0
    rr(d, (cx - 5, top - 12, cx + 5, bot + 12), 4, fill=(58, 58, 62))
    rr(d, (cx - 3.5, top - 11, cx + 3.5, bot + 11), 3, fill=(0, 0, 0))
    for i in range(11):
        ty = bot - (bot - top) * i / 10
        major = i in (0, 5, 10)
        d.line(((cx - (24 if major else 20)) * SS, ty * SS, (cx - 12) * SS, ty * SS), fill=SILK if major else mix(SILK, PANEL, 0.45), width=int((1.8 if major else 1.3) * SS))
        if major:
            text(d, (cx - 27, ty), str(i), 11, SILK, weight=700, anchor="rm")
        if c["bi"] and i == 5:
            d.line(((cx + 12) * SS, ty * SS, (cx + 24) * SS, ty * SS), fill=SILK, width=int(1.8 * SS))
    lines, size = wrap_label(d, c["label"], pitch - 6, 13)
    ly = y + zone_h - FADER_BOTTOM + 10
    for i, line in enumerate(lines):
        text(d, (cx, ly + i * (size + 2)), line, size, SILK, weight=700, anchor="ma", spacing=0.5)


# ------------------------------------------------------------------------------------------ buttons
def wave_points(kind, w, h):
    """A waveform symbol as a polyline in a w x h box (y down)."""
    top, bot, mid = 0.0, float(h), h / 2.0
    if kind == "sine":
        return [(w * i / 40.0, mid - math.sin(2 * math.pi * i / 40.0) * h / 2) for i in range(41)]
    if kind == "tri":
        return [(0, mid), (w * 0.25, top), (w * 0.75, bot), (w, mid)]
    if kind == "saw":
        return [(0, bot), (w * 0.5, top), (w * 0.5, bot), (w, top), (w, bot)]
    if kind == "square":
        return [(0, bot), (0, top), (w * 0.5, top), (w * 0.5, bot), (w, bot), (w, top)]
    if kind == "pulse":
        return [(0, bot), (w * 0.12, bot), (w * 0.12, top), (w * 0.36, top), (w * 0.36, bot), (w, bot)]
    if kind == "random":
        ys = [0.55, 0.1, 0.8, 0.35, 0.95, 0.25]
        pts = []
        for i, y in enumerate(ys):
            pts += [(w * i / len(ys), h * y), (w * (i + 1) / len(ys), h * y)]
        return pts
    if kind == "noise":
        rnd = random.Random(8)
        return [(w * i / 22.0, mid + (rnd.random() - 0.5) * h * (0.5 + 0.5 * rnd.random())) for i in range(23)]
    raise ValueError(kind)


WAVES = ("sine", "tri", "saw", "square", "pulse", "random", "noise")


def button(name, w, label, colour, on):
    """A panel switch: a coloured key with a red lamp above it, lit when on. label: text or a waveform name."""
    h = BT_H
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    # the lamp
    lx, ly = w / 2, 6
    if on:
        glow = canvas(w, h)
        ImageDraw.Draw(glow).ellipse(((lx - 13) * SS, (ly - 9) * SS, (lx + 13) * SS, (ly + 9) * SS), fill=(255, 40, 20, 130))
        img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(4 * SS)))
        d = ImageDraw.Draw(img)
    rr(d, (lx - 7, ly - 4, lx + 7, ly + 4), 2, fill=(0, 0, 0))
    rr(d, (lx - 5.5, ly - 2.8, lx + 5.5, ly + 2.8), 1.5, fill=(255, 70, 44) if on else (74, 16, 14))
    if on:
        rr(d, (lx - 3.5, ly - 1.8, lx + 1, ly - 0.4), 0.8, fill=(255, 190, 170))
    # the key
    ky = 15
    kh = h - ky
    rr(d, (0, ky, w, h), 4, fill=(0, 0, 0))
    top, bottom = (mix(colour, (255, 255, 255), 0.20), mix(colour, (0, 0, 0), 0.10)) if not on else (mix(colour, (0, 0, 0), 0.06), mix(colour, (0, 0, 0), 0.22))
    cap = masked(vgrad(w - 4, kh - 5, top, bottom), w - 4, kh - 5, 3)
    img.alpha_composite(cap, (2 * SS, (ky + (3 if on else 2)) * SS))
    d = ImageDraw.Draw(img)
    d.line((6 * SS, (ky + (4 if on else 3)) * SS, (w - 6) * SS, (ky + (4 if on else 3)) * SS), fill=mix(top, (255, 255, 255), 0.4), width=int(1.1 * SS))
    ink = (18, 16, 14)
    cy = ky + kh / 2 + (1 if on else 0)
    if label in WAVES:
        iw, ih = min(36, w - 20), 18
        pts = [((w / 2 - iw / 2 + x) * SS, (cy - ih / 2 + y) * SS) for x, y in wave_points(label, iw, ih)]
        d.line(pts, fill=ink, width=int(2.8 * SS), joint="curve")
    else:
        size = 18
        while size > 10 and d.textlength(label, font=font(size * SS, 800)) > (w - 10) * SS:
            size -= 1
        text(d, (w / 2, cy), label, size, ink, weight=800, anchor="mm")
    down(img).save(os.path.join(OUT, name), optimize=True)


def display_glass(w, h):
    """The red LED display window that shows a choice (drawn into the page background)."""
    img = canvas(w, h)
    d = ImageDraw.Draw(img)
    rr(d, (0, 0, w, h), 4, fill=(0, 0, 0))
    glass = masked(vgrad(w - 6, h - 6, (48, 10, 10), (20, 4, 5)), w - 6, h - 6, 3)
    img.alpha_composite(glass, (3 * SS, 3 * SS))
    d.line((10 * SS, 5 * SS, (w - 10) * SS, 5 * SS), fill=(96, 36, 34), width=int(1 * SS))
    return img


# ------------------------------------------------------------------------------------------ controls
# A control is a dict: kind, param (name in params.json), label (drawn on the panel), and for buttons
# the captions. Shorthands:
def S(param, label, bi=False):
    return {"kind": "slider", "param": param, "label": label, "bi": bi}


def SEL(param, label, caps):
    return {"kind": "select", "param": param, "label": label, "caps": caps}


def TOG(param, label, cap="ON"):
    return {"kind": "toggle", "param": param, "label": label, "cap": cap}


def MENU(param, label):
    return {"kind": "menu", "param": param, "label": label}


def DISP(param, label):
    return {"kind": "display", "param": param, "label": label}


def SLOT(i):
    return {"kind": "slot", "slot": i}


E = None
RANGE = ["16'", "8'", "4'", "2'"]


def patch_pages(pre, tabpre, part_level, last_block):
    """The three sound pages of a part. pre: parameter name prefix ('' or 'L ')."""
    p = lambda n: pre + n
    vco = [
        ("LFO", [S(p("LFO Rate"), "RATE"), S(p("LFO Delay"), "DELAY TIME"),
                 SEL(p("LFO Wave"), "WAVEFORM", ["sine", "saw", "square", "random"]), S(p("VCO LFO Mod"), "LFO MOD > VCO")]),
        ("VCO MODULATOR", [S(p("VCO Env Mod"), "ENV-1 MOD"), SEL(p("VCO Mod Dest"), "DESTINATION", ["VCO-1", "BOTH", "VCO-2"]),
                           S(p("Pulse Width"), "PULSE WIDTH MOD"), SEL(p("PWM Source"), "PWM SOURCE", ["LFO", "MANUAL", "ENV-1"])]),
        ("VCO-1", [SEL(p("VCO1 Range"), "RANGE", RANGE), SEL(p("VCO1 Wave"), "WAVEFORM", ["tri", "saw", "pulse", "square"]),
                   S(p("Cross Mod"), "CROSS MOD"), TOG(p("VCO2 Sync"), "SYNC VCO-2 TO VCO-1", "SYNC")]),
        ("VCO-2", [SEL(p("VCO2 Range"), "RANGE", RANGE), SEL(p("VCO2 Wave"), "WAVEFORM", ["sine", "saw", "pulse", "noise"]),
                   S(p("VCO2 Fine"), "FINE TUNE", bi=True), SEL(p("VCO2 Low Frq"), "FREQUENCY", ["NORMAL", "LOW"])]),
    ]
    vcf = [
        ("MIXER / HPF", [S(p("Source Mix"), "VCO-1 < MIX > VCO-2", bi=True), S(p("HPF Cutoff"), "HPF CUTOFF"),
                         SEL(p("Assign Mode"), "KEY ASSIGN", ["SOLO", "UNI", "POLY1", "POLY2"]), S(part_level, "PART LEVEL")]),
        ("VCF", [S(p("VCF Cutoff"), "CUTOFF FREQ"), S(p("VCF Reso"), "RESONANCE"),
                 SEL(p("VCF Slope"), "SLOPE", ["-12 dB", "-24 dB"]), S(p("VCF Key Flw"), "KEY FOLLOW")]),
        ("VCF MODULATION", [S(p("VCF Env Mod"), "ENV MOD"), SEL(p("VCF Env Sel"), "ENVELOPE", ["ENV-1", "ENV-2"]),
                            S(p("VCF LFO Mod"), "LFO MOD"), E]),
        last_block,
    ]
    env = [
        ("ENV-1", [S(p("Env1 Attack"), "ATTACK"), S(p("Env1 Decay"), "DECAY"), S(p("Env1 Sustain"), "SUSTAIN"), S(p("Env1 Release"), "RELEASE")]),
        ("ENV-2", [S(p("Env2 Attack"), "ATTACK"), S(p("Env2 Decay"), "DECAY"), S(p("Env2 Sustain"), "SUSTAIN"), S(p("Env2 Release"), "RELEASE")]),
        ("ENV MODES", [TOG(p("Env1 Key Flw"), "ENV-1 KEY FOLLOW", "KEY FLW"), SEL(p("Env1 Polar"), "ENV-1 POLARITY", ["NORMAL", "INVERT"]),
                       TOG(p("Env2 Key Flw"), "ENV-2 KEY FOLLOW", "KEY FLW"), E]),
        ("VCA", [S(p("VCA Level"), "LEVEL"), SEL(p("VCA LFO Mod"), "LFO MOD", ["0", "1", "2", "3"]), E, E]),
    ]
    return [(tabpre + "VCO", vco), (tabpre + "VCF", vcf), (tabpre + "ENV", env)]


def all_pages():
    up = patch_pages("", "", "Upper Level",
                     ("GLIDE / TUNE", [SEL("Portamento", "PORTAMENTO", ["OFF", "ON", "UPPER"]), S("Porta Time", "PORTAMENTO TIME"),
                                       S("Master Tune", "MASTER TUNE", bi=True), S("Analog Feel", "ANALOG FEEL")]))
    lo = patch_pages("L ", "L ", "Lower Level", ("LOWER PATCH", [MENU("Lower Preset", "LOAD PRESET"), E, E, E]))
    play = ("PLAY", [
        ("KEY MODE", [SEL("Key Mode", "KEY MODE", ["WHOLE", "DUAL", "SPLIT", "2PART"]), DISP("Split Point", "SPLIT POINT"),
                      TOG("Hold Upper", "HOLD UPPER", "HOLD"), TOG("Hold Lower", "HOLD LOWER", "HOLD")]),
        ("ARPEGGIO", [TOG("Arpeggio", "ARPEGGIO", "ARP"), SEL("Arp Mode", "MODE", ["UP", "DOWN", "U&D", "RND"]),
                      SEL("Arp Range", "RANGE (OCTAVES)", ["1", "2", "3", "4"]), S("Arp Rate", "RATE")]),
        ("OUTPUT", [S("Volume", "VOLUME"), S("Upper Level", "UPPER LEVEL"), S("Lower Level", "LOWER LEVEL"),
                    TOG("Stereo Layers", "LOWER LEFT / UPPER RIGHT", "STEREO")]),
        ("LOWER PART / MIDI", [MENU("Lower Preset", "LOAD PRESET > LOWER"), MENU("Upper MIDI Ch", "UPPER MIDI CH (2PART)"),
                               MENU("Lower MIDI Ch", "LOWER MIDI CH (2PART)"), E]),
    ])
    ctrl = ("CTRL", [
        ("BENDER > VCO", [S("Bend VCO", "SENSITIVITY"), TOG("Bend>VCO-1", "BEND VCO-1", "VCO-1"), TOG("Bend>VCO-2", "BEND VCO-2", "VCO-2"), E]),
        ("BENDER > VCF", [S("Bend VCF", "SENSITIVITY"), TOG("Bend>VCF", "BEND VCF", "VCF"), E, E]),
        ("WHEEL LFO > VCO", [S("Mod VCO", "SENSITIVITY"), TOG("Mod>VCO", "LFO MOD VCO", "VCO"), S("Mod Rise", "RISE TIME"), E]),
        ("WHEEL LFO > VCF", [S("Mod VCF", "SENSITIVITY"), TOG("Mod>VCF", "LFO MOD VCF", "VCF"), E, E]),
    ])
    mfx = ("MFX", [
        ("CHORUS & MFX", [TOG("MFX On", "EFFECT", "MFX"), MENU("MFX Type", "TYPE"), SLOT(0), SLOT(1)]),
        ("PARAMETERS", [SLOT(2), SLOT(3), SLOT(4), SLOT(5)]),
        ("", [SLOT(6), SLOT(7), SLOT(8), SLOT(9)]),
        ("", [SLOT(10), SLOT(11), SLOT(12), SLOT(13)]),
    ])
    mfx2 = ("MFX 2", [
        ("MORE PARAMETERS", [SLOT(14), SLOT(15), SLOT(16), SLOT(17)]),
        ("", [SLOT(18), SLOT(19), E, E]),
        ("", [E, E, E, E]),
        ("CHORUS & MFX", [TOG("MFX On", "EFFECT", "MFX"), MENU("MFX Type", "TYPE"), E, E]),
    ])
    delay = ("DELAY", [
        ("DELAY", [TOG("Delay On", "DELAY", "DELAY"), S("Dly Send", "SEND"), SEL("Dly Mode", "MODE", ["2 TAP", "PING-PONG"]), S("Dly>Reverb", "TO REVERB")]),
        ("TIME", [TOG("Dly Sync", "TEMPO SYNC", "SYNC"), S("Dly Time", "TIME (SYNC OFF)"), MENU("Dly Note", "NOTE (SYNC ON)"), S("Dly Feedback", "FEEDBACK", bi=True)]),
        ("TAPS", [S("Dly Pan 1", "PAN 1", bi=True), S("Dly Pan 2", "PAN 2", bi=True), S("Dly Level 1", "LEVEL 1"), S("Dly Level 2", "LEVEL 2")]),
        ("TONE / OUTPUT", [MENU("Dly Hi Damp", "HIGH DAMP"), S("Dly Output", "OUTPUT LEVEL"), E, E]),
    ])
    reverb = ("REVERB", [
        ("REVERB", [TOG("Reverb On", "REVERB", "REVERB"), S("Rev Send", "SEND"), SEL("Rev Mode", "MODE", ["STANDARD", "SHIMMER"]), E]),
        ("STANDARD", [MENU("Rev Type", "TYPE"), S("Rev PreDelay", "PRE-DELAY"), S("Rev Time", "TIME"), S("Rev Level", "LEVEL")]),
        ("", [E, E, E, E]),
        ("", [E, E, E, E]),
    ])
    shimmer = ("SHIMMER", [
        ("SHIMMER REVERB", [S("Shm Time", "TIME"), S("Shm Density", "DENSITY"), S("Shm PreDelay", "PRE-DELAY"), S("Shm Level", "LEVEL")]),
        ("DAMP / MODULATION", [S("Shm Low Damp", "LOW DAMP", bi=True), S("Shm Hi Damp", "HIGH DAMP", bi=True), S("Shm Mod Rate", "MOD RATE"), S("Shm Mod Dpth", "MOD DEPTH")]),
        ("PITCH 1", [S("Shm Pitch 1", "PITCH", bi=True), S("Shm Feedbk 1", "FEEDBACK", bi=True), S("Shm Level 1", "LEVEL"), E]),
        ("PITCH 2", [S("Shm Pitch 2", "PITCH", bi=True), S("Shm Feedbk 2", "FEEDBACK", bi=True), S("Shm Level 2", "LEVEL"), E]),
    ])
    pages = [(n, b, "u") for n, b in up] + [(play[0], play[1], "u"), (mfx[0], mfx[1], "u"), (mfx2[0], mfx2[1], "u"),
                                             (delay[0], delay[1], "u"), (reverb[0], reverb[1], "u"), (shimmer[0], shimmer[1], "u"),
                                             (ctrl[0], ctrl[1], "u")]
    pages += [(n, b, "l") for n, b in lo]
    return pages


NOTES = {
    "MFX 2": (COL_X[2], "Only Modulation Delay and\n2Voice Pitch Shift have more\nthan 14 parameters. With the\nother effects this page is empty."),
    "REVERB": (COL_X[2], "MODE SHIMMER uses the\ncontrols of the SHIMMER page\ninstead of the STANDARD\nsection: two pitch shifters in\nthe reverb's feedback."),
}

# The 16 controls the knobs hold in Program mode, Q-Link 1 (bottom left) to 16 (top right).
PROGRAM_QLINKS = ["Volume", "Porta Time", "Dly Send", "Rev Send",
                  "Env2 Attack", "Env2 Decay", "Env2 Sustain", "Env2 Release",
                  "LFO Rate", "VCO LFO Mod", "Source Mix", "Pulse Width",
                  "VCF Cutoff", "VCF Reso", "VCF Env Mod", "HPF Cutoff"]


# ------------------------------------------------------------------------------------------ JSON
def slug(cap):
    """A caption as part of a file name: 16' and 16 must not collide."""
    return re.sub(r"[^a-z0-9]", "", cap.lower().replace("'", "ft").replace("&", "and").replace("-", "m").replace(" ", "")) or "x"


class O(list):
    """An object as an ordered list of (key, value) pairs: the overlay widgets need two "version" keys."""


def obj(*pairs):
    return O(pairs)


def dumps(o):
    if isinstance(o, O):
        return "{" + ", ".join(json.dumps(k) + ": " + dumps(v) for k, v in o) + "}"
    if isinstance(o, (list, tuple)):
        return "[" + ", ".join(dumps(v) for v in o) + "]"
    return json.dumps(o)


TRANSPARENT = obj(("version", 1), ("focussed", obj(("version", 1), ("colour", "0"), ("image", ""))),
                  ("unfocussed", obj(("version", 1), ("colour", "0"), ("image", ""))))


def bounds(rect, focus="No", invalid="Show", when="Always", extra=None):
    b = [("version", 2 if extra is not None else 1), ("acceptsHWFocus", focus), ("showWhenDataModelInvalid", invalid),
         ("whenVisible", when), ("boundsType", "Absolute"), ("bounds", "%g %g %g %g" % tuple(rect))]
    if extra is not None:
        b.append(("additionalInvalidatingHandles", extra))
    return O(b)


def entry(name, typ, data, rect, remap=None, **kw):
    return obj(("version", 2),
               ("componentData", obj(("version", 1), ("name", name), ("type", typ), ("data", data))),
               ("handle remapping", obj(("version", 1), ("map", [obj(("key", k), ("value", v)) for k, v in (remap or [])]))),
               ("bounds", bounds(rect, **kw)))


def action(on, handler, extra=""):
    return obj(("version", 1), ("onAction", on), ("handler", handler), ("handleName", "Data"), ("additionalData", extra))


def component(children, overlay=None):
    acts = [action("Mouse Down", "Q-Link")]
    if overlay:
        acts += [action("Double Click", "Show Overlay", overlay), action("Enter Pressed", "Show Overlay", overlay)]
    return obj(("version", 2), ("actions", acts), ("backgroundData", TRANSPARENT), ("ignoreMousePresses", False),
               ("disableCoarseDataWheel", False), ("componentsData", children))


def text_style(height, colour, just, style="SemiBold", case="Original", name="Titillium Web"):
    return obj(("version", 1), ("font", obj(("version", 1), ("name", name), ("style", style), ("height", float(height)))),
               ("colour", colour), ("justification", just), ("case", case))


def label(kind, rect, height, colour, just, style="SemiBold", case="Original"):
    return entry(kind.lower(), "Label", obj(("version", 1), ("textStyle", text_style(height, colour, just, style, case)),
                                            ("type", kind), ("handleName", "Data")), rect)


def focus_ring(accent, w, h):
    return entry("focus", "Focus", obj(("version", 1), ("backgroundColour", "0cffffff"), ("outlineColour", argb(accent)),
                                       ("backgroundInset", 3.0), ("outlineThickness", 2.0)),
                 (0, 0, w, h), when="WhenFocussed")


def overlay_component(typ, data_pairs):
    focus_lost = [obj(("version", 1), ("onAction", "Focus Lost"), ("handler", "Focus Lost"), ("handleName", "Focus Lost"), ("additionalData", ""))]
    data = O([data_pairs[0], ("version", 1), ("actions", focus_lost)] + data_pairs[1:])
    dark = obj(("version", 1), ("focussed", obj(("version", 1), ("colour", "ff1e1d1e"), ("image", ""))),
               ("unfocussed", obj(("version", 1), ("colour", "ff1e1d1e"), ("image", ""))))
    return obj(("version", 2), ("actions", []), ("backgroundData", dark), ("ignoreMousePresses", True), ("disableCoarseDataWheel", False),
               ("componentsData", [entry("overlay", typ, data, (0, 0, 1280, 800))]))


def knob_overlay(polarity):
    return overlay_component("KnobOverlay", [("version", 2), ("overlayType", "Knob With Header"), ("scaleType", "Linear"),
                                             ("polarityType", polarity), ("showMinusSign", True), ("handleName", "Data")])


# ------------------------------------------------------------------------------------------ layout
def is_grid(blocks):
    """The MFX pages keep the four-by-four grid: their controls are named by the plugin, not by the panel."""
    return any(c is not None and c["kind"] == "slot" for _t, cells in blocks for c in cells)


def layout(blocks):
    """Where each control of a page goes. Returns a list of items:
         {c, col, row, rect, form: 'fader' | 'row' | 'slot', stripe, colour, pitch}
       Panel pages: a section's sliders stand side by side as vertical faders; its switches are rows under them,
       the last one on the orange stripe. A section with one slider and three switches puts the slider at the left."""
    items = []
    grid = is_grid(blocks)
    n_black = n_stripe = 0
    for c_i, (_title, cells) in enumerate(blocks):
        x = COL_X[c_i]
        live = [(r_i, c) for r_i, c in enumerate(cells) if c is not None]
        if grid:
            for r_i, c in live:
                if c["kind"] == "slot":
                    items.append(dict(c=c, col=c_i, row=r_i, rect=(x, CELL_Y[r_i], COL_W, CELL_H), form="slot"))
                else:
                    items.append(dict(c=c, col=c_i, row=r_i, rect=(x, CELL_Y[r_i] + 20, COL_W, ROW_H), form="row", stripe=False,
                                      colour=BLACK_CAPS[n_black % len(BLACK_CAPS)]))
                    n_black += 1
            continue
        faders = [(r_i, c) for r_i, c in live if c["kind"] == "slider"]
        rows = [(r_i, c) for r_i, c in live if c["kind"] != "slider"]
        above = rows[:-1]
        if rows:
            r_i, c = rows[-1]
            items.append(dict(c=c, col=c_i, row=r_i, rect=(x, STRIPE_Y, COL_W, ROW_H), form="row", stripe=True,
                              colour=STRIPE_CAPS[n_stripe % len(STRIPE_CAPS)]))
            n_stripe += 1
        side = bool(faders) and len(above) >= 2
        zone_h = ZONE_LONG if (side or not above or not faders) else ZONE_SHORT
        if side:
            pitch = 84
            fx = x
            rx, rw = x + pitch * len(faders) + 4, COL_W - pitch * len(faders) - 4
        else:
            pitch = COL_W // len(faders) if len(faders) >= 3 else 104
            fx = x + (COL_W - pitch * len(faders)) // 2 if faders else x
            rx, rw = x, COL_W
        for k, (r_i, c) in enumerate(faders):
            items.append(dict(c=c, col=c_i, row=r_i, rect=(fx + k * pitch, TOP, pitch, zone_h), form="fader", pitch=pitch))
        if above:
            if faders and not side:
                ys = [STRIPE_Y - ROW_H - 6]
            else:
                gap = (ZONE_LONG - ROW_H * len(above)) / (len(above) + 1)
                gap = min(gap, 70)
                y0 = TOP + (16 if not faders else (ZONE_LONG - ROW_H * len(above) - gap * (len(above) - 1)) / 2)
                ys = [int(y0 + k * (ROW_H + gap)) for k in range(len(above))]
            for (r_i, c), y in zip(above, ys):
                items.append(dict(c=c, col=c_i, row=r_i, rect=(rx, y, rw, ROW_H), form="row", stripe=False,
                                  colour=BLACK_CAPS[n_black % len(BLACK_CAPS)]))
                n_black += 1
    return items


def button_widths(w, n):
    gap = 5
    return (w - 2 * PAD - gap * (n - 1)) // n, gap


TOG_W = 104


def row_buttons(it):
    """[(image base name, x, width, caption)] of a row of switches."""
    c, w = it["c"], it["rect"][2]
    if c["kind"] == "select":
        bw, gap = button_widths(w, len(c["caps"]))
        return [("b_%s_%d_%s" % (it["colour"], bw, slug(cap)), PAD + i * (bw + gap), bw, cap) for i, cap in enumerate(c["caps"])]
    if c["kind"] == "toggle":
        return [("b_%s_%d_%s" % (it["colour"], TOG_W, slug(c["cap"])), PAD, TOG_W, c["cap"])]
    return []


# ------------------------------------------------------------------------------------------ build
def main():
    params_path = os.path.join(ROOT, "build", "params.json")
    if not os.path.isfile(params_path):
        raise SystemExit("build/params.json is missing: run the SDK's tools/build.py --target native first")
    info = json.load(open(params_path))
    index = {}
    for p in info["params"]:
        if p["name"]:
            index.setdefault(p["name"], p["index"])
    slot0 = index["MFX Type"] + 1
    kinds = {p["index"]: p for p in info["params"]}

    # choice lists the host-sim dump cannot see whole (more than 16 choices): read them from the source
    fxsrc = open(os.path.join(ROOT, "src", "fx_params.cpp")).read()

    def c_array(name):
        m = re.search(r"%s\[\]\s*=\s*\{(.*?)\};" % re.escape(name), fxsrc, re.S)
        return re.findall(r'"([^"]*)"', m.group(1))

    long_choices = {
        "MFX Type": re.findall(r'T\("([^"]+)"', fxsrc),
        "Dly Note": c_array("kNotes"),
        "Dly Hi Damp": c_array("kDamp"),
        "Lower Preset": ["- - -"] + info["programs"],
        "Upper MIDI Ch": ["Omni"] + [str(i) for i in range(1, 17)],
        "Lower MIDI Ch": ["Omni"] + [str(i) for i in range(1, 17)],
    }
    assert len(long_choices["MFX Type"]) == 17 and len(long_choices["Dly Note"]) == 22 and len(long_choices["Dly Hi Damp"]) == 18

    os.makedirs(OUT, exist_ok=True)
    os.makedirs(PREVIEW, exist_ok=True)
    for d_ in (OUT, PREVIEW):
        for f in os.listdir(d_):
            if f.endswith((".png", ".json")):
                os.remove(os.path.join(d_, f))

    pages = all_pages()
    defs = {}                # component name -> definition
    images = {}              # button image base name -> (w, caption, colour)
    replacement = {}         # parameter index -> strings
    tabs, qmaps, layouts = [], [], []

    for zone_h, tag in ((ZONE_LONG, "long"), (ZONE_SHORT, "short")):
        fader_strip("fader_%s.png" % tag, zone_h - FADER_TOP - FADER_BOTTOM)

    def choices_of(c, n=None):
        name = c["param"]
        if name in long_choices:
            return long_choices[name]
        ch = kinds[index[name]]["choices"]
        if not ch and n is not None:        # the dump sees "0 1 2 3" as a number, not as choices
            return list(c["caps"])
        if n is not None and len(ch) != n:
            raise SystemExit("%s: %d captions for %d choices %r" % (name, n, len(ch), ch))
        return ch

    def ring(w, h):
        return focus_ring(FOCUS, w, h)

    def item_component(it):
        """Returns the name of the component that draws a control (creating it on first use)."""
        c = it["c"]
        k = c["kind"]
        _x, _y, w, h = it["rect"]
        if it["form"] == "fader":
            tag = "long" if h == ZONE_LONG else "short"
            name = "fader_%s_%d%s" % (tag, w, "_bi" if c["bi"] else "")
            if name not in defs:
                defs[name] = component([
                    ring(w, h),
                    label("Value", (0, 2, w, 24), 20, VALUE_ARGB, "horizontallyCentred verticallyCentred"),
                    entry("fader", "Knob", obj(("version", 3), ("knobType", "FilmStrip"), ("filmStrip", "fader_%s.png" % tag), ("numFrames", FRAMES),
                                               ("invert", False), ("dragOrientation", "Vertical"), ("handleName", "Data")),
                          ((w - STRIP_W) // 2, FADER_TOP, STRIP_W, h - FADER_TOP - FADER_BOTTOM)),
                ], "knob overlay bipolar" if c["bi"] else "knob overlay")
            return name
        if it["form"] == "slot":
            name = "slot"
            if name not in defs:
                defs[name] = component([
                    ring(w, h),
                    label("Name", (PAD + 2, 8, 170, 34), 22, argb(SILK), "left verticallyCentred", style="Bold", case="Upper Case"),
                    label("Value", (150, 8, COL_W - 150 - PAD, 34), 26, VALUE_ARGB, "right verticallyCentred"),
                    entry("bar", "Slider", obj(("version", 2), ("sliderType", "ColourBar"), ("direction", "Horizontal"),
                                               ("backgroundColour", "ff000000"), ("outlineColour", "ff3a3a3e"), ("valueColour", argb(STRIPE)),
                                               ("isBipolar", False), ("handleName", "Data")),
                          (PAD + 2, 66, CTRL_W - 4, 40)),
                ], "knob overlay")
            return name
        if k in ("select", "toggle"):
            btns = row_buttons(it)
            name = "%s_%d_%s_%s" % ("sel" if k == "select" else "tog", w, it["colour"], "_".join(slug(b[3]) for b in btns))
            if name not in defs:
                children = [ring(w, h)]
                for i, (img, bx, bw, cap) in enumerate(btns):
                    images[img] = (bw, cap, it["colour"])
                    children.append(entry("button %d" % i, "Button",
                                          obj(("version", 2), ("onImage", img + "_on.png"), ("offImage", img + "_off.png"),
                                              ("buttonId", i if k == "select" else 1), ("numButtonsInGroup", len(btns) if k == "select" else 1),
                                              ("handleName", "Data"), ("gestureBehaviour", "Instant")),
                                          (bx, BT_Y, bw, BT_H)))
                defs[name] = component(children, "knob overlay")   # MenuOverlay is empty on real firmware
            return name
        if k in ("menu", "display"):
            name = "%s_%d" % (k, w)
            if name not in defs:
                defs[name] = component([
                    ring(w, h),
                    label("Value", (PAD + 6, GLASS_Y, w - 2 * PAD - 12, GLASS_H), 27 if k == "menu" else 32, DISPLAY_ARGB,
                          "horizontallyCentred verticallyCentred", style="Bold"),
                ], "knob overlay")    # not "menu overlay": its list comes up empty on real firmware (SDK examples too)
            return name
        raise ValueError(k)

    for tab_i, (tab_name, blocks, acc) in enumerate(pages):
        grid = is_grid(blocks)
        bg = panel_background(stripe=not grid)
        d = ImageDraw.Draw(bg)
        children = []
        qlinks = {q: -1 for q in range(1, 17)}
        for c_i, (title, _cells) in enumerate(blocks):
            if title:
                section_title(d, COL_X[c_i], title, "LOWER" if acc == "l" else "")
            if c_i:
                xd = COL_X[c_i] - 4
                d.line((xd * SS, (HEAD_Y + 4) * SS, xd * SS, (STRIPE_Y - 8 if not grid else TRIM_Y - 16) * SS), fill=mix(SILK, PANEL, 0.72), width=int(1.2 * SS))
        items = layout(blocks)
        layouts.append(items)
        for it in items:
            c = it["c"]
            x, y, w, h = it["rect"]
            comp = item_component(it)
            if it["form"] == "slot":
                pidx = slot0 + c["slot"]
                invalid = "HideOnInvalidName"
            else:
                pidx = index[c["param"]]
                invalid = "Hide"
                if it["form"] == "fader":
                    draw_fader(d, x, y, w, h, c)
                else:
                    ink = STRIPE_INK if it["stripe"] else SILK
                    text(d, (x + PAD + 1, y + 14), c["label"], 15, ink, weight=800, anchor="lm", spacing=0.7)
                    if c["kind"] in ("menu", "display"):
                        bg.alpha_composite(display_glass(w - 2 * PAD, GLASS_H), ((x + PAD) * SS, (y + GLASS_Y) * SS))
                        d = ImageDraw.Draw(bg)
                    if c["kind"] == "select":
                        replacement[pidx] = choices_of(c, len(c["caps"]))
                    elif c["kind"] == "toggle":
                        replacement[pidx] = ["Off", "On"]
                    elif c["kind"] == "menu":
                        replacement[pidx] = choices_of(c)
            it["pidx"] = pidx
            children.append(entry(c.get("param", "mfx slot %d" % (c.get("slot", 0) + 1)), comp, obj(("version", 1), ("handleName", "Data")),
                                  (x, y, w, h), remap=[("Data", "Parameter %d" % pidx)], focus="Yes", invalid=invalid))
            qlinks[13 + it["col"] - 4 * it["row"]] = pidx
        if tab_name in NOTES:
            nx, msg = NOTES[tab_name]
            for li, line in enumerate(msg.split("\n")):
                text(d, (nx + 16, 120 + li * 26), line, 17, mix(SILK, PANEL, 0.45), weight=500)
        bg_name = "page_%02d.png" % (tab_i + 1)
        down(bg).convert("RGB").save(os.path.join(OUT, bg_name), optimize=True)
        root = obj(("version", 2), ("actions", []),
                   ("backgroundData", obj(("version", 1), ("focussed", obj(("version", 1), ("colour", argb(PANEL)), ("image", ""))),
                                          ("unfocussed", obj(("version", 1), ("colour", argb(PANEL)), ("image", ""))))),
                   ("ignoreMousePresses", False), ("disableCoarseDataWheel", False),
                   ("componentsData", [entry("panel", "Image", obj(("version", 2), ("imageType", "Regular"), ("colour", "0"), ("image", bg_name)),
                                             (0, 0, W, H))] + children))
        tabs.append(obj(("version", 1), ("tabName", tab_name), ("fnKeyIndex", tab_i), ("fnKeySubIndex", 0), ("componentDefinition", root),
                        ("qlinkBoundsData", ["%d %d %d %d" % (cx - 3, 6, COL_W + 6, TRIM_Y - 8) for cx in COL_X])))
        qmaps.append(obj(("Tab", tab_i + 1), ("SubTab", 1), ("Bank Direction", "Column"),
                         ("Q-Links", O([("Q-Link %d" % q, qlinks[q]) for q in range(1, 17)]))))

    for img, (w, cap, colour) in images.items():
        button(img + "_on.png", w, cap, CAPS[colour], True)
        button(img + "_off.png", w, cap, CAPS[colour], False)

    defs["knob overlay"] = knob_overlay("Unipolar")
    defs["knob overlay bipolar"] = knob_overlay("Bipolar")

    tui = obj(("pageData", obj(
        ("version", 1),
        ("componentDefinitions", obj(("version", 2), ("importFiles", []),
                                     ("localComponentDefinitions", [obj(("key", k), ("value", v)) for k, v in defs.items()]))),
        ("replacementStrings", [obj(("key", "Parameter %d" % i), ("value", v)) for i, v in sorted(replacement.items())]),
        ("info", obj(("version", 1), ("type", "CompleteDescription"))),
        ("tabs", tabs))))
    open(os.path.join(OUT, "TUI.json"), "w").write(dumps(tui) + "\n")

    qfile = obj(("version", 4), ("info", obj(("version", 1), ("type", "CompleteDescription"))),
                ("Screen Mode Q-Links", obj(("version", 4), ("map", qmaps))),
                ("Program Mode Q-Links", O([("Q-Link %d" % (i + 1), index[n]) for i, n in enumerate(PROGRAM_QLINKS)])))
    open(os.path.join(OUT, "Q-Links.json"), "w").write(dumps(qfile) + "\n")

    make_previews(pages, layouts, info)
    n_png = len([f for f in os.listdir(OUT) if f.endswith(".png")])
    size = sum(os.path.getsize(os.path.join(OUT, f)) for f in os.listdir(OUT))
    print("skins/: %d tabs, %d components, %d images, %.1f MB; previews in preview/" % (len(tabs), len(defs), n_png, size / 1e6))


# ------------------------------------------------------------------------------------------ previews
def make_previews(pages, layouts, info):
    """What each page looks like at the default values (the MPC's own text is approximated)."""
    params = {p["index"]: p for p in info["params"]}
    strips = {}
    roboto = "/usr/share/fonts/truetype/roboto/unhinted/RobotoCondensed-Bold.ttf"
    vfont = lambda size: ImageFont.truetype(roboto, size) if os.path.isfile(roboto) else font(size)
    value, red = (255, 226, 184), (255, 90, 72)
    for tab_i, ((tab_name, _blocks, _acc), items) in enumerate(zip(pages, layouts)):
        img = Image.open(os.path.join(OUT, "page_%02d.png" % (tab_i + 1))).convert("RGBA")
        d = ImageDraw.Draw(img)
        for it in items:
            c = it["c"]
            x, y, w, h = it["rect"]
            p = params[it["pidx"]]
            val, disp = p["default"], p["display"]
            if it["form"] == "fader":
                key = "fader_%s.png" % ("long" if h == ZONE_LONG else "short")
                if key not in strips:
                    strips[key] = Image.open(os.path.join(OUT, key))
                sh = h - FADER_TOP - FADER_BOTTOM
                f = int(round(val * (FRAMES - 1)))
                img.alpha_composite(strips[key].crop((0, f * sh, STRIP_W, (f + 1) * sh)), (x + (w - STRIP_W) // 2, y + FADER_TOP))
                d.text((x + w / 2, y + 14), disp, font=vfont(19), fill=value, anchor="mm")
            elif it["form"] == "slot":
                if not p["name"]:
                    continue
                d.text((x + PAD + 2, y + 25), p["name"].upper(), font=vfont(21), fill=SILK, anchor="lm")
                d.text((x + COL_W - PAD, y + 25), disp, font=vfont(24), fill=value, anchor="rm")
                d.rectangle((x + PAD + 2, y + 66, x + PAD + CTRL_W - 2, y + 106), fill=(0, 0, 0), outline=(58, 58, 62))
                d.rectangle((x + PAD + 3, y + 67, x + PAD + 3 + int((CTRL_W - 6) * val), y + 105), fill=STRIPE)
            elif c["kind"] in ("select", "toggle"):
                btns = row_buttons(it)
                cur = int(round(val * (len(btns) - 1))) if c["kind"] == "select" else (0 if val >= 0.5 else -1)
                for i, (name, bx, _bw, _cap) in enumerate(btns):
                    img.alpha_composite(Image.open(os.path.join(OUT, "%s_%s.png" % (name, "on" if i == cur else "off"))), (x + bx, y + BT_Y))
            else:
                d.text((x + w / 2, y + GLASS_Y + GLASS_H / 2), disp, font=vfont(26 if c["kind"] == "menu" else 31), fill=red, anchor="mm")
        img.convert("RGB").save(os.path.join(PREVIEW, "%02d_%s.png" % (tab_i + 1, tab_name.replace(" ", "_"))))


if __name__ == "__main__":
    main()
