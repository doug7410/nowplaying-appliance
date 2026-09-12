#!/usr/bin/env python3
"""Generate panelboard.kicad_sch from the netlist, pulling symbols from the stock KiCad libs.

One-off scaffolding: the .kicad_sch it emits is the artifact that lives in git.
UUIDs are uuid5-derived so re-running produces a byte-identical file.

The MCU is a plug-in **ESP32-WROOM-32 38-pin dev board** (classic ESP32, not an S3)
sitting on two 1x19 sockets 22.86mm apart.  USB, RESET/BOOT and the 3.3V regulator
all come with the module, so this board carries none of them.

Pin map authority is the FIRMWARE, not docs:
  WLED-MM/wled00/bus_manager.cpp -- the classic-ESP32 `#else` default branch
  (the ESP32-Trinity / mrcodetastic map).  Wiring to it means the firmware needs
  no pinout patch at all:
    R1=25 G1=26 B1=27 R2=14 G2=12 B2=13
    A=23  B=19  C=5   D=17  E=18  LAT=4 OE=15 CLK=16
  GPIO 6-11 are the SPI flash on classic ESP32 and are left unconnected.

  ponytail: all six RGB pins live in the module's J1 column and all eight control
  pins in J3, which is why the HUB75 fan splits so cleanly across the board.
"""
import uuid, sys, os

NS = uuid.UUID("9a7e1b0a-0000-4000-8000-000000000000")
U = lambda k: str(uuid.uuid5(NS, k))
SHEET = U("sheet:root")
PROJECT = "panelboard"

SYMDIR = "/usr/share/kicad/symbols"


def sexp_block(text, header, start=0):
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


STUB = {0: (-1, 0), 90: (0, 1), 180: (1, 0), 270: (0, -1)}

SYMS = [
    ("Connector", "Conn_01x19_Socket"),
    ("Connector", "Conn_01x02_Pin"),
    ("Connector", "Screw_Terminal_01x02"),
    ("Connector_Generic", "Conn_02x08_Odd_Even"),
    ("Transistor_FET", "Q_PMOS_GDS"),
    ("Device", "R"),
    ("Device", "C"),
    ("Device", "C_Polarized"),
    ("power", "PWR_FLAG"),
]
LIBS = {f"{lib}:{name}": load_symbol(lib, name) for lib, name in SYMS}
PINS = {k: pins_of(v) for k, v in LIBS.items()}

# ---- footprints (every one is stock; no custom library)
FP_SOCK19 = "Connector_PinSocket_2.54mm:PinSocket_1x19_P2.54mm_Vertical"
FP_HDR2 = "Connector_PinHeader_2.54mm:PinHeader_1x02_P2.54mm_Vertical"
FP_SCREW = "Connector_Phoenix_MSTB:PhoenixContact_MSTBVA_2,5_2-G_1x02_P5.00mm_Vertical"
FP_IDC16 = "Connector_IDC:IDC-Header_2x08_P2.54mm_Vertical"
FP_TO252 = "Package_TO_SOT_SMD:TO-252-3_TabPin2"
FP_R = "Resistor_THT:R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal"
FP_C = "Capacitor_THT:C_Disc_D5.0mm_W2.5mm_P5.00mm"
FP_CP = "Capacitor_THT:CP_Radial_D8.0mm_P3.50mm"

G = 1.27
NC = None

# ---- the 38-pin ESP32 dev board, as two 19-pin columns.
# Order is the standard ESP32-DevKitC-32 / NodeMCU-32S 38-pin layout, pin 1 at the
# ANTENNA end of each column.  Comments are the module's own silkscreen labels.
ESP_L = {  # J1 -- the 3V3/EN column.  Carries all six HUB75 RGB pins.
    "1": NC, "2": NC,                              # 3V3, EN -- module regulates itself
    "3": NC, "4": NC,                              # VP/IO36, VN/IO39 (input-only)
    "5": NC, "6": NC,                              # IO34, IO35 (input-only)
    "7": NC, "8": NC,                              # IO32, IO33
    "9": "HUB_R1", "10": "HUB_G1", "11": "HUB_B1", # IO25, IO26, IO27
    "12": "HUB_R2", "13": "HUB_G2",                # IO14, IO12
    "14": "GND",
    "15": "HUB_B2",                                # IO13
    "16": NC, "17": NC, "18": NC,                  # SD2/IO9, SD3/IO10, CMD/IO11 -- flash
    "19": "ESP_5V",                                # 5V, fed through JP1
}
ESP_R = {  # J3 -- the GND/IO23 column.  Carries all eight HUB75 control pins.
    "1": "GND",
    "2": "HUB_A",                                  # IO23
    "3": NC, "4": NC, "5": NC, "6": NC,            # IO22, TX0/IO1, RX0/IO3, IO21
    "7": "GND",
    "8": "HUB_B", "9": "HUB_E", "10": "HUB_C",     # IO19, IO18, IO5
    "11": "HUB_D", "12": "HUB_CLK", "13": "HUB_LAT",  # IO17, IO16, IO4
    "14": NC, "15": NC,                            # IO0 (boot strap), IO2
    "16": "HUB_OE",                                # IO15
    "17": NC, "18": NC, "19": NC,                  # SD1/IO8, SD0/IO7, CLK/IO6 -- flash
}
HUB = {
    "1": "HUB_R1", "2": "HUB_G1", "3": "HUB_B1", "4": "GND",
    "5": "HUB_R2", "6": "HUB_G2", "7": "HUB_B2", "8": "HUB_E",
    "9": "HUB_A", "10": "HUB_B", "11": "HUB_C", "12": "HUB_D",
    "13": "HUB_CLK", "14": "HUB_LAT", "15": "HUB_OE", "16": "GND",
}

