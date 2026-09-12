#!/usr/bin/env python3
"""Generate carrier-board.kicad_sch from the netlist, pulling symbols from the stock KiCad libs.

One-off scaffolding: the .kicad_sch it emits is the artifact that lives in git.
UUIDs are uuid5-derived so re-running produces a byte-identical file.

Pin map authority is the FIRMWARE, not the design docs:
  WLED-MM/usermods/room_mic_uploader/room_mic_uploader.h   (npEnc[], UART_RX/TX, SW[])
  WLED-MM/wled00/bus_manager.cpp SECTION9_2_PINOUT         (HUB75 gpio[])
"""
import uuid, sys, os

NS = uuid.UUID("c4a77b10-0000-4000-8000-000000000000")
U = lambda k: str(uuid.uuid5(NS, k))
SHEET = U("sheet:root")
PROJECT = "carrier-board"

SYMDIR = "/usr/share/kicad/symbols"


def sexp_block(text, header, start=0):
    """Extract the balanced s-expression starting at `header`."""
    i = text.index(header, start)
    d = 0
    for j in range(i, len(text)):
        if text[j] == "(":
            d += 1
        elif text[j] == ")":
            d -= 1
            if d == 0:
                return text[i : j + 1]
    raise ValueError(header)


def load_symbol(lib, name):
    src = open(f"{SYMDIR}/{lib}.kicad_sym").read()
    blk = sexp_block(src, f'(symbol "{name}"')
    return blk.replace(f'(symbol "{name}"', f'(symbol "{lib}:{name}"', 1)


def pins_of(blk):
    """-> {number: (x, y, angle)} in symbol space (Y up). `at` is the connection point."""
    out, idx = {}, 0
    while True:
        try:
            i = blk.index("(pin ", idx)
        except ValueError:
            return out
        d = 0
        for j in range(i, len(blk)):
            if blk[j] == "(":
                d += 1
            elif blk[j] == ")":
                d -= 1
                if d == 0:
                    pin, idx = blk[i : j + 1], j
                    break
        if pin.startswith("(pin_"):
            idx = i + 5
            continue
        at = pin[pin.index("(at ") + 4 :]
        at = at[: at.index(")")].split()
        num = pin[pin.rindex('(number "') + 9 :]
        num = num[: num.index('"')]
        out[num] = (float(at[0]), float(at[1]), int(float(at[2])))


# stub direction in SCHEMATIC space (Y down) for a given symbol-space pin angle
STUB = {0: (-1, 0), 90: (0, 1), 180: (1, 0), 270: (0, -1)}

SYMS = [
    ("Connector", "Conn_01x22_Socket"),
    ("Connector", "Conn_01x08_Pin"),
    ("Connector", "Conn_01x06_Pin"),
    ("Connector", "Conn_01x05_Pin"),
    ("Connector", "Conn_01x02_Pin"),
    ("Connector", "Screw_Terminal_01x02"),
    ("Connector_Generic", "Conn_02x08_Odd_Even"),
    ("Transistor_FET", "Q_PMOS_GDS"),
    ("Device", "R"),
    ("Device", "C"),
    ("Device", "C_Polarized"),
]
LIBS = {f"{lib}:{name}": load_symbol(lib, name) for lib, name in SYMS}
PINS = {k: pins_of(v) for k, v in LIBS.items()}

# ---- footprints (every one is stock; no custom library)
FP_SOCK22 = "Connector_PinSocket_2.54mm:PinSocket_1x22_P2.54mm_Vertical"
FP_XH8 = "Connector_JST:JST_XH_B8B-XH-A_1x08_P2.50mm_Vertical"
FP_XH6 = "Connector_JST:JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical"
FP_XH5 = "Connector_JST:JST_XH_B5B-XH-A_1x05_P2.50mm_Vertical"
FP_HDR2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
FP_SCREW = "Connector_Phoenix_MSTB:PhoenixContact_MSTBVA_2,5_2-G_1x02_P5.00mm_Vertical"
FP_IDC16 = "Connector_IDC:IDC-Header_2x08_P2.54mm_Latch_Vertical"
FP_TO252 = "Package_TO_SOT_SMD:TO-252-3_TabPin2"
FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"
FP_C = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
FP_CP = "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"

G = 1.27  # symbol origins must sit on the 1.27mm connection grid
NC = None  # pin gets an explicit no-connect instead of a label

# ---- ESP32-S3 devkit headers.  Order is Espressif's published J1/J3 table for the
# GENUINE ESP32-S3-DevKitC-1, which is what this board is cut for: board 25.40mm wide,
# rows inset 1.27mm each side => 22.86mm (0.9") row spacing, per Espressif's dimension
# drawing DXF_ESP32-S3-DevKitC-1_V1.  The YD-ESP32-S3 clone on the bench measures 25.4mm
# (1.0") and will NOT seat in this board -- it is one 0.1" grid step wider.
ESP_L = {  # J1
    "1": "P3V3", "2": "P3V3", "3": NC,            # 3V3, 3V3, RST
    "4": "HUB_R1", "5": "HUB_G1", "6": "HUB_B1",  # GPIO 4, 5, 6
    "7": "HUB_R2", "8": "HUB_G2", "9": "HUB_B2",  # GPIO 7, 15, 16
    "10": "UART_RX", "11": "UART_TX",             # GPIO 17 RX, 18 TX  <-- NOT reversed
    "12": "HUB_A",                                # GPIO 8
    "13": NC, "14": NC,                           # GPIO 3, 46 -- forbidden
    "15": "HUB_B", "16": "HUB_C", "17": "HUB_D",  # GPIO 9, 10, 11
    "18": "BRIGHT_B",                             # GPIO 12
    "19": "HUB_LAT", "20": "HUB_OE",              # GPIO 13, 14
    "21": "ESP_5V", "22": "GND",
}
ESP_R = {  # J3
    "1": "GND", "2": NC, "3": NC,                 # G, U0TXD, U0RXD
    "4": "BRIGHT_A", "5": "SW_LADDER",            # GPIO 1, 2 (ADC1_CH1)
    "6": "GAIN_B", "7": "GAIN_A",                 # GPIO 42, 41
    "8": "MIC_SD", "9": "MIC_WS", "10": "MIC_SCK",# GPIO 40, 39, 38
    "11": NC, "12": NC, "13": NC,                 # GPIO 37, 36, 35 -- dead on N16R8
    "14": NC, "15": NC,                           # GPIO 0, 45 -- forbidden
    "16": "MODE_B", "17": "MODE_A",               # GPIO 48, 47
    "18": "HUB_CLK",                              # GPIO 21
    "19": NC, "20": NC,                           # GPIO 20, 19 -- USB D+/D-
    "21": "GND", "22": "GND",
}