# ref, lib_id, x, y, value, footprint, mpn, {pin: net}
PARTS = [
    # ---- power: screw in -> reverse-polarity PMOS -> P5V rail -> panel screw out
    ("J_PWR", "Connector:Screw_Terminal_01x02", 24 * G, 24 * G, "DC IN 5V", FP_SCREW, "",
     {"1": "VIN", "2": "GND"}),
    # Drain on the INPUT side: the body diode must point the way current normally flows.
    ("Q1", "Transistor_FET:Q_PMOS_GDS", 52 * G, 26 * G, "AOD403", FP_TO252, "AOD403",
     {"1": "QG", "2": "VIN", "3": "P5V"}),
    ("RG", "Device:R", 44 * G, 36 * G, "100k", FP_R, "",
     {"1": "QG", "2": "GND"}),
    ("CB1", "Device:C_Polarized", 68 * G, 32 * G, "470uF/16V", FP_CP, "",
     {"1": "P5V", "2": "GND"}),
    ("CB2", "Device:C", 78 * G, 32 * G, "100nF", FP_C, "",
     {"1": "P5V", "2": "GND"}),
    ("J_PANEL", "Connector:Screw_Terminal_01x02", 24 * G, 52 * G, "PANEL 5V", FP_SCREW, "",
     {"1": "P5V", "2": "GND"}),
    # A dev board's 5V pin has no reverse-current protection: leave it tied to a live
    # supply with a USB cable also plugged in and the PSU back-feeds the host port.
    # Pull the shunt before plugging USB in.
    ("JP1", "Connector:Conn_01x02_Pin", 92 * G, 52 * G, "ESP 5V", FP_HDR2, "",
     {"1": "ESP_5V", "2": "P5V"}),
    # IO12 (G2) is the MTDI strapping pin -- it must read LOW at reset or the chip
    # comes up expecting 1.8V flash.  The panel's ribbon is the only thing on that
    # net, so hold it down here rather than trusting the internal pull-down.
    ("RPD", "Device:R", 110 * G, 52 * G, "10k", FP_R, "",
     {"1": "HUB_G2", "2": "GND"}),

    # ---- the dev board, as two sockets
    ("J_ESP_L", "Connector:Conn_01x19_Socket", 160 * G, 60 * G, "ESP32 J1",
     FP_SOCK19, "", ESP_L),
    ("J_ESP_R", "Connector:Conn_01x19_Socket", 220 * G, 60 * G, "ESP32 J3",
     FP_SOCK19, "", ESP_R),

    # ---- HUB75 out
    ("J_HUB", "Connector_Generic:Conn_02x08_Odd_Even", 280 * G, 30 * G, "HUB75",
     FP_IDC16, "", HUB),

    # ---- ERC drivers for rails no power-output pin reaches
    ("PF1", "power:PWR_FLAG", 24 * G, 70 * G, "PWR_FLAG", "", "", {"1": "P5V"}),
    ("PF2", "power:PWR_FLAG", 34 * G, 70 * G, "PWR_FLAG", "", "", {"1": "GND"}),
    ("PF3", "power:PWR_FLAG", 44 * G, 70 * G, "PWR_FLAG", "", "", {"1": "VIN"}),
]

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
           '\t(title_block\n\t\t(title "panelboard")\n'
           '\t\t(rev "2")\n'
           '\t\t(comment 1 "ESP32-WROOM-32 38-pin dev board + HUB75 out + 5V screw in/out")\n'
           '\t\t(comment 2 "HUB75 pin map: WLED-MM classic-ESP32 default (firmware is the authority)")\n'
           "\t)\n")

out.append("\t(lib_symbols\n")
for blk in LIBS.values():
    out.append("\t\t" + blk.replace("\n", "\n\t") + "\n")
out.append("\t)\n")

# wires + labels + no-connects (dedupe stacked pins)
seen = set()
for ref, lib_id, px, py, val, fp, mpn, nets in PARTS:
    for num, net in nets.items():
        lx, ly, ang = PINS[lib_id][num]
        sx, sy = px + lx, py - ly
        if (ref, sx, sy) in seen:
            continue
        seen.add((ref, sx, sy))
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

for ref, lib_id, px, py, val, fp, mpn, nets in PARTS:
    flag = ref.startswith("PF")
    yn = "no" if flag else "yes"
    out.append(f'\t(symbol\n\t\t(lib_id "{lib_id}")\n\t\t(at {px:g} {py:g} 0)\n'
               "\t\t(unit 1)\n\t\t(exclude_from_sim no)\n"
               f"\t\t(in_bom {yn})\n\t\t(on_board {yn})\n\t\t(dnp no)\n"
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
open(dest, "w").write("".join(out))
print("wrote", dest)

# ---- verification table: net -> every pin it lands on
dest_map = {}
for ref, lib_id, px, py, val, fp, mpn, nets in PARTS:
    for num, net in nets.items():
        if net not in (NC,):
            dest_map.setdefault(net, []).append(f"{ref}:{num}")
for net in sorted(dest_map):
    ends = dest_map[net]
    tag = "  <-- SINGLE PIN" if len([e for e in ends if not e.startswith("PF")]) < 2 else ""
    print(f"{net:12} {', '.join(ends)}{tag}")