# ref, lib_id, x, y, value, footprint, mpn, {pin: net}
PARTS = [
    # ---- power chain: barrel -> screw term -> panel toggle -> reverse-polarity FET -> 5V
    ("J_PWR", "Connector:Screw_Terminal_01x02", 24 * G, 24 * G, "DC IN 5V", FP_SCREW, "",
     {"1": "VIN_RAW", "2": "GND"}),
    ("J_SW", "Connector:Screw_Terminal_01x02", 24 * G, 40 * G, "MASTER SW", FP_SCREW, "",
     {"1": "VIN_RAW", "2": "VIN_SW"}),
    # Drain on the INPUT side: the body diode must point the way current normally
    # flows, or a reversed supply just conducts through it and the part does nothing.
    ("Q1", "Transistor_FET:Q_PMOS_GDS", 56 * G, 26 * G, "AOD403", FP_TO252, "AOD403",
     {"1": "GATE", "2": "VIN_SW", "3": "P5V"}),
    ("RG", "Device:R", 48 * G, 34 * G, "100k", FP_R, "",
     {"1": "GATE", "2": "GND"}),
    ("CB1", "Device:C_Polarized", 72 * G, 32 * G, "470uF/10V", FP_CP, "",
     {"1": "P5V", "2": "GND"}),
    ("CB2", "Device:C", 82 * G, 32 * G, "100nF", FP_C, "",
     {"1": "P5V", "2": "GND"}),
    # ESP32 5V isolation: the devkit has no reverse-current protection on USB VBUS,
    # so a live 5V pin + a plugged-in USB cable back-feeds the host port.
    ("JP1", "Connector:Conn_01x02_Pin", 72 * G, 48 * G, "ESP 5V", FP_HDR2, "",
     {"1": "P5V", "2": "ESP_5V"}),
    ("J_PANEL", "Connector:Screw_Terminal_01x02", 24 * G, 56 * G, "PANEL 5V", FP_SCREW, "",
     {"1": "P5V", "2": "GND"}),
    # Pi 5V rides TWO jumpers, one per Pi header pin, so each shunt carries <=1.5A --
    # inside a standard 2.54mm shunt's rating.  Pulling both still isolates the Pi.
    ("JP2", "Connector:Conn_01x02_Pin", 72 * G, 58 * G, "PI 5V A", FP_HDR2, "",
     {"1": "P5V", "2": "PI_5V_A"}),
    ("JP3", "Connector:Conn_01x02_Pin", 72 * G, 68 * G, "PI 5V B", FP_HDR2, "",
     {"1": "P5V", "2": "PI_5V_B"}),
    ("C3V3", "Device:C", 92 * G, 32 * G, "100nF", FP_C, "",
     {"1": "P3V3", "2": "GND"}),

    # ---- the module
    ("J_ESP_L", "Connector:Conn_01x22_Socket", 116 * G, 40 * G, "ESP32-S3 J1", FP_SOCK22, "", ESP_L),
    ("J_ESP_R", "Connector:Conn_01x22_Socket", 116 * G, 100 * G, "ESP32-S3 J3", FP_SOCK22, "", ESP_R),

    # ---- peripheral connectors
    ("J_HUB", "Connector_Generic:Conn_02x08_Odd_Even", 176 * G, 26 * G, "HUB75", FP_IDC16, "",
     {"1": "HUB_R1", "2": "HUB_G1", "3": "HUB_B1", "4": "GND",
      "5": "HUB_R2", "6": "HUB_G2", "7": "HUB_B2", "8": "GND",
      "9": "HUB_A", "10": "HUB_B", "11": "HUB_C", "12": "HUB_D",
      "13": "HUB_CLK", "14": "HUB_LAT", "15": "HUB_OE", "16": "GND"}),
    ("J_KNOBS", "Connector:Conn_01x08_Pin", 176 * G, 62 * G, "KNOBS", FP_XH8, "B8B-XH-A(LF)(SN)",
     {"1": "MODE_A", "2": "MODE_B", "3": "BRIGHT_A", "4": "BRIGHT_B",
      "5": "GAIN_A", "6": "GAIN_B", "7": "SW_LADDER", "8": "GND"}),
    ("J_MIC", "Connector:Conn_01x06_Pin", 176 * G, 90 * G, "MIC", FP_XH6, "B6B-XH-A(LF)(SN)",
     {"1": "MIC_SCK", "2": "MIC_WS", "3": "MIC_SD", "4": "GND", "5": "P3V3", "6": "GND"}),
    # Pins map 1:1 onto Raspberry Pi header pins 2, 4, 6, 8, 10 in order -- the cable is
    # straight through, no crossing.  UART_RX takes the Pi's TXD (pin 8) and UART_TX feeds
    # the Pi's RXD (pin 10): the crossover happens here, once, and nowhere else.
    # ponytail: GND is one XH contact (3A rated) carrying both 5V legs' return.  A Pi 4 +
    # SSD measures ~1.2A, so 2.5x margin; if a future load pushes past ~2A sustained, the
    # fix is a 6th contact onto Pi pin 9, which IS a respin -- measure before assuming.
    ("J_PI", "Connector:Conn_01x05_Pin", 176 * G, 112 * G, "PI 2/4/6/8/10", FP_XH5,
     "B5B-XH-A(LF)(SN)",
     {"1": "PI_5V_A", "2": "PI_5V_B", "3": "GND", "4": "UART_RX", "5": "UART_TX"}),
]

# ---- knob-line filtering.  These live HERE, not on the knob board: their job is to
# filter what an enclosure-length cable picks up, which only works at the receiving end.
# 10nF (not 100nF) on the six rotation lines -- 10k*100nF = 1ms, and a brisk EC11 spin
# puts edges ~4ms apart, so 100nF would eat counts.  SW_LADDER is a button: 100nF is right.
KNOB_RC = [("MODE_A", "10nF"), ("MODE_B", "10nF"), ("BRIGHT_A", "10nF"),
           ("BRIGHT_B", "10nF"), ("GAIN_A", "10nF"), ("GAIN_B", "10nF"),
           ("SW_LADDER", "100nF")]
for i, (net, cval) in enumerate(KNOB_RC, 1):
    y = (22 + 14 * (i - 1)) * G
    PARTS.append((f"R{i}", "Device:R", 226 * G, y, "10k", FP_R, "",
                  {"1": "P3V3", "2": net}))
    PARTS.append((f"C{i}", "Device:C", 240 * G, y + 6 * G, cval, FP_C, "",
                  {"1": net, "2": "GND"}))

STUB_LEN = 3.81


def prop(name, value, x, y, rot=0, hide=False):
    eff = "\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)\n"
    if hide:
        eff += "\t\t\t\t(hide yes)\n"
    return (f'\t\t(property "{name}" "{value}"\n'
            f"\t\t\t(at {x:g} {y:g} {rot})\n"
            f"\t\t\t(effects\n{eff}\t\t\t)\n\t\t)\n")


out = []
out.append("(kicad_sch\n\t(version 20250114)\n\t(generator \"eeschema\")\n"
           "\t(generator_version \"10.0\")\n"
           f'\t(uuid "{SHEET}")\n\t(paper "A3")\n'
           '\t(title_block\n\t\t(title "Now-Playing carrier board")\n'
           '\t\t(rev "1")\n'
           '\t\t(comment 1 "ESP32-S3 + HUB75 + knobs + mic + Pi UART interconnect")\n'
           '\t\t(comment 2 "Pin map authority: WLED-MM firmware, not the design docs")\n'
           "\t)\n")

out.append("\t(lib_symbols\n")
for blk in LIBS.values():
    out.append("\t\t" + blk.replace("\n", "\n\t") + "\n")
out.append("\t)\n")

# wires + labels + no-connects
for ref, lib_id, px, py, val, fp, mpn, nets in PARTS:
    for num, net in nets.items():
        lx, ly, ang = PINS[lib_id][num]
        sx, sy = px + lx, py - ly       # symbol space (Y up) -> schematic space (Y down)
        dx, dy = STUB[ang]
        if net is NC:
            out.append(f'\t(no_connect\n\t\t(at {sx:g} {sy:g})\n'
                       f'\t\t(uuid "{U(f"nc:{ref}:{num}")}")\n\t)\n')
            continue
        ex, ey = sx + dx * STUB_LEN, sy + dy * STUB_LEN
        out.append(f"\t(wire\n\t\t(pts\n\t\t\t(xy {sx:g} {sy:g})\n\t\t\t(xy {ex:g} {ey:g})\n\t\t)\n"
                   "\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n"
                   f'\t\t(uuid "{U(f"w:{ref}:{num}")}")\n\t)\n')
        lrot = 0 if dx >= 0 else 180
        if dy:
            lrot = 90 if dy < 0 else 270
        just = "left" if (dx > 0 or dy) else "right"
        out.append(f'\t(label "{net}"\n\t\t(at {ex:g} {ey:g} {lrot})\n'
                   "\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n"
                   f"\t\t\t(justify {just} bottom)\n\t\t)\n"
                   f'\t\t(uuid "{U(f"l:{ref}:{num}")}")\n\t)\n')

# symbol instances
for ref, lib_id, px, py, val, fp, mpn, nets in PARTS:
    out.append(f'\t(symbol\n\t\t(lib_id "{lib_id}")\n\t\t(at {px:g} {py:g} 0)\n'
               "\t\t(unit 1)\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n"
               "\t\t(on_board yes)\n\t\t(dnp no)\n"
               f'\t\t(uuid "{U("s:" + ref)}")\n')
    out.append(prop("Reference", ref, px - 12.7, py - 12.7))
    out.append(prop("Value", val, px - 12.7, py - 10.16))
    out.append(prop("Footprint", fp, px, py, hide=True))
    out.append(prop("Datasheet", "", px, py, hide=True))
    out.append(prop("Description", "", px, py, hide=True))
    if mpn:
        out.append(prop("MPN", mpn, px, py, hide=True))
    for num in PINS[lib_id]:
        out.append(f'\t\t(pin "{num}"\n\t\t\t(uuid "{U(f"p:{ref}:{num}")}")\n\t\t)\n')
    out.append(f'\t\t(instances\n\t\t\t(project "{PROJECT}"\n\t\t\t\t(path "/{SHEET}"\n'
               f'\t\t\t\t\t(reference "{ref}")\n\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n')
    out.append("\t)\n")

out.append('\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n')
out.append("\t(embedded_fonts no)\n)\n")

dest = sys.argv[1]
os.makedirs(os.path.dirname(dest), exist_ok=True)
open(dest, "w").write("".join(out))
print("wrote", dest)

# ---- verification table: GPIO -> net -> destination.  Section 6 of the spec asks for
# exactly this, and it is the only thing that catches a dead-GPIO or swapped-pair error.
GPIO_L = {"4": 4, "5": 5, "6": 6, "7": 7, "8": 15, "9": 16, "10": 17, "11": 18, "12": 8,
          "13": 3, "14": 46, "15": 9, "16": 10, "17": 11, "18": 12, "19": 13, "20": 14}
GPIO_R = {"4": 1, "5": 2, "6": 42, "7": 41, "8": 40, "9": 39, "10": 38, "11": 37, "12": 36,
          "13": 35, "14": 0, "15": 45, "16": 48, "17": 47, "18": 21, "19": 20, "20": 19}
FORBIDDEN = {0, 3, 19, 20, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 43, 44, 45, 46}

dest_map = {}
for ref, lib_id, px, py, val, fp, mpn, nets in PARTS:
    if ref.startswith("J_ESP"):
        continue
    for num, net in nets.items():
        if net not in (NC, "GND", "P3V3", "P5V"):
            dest_map.setdefault(net, []).append(f"{ref}:{num}")

print(f"\n{'GPIO':>5}  {'hdr':<12} {'net':<12} destination")
print("-" * 58)
used = []
for hdr, table, nets in (("J1", GPIO_L, ESP_L), ("J3", GPIO_R, ESP_R)):
    for pin, gpio in sorted(table.items(), key=lambda kv: kv[1]):
        net = nets[pin]
        if net is NC:
            continue
        used.append(gpio)
        print(f"{gpio:>5}  {hdr + ' pin ' + pin:<12} {net:<12} "
              f"{', '.join(dest_map.get(net, ['-']))}")

bad = sorted(set(used) & FORBIDDEN)
dupes = sorted({g for g in used if used.count(g) > 1})
print(f"\n{len(used)} GPIOs used, {len(set(used))} distinct")
print(f"forbidden GPIOs routed: {bad if bad else 'NONE'}")
print(f"duplicated GPIOs:       {dupes if dupes else 'NONE'}")
