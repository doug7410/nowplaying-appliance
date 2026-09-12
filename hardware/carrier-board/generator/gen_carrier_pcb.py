#!/usr/bin/env python3
"""Build carrier-board.kicad_pcb: outline, placement, routing, bottom GND pour.

One-off scaffolding; the .kicad_pcb it emits is the artifact that lives in git.

Board 140 x 100 mm, rect (10,10)-(150,110).  The ESP32-S3-DevKitC-1 sits in the middle
on two 1x22 sockets 22.86mm apart (Espressif's dimension drawing: 25.40mm board, rows
inset 1.27mm each side).  Placement follows the pin map: 12 of 13 HUB75 signals are on
J1 so the panel connector goes LEFT; the knobs and mic are on J3 so they go RIGHT.
The Pi is left too -- its UART is on J1 and its 5V comes from the power block.

Run with:  python3 gen_carrier_pcb.py <dest.kicad_pcb> [--dump]
"""
import sys, pcbnew
from pcbnew import VECTOR2I, FromMM as MM

FPDIR = "/usr/share/kicad/footprints"

BOARD = (10.0, 10.0, 150.0, 110.0)
TRACK_W, CLEAR = 0.30, 0.25
# 1oz outer copper.  IPC-2221 external: 6A at a 10C rise needs 3.56mm on 1oz
# (it was 1.78mm on 2oz, which is why this used to be 3.0).  4mm gives 6.5A @10C
# / 8.9A @20C -- the same ~2x margin over the 6A HUB75 full-white budget that the
# 2oz spin had.  Widening this is the whole point of the 1oz respin: do not drop
# it back to 3.0 without also going back to 2oz copper.
PWR_W = 4.0
SILK_W = 0.16   # JLC's minimum printable stroke is 0.153mm (6 mil), NOT 0.15

# ESP32 module: J1 (left) / J3 (right), pin 1 at the antenna end, both rows aligned.
ESP_X, ESP_Y = 80.0, 30.0           # centre-x of the module, y of pin 1
ROW = 22.86                          # 0.9" -- genuine DevKitC-1.  YD clone is 25.4 and won't fit.

NETS = {}


def net(board, name):
    # local labels on the root sheet come out of eeschema as "/NAME"; match that exactly
    # or every pad trips a net_conflict parity warning.
    if not name.startswith(("/", "unconnected-")):
        name = "/" + name
    if name not in NETS:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        NETS[name] = n
    return NETS[name]


def track(board, netname, layer, *pts, width=None):
    """Polyline of tracks through pts (mm tuples)."""
    n = net(board, netname)
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(VECTOR2I(MM(a[0]), MM(a[1])))
        t.SetEnd(VECTOR2I(MM(b[0]), MM(b[1])))
        t.SetWidth(MM(width or TRACK_W))
        t.SetLayer(layer)
        t.SetNet(n)
        board.Add(t)


def via(board, netname, x, y):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(VECTOR2I(MM(x), MM(y)))
    v.SetDrill(MM(0.4))
    v.SetWidth(MM(0.8))
    v.SetNet(net(board, netname))
    board.Add(v)


def route(board):
    T, B = pcbnew.F_Cu, pcbnew.B_Cu

    # -- power block (6 A path at PWR_W) --
    # VIN_RAW dips south of the connector row to clear J_PWR.2 (GND).  y=104.5,
    # not 104: at 4mm the dip reaches 2mm each side of centre and y=104 leaves
    # only 0.20mm to the J_PWR.2 pad (which starts at 101.80).
    track(board, "VIN_RAW", T, (20, 100), (20, 104.5), (40, 104.5), (40, 100),
          width=PWR_W)
    # P5V trunk: Q1.3 south, east along y=95, north into J_PANEL; CB branches.
    # y=95, not 93: that centres the trunk in the 7.3mm corridor between the Q1
    # tab (ends 90.90) and the connector pad row (starts 98.20).  At 4mm the
    # trunk can no longer share that corridor with the VIN_SW crossing, so the
    # crossing moved wholly to the bottom layer -- see below.
    # 2.0 mm exit stub so the end cap clears Q1 pin 2 (2.28 mm pitch).
    track(board, "P5V", T, (32.96, 90.28), (32.96, 91.8), width=2.0)
    track(board, "P5V", T, (32.96, 91.8), (32.96, 95), (60, 95), (60, 100),
          width=PWR_W)
    track(board, "P5V", T, (50, 95), (50, 88), width=PWR_W)
    track(board, "P5V", T, (58, 95), (58, 88), width=PWR_W)
    # JP pin-1 rail fed around the west side (clears RG at x=20).  Carries the
    # ESP leg + both Pi legs, ~3.5A: 2.5mm is 4.6A @10C on 1oz, and the header
    # run east of JP1 has already shed the ESP leg so 2.0mm (3.9A) covers it.
    track(board, "P5V", T, (32.96, 95), (17, 95), (17, 74), (20, 74), width=2.5)
    track(board, "P5V", T, (20, 74), (44, 74), width=2.0)
    # VIN_SW: J_SW.2 is THT, so the crossing drops to the bottom layer *at the
    # pad* and runs the whole way under the P5V trunk -- one via farm at the
    # tab end instead of the two the old top-bottom-top hop needed.  Bottom is
    # clear here because Q1 is SMD, so this strap gets the full PWR_W.
    # 4 vias x ~2.2A (0.4mm drill, 25um barrel, IPC-2221) = 8.8A into the tab.
    track(board, "VIN_SW", B, (45, 100), (45, 85.5), width=PWR_W)
    for y in (85.5, 87.0, 88.5, 90.0):
        via(board, "VIN_SW", 45, y)
    track(board, "VIN_SW", T, (45, 85.5), (45, 90), width=2.0)
    track(board, "VIN_SW", T, (45, 88), (40, 88), width=3.0)     # via farm → tab
    track(board, "VIN_SW", T, (32.96, 88), (37, 88), width=1.0)  # pin 2 → tab strap
    track(board, "GATE", T, (32.96, 85.72), (22.3, 85.72), (20, 88))
    track(board, "ESP_5V", T, (20, 76.54), (22, 79), (24, 81), (62, 81),
          (68.57, 80.80), width=1.0)
    # -- HUB75 staircase: nested Ls, one lane per net; left-column pads entered
    # at mid-row height (slip between right pads) + short diagonal --
    # (net, esp_y, lane_x, target_y, hub_pad)
    # OE/LAT pins sit south of the JP rail (y=74): jog north at x 46.5/48 first
    track(board, "HUB_OE", T, (68.57, 78.26), (46.5, 78.26), (46.5, 72.5),
          (33.5, 72.5), (33.5, 41.2), (25.4, 41.2), (24, 39.78))
    track(board, "HUB_LAT", T, (68.57, 75.72), (48, 75.72), (48, 71.3),
          (35.5, 71.3), (35.5, 37.24), (26.54, 37.24))
    HUB_STAIR = [
        ("HUB_D",   70.64, 37.5, 34.70, (26.54, 34.70)),
        ("HUB_C",   68.10, 39.5, 33.43, (24.00, 34.70)),
        ("HUB_B",   65.56, 41.5, 32.16, (26.54, 32.16)),
        ("HUB_A",   57.94, 43.5, 30.89, (24.00, 32.16)),
        ("HUB_B2",  50.32, 45.5, 28.35, (24.00, 29.62)),
        ("HUB_G2",  47.78, 47.5, 27.08, (26.54, 27.08)),
        ("HUB_R2",  45.24, 49.5, 25.81, (24.00, 27.08)),
        ("HUB_B1",  42.70, 51.5, 23.27, (24.00, 24.54)),
        ("HUB_G1",  40.16, 53.5, 22.00, (26.54, 22.00)),
        ("HUB_R1",  37.62, 55.5, 20.60, (24.00, 22.00)),
    ]
    for name, ey, lx, ty, (px, py) in HUB_STAIR:
        pts = [(68.57, ey), (lx, ey), (lx, ty)]
        if ty == py:
            pts.append((px, py))
        else:
            pts += [(25.4, ty), (px, py)]
        track(board, name, T, *pts)
    # UART to the Pi on bottom (THT both ends; the staircase owns the top here)
    track(board, "UART_RX", B, (68.57, 52.86), (29, 52.86), (27.5, 54.5),
          (27.5, 62))
    track(board, "UART_TX", B, (68.57, 55.40), (31.5, 55.40), (30, 57),
          (30, 62))
    # -- P3V3: over the antenna end, then the resistor bus down x=98;
    # jog west at y 78-88 around C3V3.2 (GND, sits on the bus line) --
    track(board, "P3V3", T, (68.57, 32.54), (68.57, 26), (98, 26))
    track(board, "P3V3", T, (98, 26), (98, 78), (96, 80), (96, 86), (98, 88))
    track(board, "P3V3", T, (98, 88), (136, 88), (136, 84))
    # -- HUB_CLK on bottom: west under the module, slip the L-header between
    # pins 9/10, down the west strip into J_HUB.13 --
    track(board, "HUB_CLK", B, (91.43, 73.18), (70.5, 73.18), (70.5, 51.59),
          (22.5, 51.59), (22.5, 37.24), (24, 37.24))
    # -- knob ladder: R→C→connector on top (both orders monotone) --
    KNOB_ROWS = [
        ("MODE_A", 26, 122.0), ("MODE_B", 34, 124.5), ("BRIGHT_A", 42, 127.0),
        ("BRIGHT_B", 50, 129.5), ("GAIN_A", 58, 132.0), ("GAIN_B", 66, 134.5),
        ("SW_LADDER", 74, 137.0),
    ]
    for name, ry, px in KNOB_ROWS:
        track(board, name, T, (105.62, ry), (112, ry), (px, ry), (px, 18))
    # ESP→ladder ties. MODE pair goes west of the R-header on bottom (MODE_B
    # slips header pins 1/2); GAIN/SW take a top lane between header and bus,
    # then one via and east on bottom under the P3V3 bus.
    track(board, "MODE_A", B, (91.43, 70.64), (89, 68.5), (89, 23.5),
          (104, 23.5), (105.62, 26))
    track(board, "MODE_B", B, (91.43, 68.10), (90, 66.3), (90, 31.27),
          (104, 31.27), (105.62, 34))
    track(board, "BRIGHT_A", B, (91.43, 37.62), (102, 37.62), (104, 39.5),
          (105.62, 42))
    for name, ex, ey, vy, ry in [("GAIN_A", 94.3, 45.24, 56.5, 58),
                                 ("GAIN_B", 95.4, 42.70, 63.5, 66),
                                 ("SW_LADDER", 96.5, 40.16, 71.5, 74)]:
        track(board, name, T, (91.43, ey), (ex, ey), (ex, vy))
        via(board, name, ex, vy)
        track(board, name, B, (ex, vy), (104, vy), (105.62, ry))
    # BRIGHT_B: from ESP_L.18 slip the R-header pins 18/19, wrap the SE on
    # bottom, one via to hop the mic runs into row 50 from the east
    track(board, "BRIGHT_B", B, (68.57, 73.18), (70, 74.45), (92.5, 74.45),
          (94, 72.9), (94, 68.5), (107, 68.5), (107, 57.0))
    via(board, "BRIGHT_B", 107, 57.0)
    track(board, "BRIGHT_B", T, (107, 57.0), (107, 50), (105.62, 50))
    # -- mic: bottom, nested Ls (north pin → east mic pad, so SD outermost) --
    track(board, "MIC_SD", B, (91.43, 47.78), (131, 47.78), (131, 84))
    track(board, "MIC_WS", B, (91.43, 50.32), (94.5, 54.5), (128.5, 54.5),
          (128.5, 84))
    track(board, "MIC_SCK", B, (91.43, 52.86), (93.5, 55.5), (126, 55.5),
          (126, 84))
    # GND stitches: C5.2 is pour-islanded by the mic runs; J_PI.3 starves its
    # thermal spokes. Top strap + via into the main pour for each.
    track(board, "GND", T, (112, 53), (114, 56.5))
    via(board, "GND", 114, 56.5)
    # C5.2 sits in a pour-free pocket: it connects via the strap above, so no
    # thermal spokes are possible — declare that instead of tripping DRC
    for fp in board.GetFootprints():
        if fp.GetReference() == "C5":
            for p in fp.Pads():
                if p.GetNumber() == "2":
                    p.SetLocalZoneConnection(pcbnew.ZONE_CONNECTION_NONE)
    track(board, "GND", T, (25, 62), (25, 59))
    via(board, "GND", 25, 59)
    # PI 5V pair on bottom (THT pads both sides; dodges the JP rail + J_PI row)
    track(board, "PI_5V_A", B, (32, 76.54), (26, 72), (20, 68), (20, 62),
          width=1.0)
    track(board, "PI_5V_B", B, (44, 76.54), (46, 75), (46, 66), (31, 64.5),
          (23.5, 64.5), (22.5, 63.5), (22.5, 62), width=1.0)


def place(board, lib, fpname, ref, value, x, y, rot, netmap, mpn="", ref_at=None, bottom=False):
    fp = pcbnew.FootprintLoad(f"{FPDIR}/{lib}.pretty", fpname)
    board.Add(fp)
    fp.SetFPID(pcbnew.LIB_ID(lib, fpname))
    fp.SetReference(ref)
    fp.SetValue(value)
    if mpn:
        fp.SetField("MPN", mpn)
        for f in fp.GetFields():   # SetField defaults to VISIBLE on silkscreen at (0,0)
            if f.GetName() == "MPN":
                f.SetVisible(False)
                f.SetLayer(pcbnew.B_Fab if bottom else pcbnew.F_Fab)
    if bottom:
        fp.Flip(VECTOR2I(MM(x), MM(y)), False)
    fp.SetPosition(VECTOR2I(MM(x), MM(y)))
    fp.SetOrientationDegrees(rot)
    for pad in fp.Pads():
        nm = netmap.get(pad.GetNumber())
        if nm:
            pad.SetNet(net(board, nm))
    for it in fp.GraphicalItems():   # stock silk is 0.12mm -> below JLC's 0.153 minimum
        if it.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            if hasattr(it, "SetTextThickness"):
                it.SetTextThickness(MM(SILK_W))
            elif hasattr(it, "SetWidth"):
                it.SetWidth(MM(SILK_W))
    r = fp.Reference()
    r.SetLayer(pcbnew.B_SilkS if bottom else pcbnew.F_SilkS)
    r.SetTextSize(VECTOR2I(MM(1.0), MM(1.0)))
    r.SetTextThickness(MM(SILK_W))
    if ref_at:
        r.SetPosition(VECTOR2I(MM(ref_at[0]), MM(ref_at[1])))
        r.SetTextAngleDegrees(0)      # inherited rotation drags tall text over pads
    fp.Value().SetVisible(False)
    return fp


# ---- netmaps, lifted verbatim from the schematic generator so the two cannot drift
ESP_L = {"1": "P3V3", "2": "P3V3", "4": "HUB_R1", "5": "HUB_G1", "6": "HUB_B1",
         "7": "HUB_R2", "8": "HUB_G2", "9": "HUB_B2", "10": "UART_RX", "11": "UART_TX",
         "12": "HUB_A", "15": "HUB_B", "16": "HUB_C", "17": "HUB_D", "18": "BRIGHT_B",
         "19": "HUB_LAT", "20": "HUB_OE", "21": "ESP_5V", "22": "GND"}
ESP_R = {"1": "GND", "4": "BRIGHT_A", "5": "SW_LADDER", "6": "GAIN_B", "7": "GAIN_A",
         "8": "MIC_SD", "9": "MIC_WS", "10": "MIC_SCK", "16": "MODE_B", "17": "MODE_A",
         "18": "HUB_CLK", "21": "GND", "22": "GND"}
HUB = {"1": "HUB_R1", "2": "HUB_G1", "3": "HUB_B1", "4": "GND",
       "5": "HUB_R2", "6": "HUB_G2", "7": "HUB_B2", "8": "GND",
       "9": "HUB_A", "10": "HUB_B", "11": "HUB_C", "12": "HUB_D",
       "13": "HUB_CLK", "14": "HUB_LAT", "15": "HUB_OE", "16": "GND"}
KNOBS = {"1": "MODE_A", "2": "MODE_B", "3": "BRIGHT_A", "4": "BRIGHT_B",
         "5": "GAIN_A", "6": "GAIN_B", "7": "SW_LADDER", "8": "GND"}
MIC = {"1": "MIC_SCK", "2": "MIC_WS", "3": "MIC_SD", "4": "GND", "5": "P3V3", "6": "GND"}
PI = {"1": "PI_5V_A", "2": "PI_5V_B", "3": "GND", "4": "UART_RX", "5": "UART_TX"}

# Pins with no net in the schematic still need their auto-generated name here.
for _ref, _m in (("J_ESP_L", ESP_L), ("J_ESP_R", ESP_R)):
    for _n in range(1, 23):
        _m.setdefault(str(_n), f"unconnected-({_ref}-Pin_{_n}-Pad{_n})")

KNOB_RC = [("MODE_A", "10nF"), ("MODE_B", "10nF"), ("BRIGHT_A", "10nF"),
           ("BRIGHT_B", "10nF"), ("GAIN_A", "10nF"), ("GAIN_B", "10nF"),
           ("SW_LADDER", "100nF")]


def main(dest):
    board = pcbnew.NewBoard(dest)
    board.GetDesignSettings().SetCopperLayerCount(2)

    x1, y1, x2, y2 = BOARD
    for a, b in [((x1, y1), (x2, y1)), ((x2, y1), (x2, y2)),
                 ((x2, y2), (x1, y2)), ((x1, y2), (x1, y1))]:
        s = pcbnew.PCB_SHAPE(board)
        s.SetShape(pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(VECTOR2I(MM(a[0]), MM(a[1])))
        s.SetEnd(VECTOR2I(MM(b[0]), MM(b[1])))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1))
        board.Add(s)

    # ---- the module, dead centre; everything else fans out from it
    place(board, "Connector_PinSocket_2.54mm", "PinSocket_1x22_P2.54mm_Vertical",
          "J_ESP_L", "ESP32-S3 J1", ESP_X - ROW / 2, ESP_Y, 0, ESP_L, ref_at=(ESP_X - ROW / 2, 26.0))
    place(board, "Connector_PinSocket_2.54mm", "PinSocket_1x22_P2.54mm_Vertical",
          "J_ESP_R", "ESP32-S3 J3", ESP_X + ROW / 2, ESP_Y, 0, ESP_R, ref_at=(ESP_X + ROW / 2, 26.0))

    # ---- LEFT: HUB75 (12 of its 13 signals are on J1), Pi, power
    place(board, "Connector_IDC", "IDC-Header_2x08_P2.54mm_Latch_Vertical",
          "J_HUB", "HUB75", 24.0, 22.0, 0, HUB, ref_at=(24.0, 16.0))
    place(board, "Connector_JST", "JST_XH_B5B-XH-A_1x05_P2.50mm_Vertical",
          "J_PI", "PI 2/4/6/8/10", 20.0, 62.0, 0, PI, "B5B-XH-A(LF)(SN)", ref_at=(20.0, 57.0))

    place(board, "Connector_Phoenix_MSTB", "PhoenixContact_MSTBVA_2,5_2-G_1x02_P5.00mm_Vertical",
          "J_PWR", "DC IN 5V", 20.0, 100.0, 0, {"1": "VIN_RAW", "2": "GND"}, ref_at=(20.0, 93.0))
    place(board, "Connector_Phoenix_MSTB", "PhoenixContact_MSTBVA_2,5_2-G_1x02_P5.00mm_Vertical",
          "J_SW", "MASTER SW", 40.0, 100.0, 0, {"1": "VIN_RAW", "2": "VIN_SW"}, ref_at=(40.0, 93.0))
    place(board, "Connector_Phoenix_MSTB", "PhoenixContact_MSTBVA_2,5_2-G_1x02_P5.00mm_Vertical",
          "J_PANEL", "PANEL 5V", 60.0, 100.0, 0, {"1": "P5V", "2": "GND"}, ref_at=(60.0, 93.0))
    # Drain (= the TO-252 tab, pin 2) on the INPUT side: the body diode has to point the
    # way current normally flows, or a reversed supply conducts straight through it.
    place(board, "Package_TO_SOT_SMD", "TO-252-3_TabPin2", "Q1", "AOD403",
          38.0, 88.0, 0, {"1": "GATE", "2": "VIN_SW", "3": "P5V"}, "AOD403", ref_at=(38.0, 81.0))
    place(board, "Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal",
          "RG", "100k", 20.0, 88.0, 0, {"1": "GATE", "2": "GND"}, ref_at=(20.0, 83.0))
    place(board, "Capacitor_THT", "CP_Radial_D8.0mm_P3.50mm", "CB1", "470uF/10V",
          50.0, 88.0, 90, {"1": "P5V", "2": "GND"}, ref_at=(50.0, 79.0))
    place(board, "Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm", "CB2", "100nF",
          58.0, 88.0, 90, {"1": "P5V", "2": "GND"}, ref_at=(58.0, 79.0))
    # Each Pi 5V leg gets its own shunt: <=1.5A each, inside a 2.54mm shunt's rating.
    for ref, val, nn, px in [("JP1", "ESP 5V", "ESP_5V", 20.0),
                             ("JP2", "PI 5V A", "PI_5V_A", 32.0),
                             ("JP3", "PI 5V B", "PI_5V_B", 44.0)]:
        place(board, "Connector_PinHeader_2.54mm", "PinHeader_1x02_P2.54mm_Vertical",
              ref, val, px, 74.0, 0, {"1": "P5V", "2": nn}, ref_at=(px, 70.0))

    # ---- RIGHT: knob filtering, then the knob/mic connectors
    for i, (nn, cval) in enumerate(KNOB_RC, 1):
        y = 26.0 + 8.0 * (i - 1)
        place(board, "Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal",
              f"R{i}", "10k", 98.0, y, 0, {"1": "P3V3", "2": nn}, ref_at=(98.0, y - 3.0))
        place(board, "Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm",
              f"C{i}", cval, 112.0, y, 90, {"1": nn, "2": "GND"}, ref_at=(117.5, y))
    place(board, "Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm", "C3V3", "100nF",
          98.0, 88.0, 90, {"1": "P3V3", "2": "GND"}, ref_at=(102.0, 88.0))
    place(board, "Connector_JST", "JST_XH_B8B-XH-A_1x08_P2.50mm_Vertical",
          "J_KNOBS", "KNOBS", 122.0, 18.0, 0, KNOBS, "B8B-XH-A(LF)(SN)", ref_at=(130.0, 13.0))
    place(board, "Connector_JST", "JST_XH_B6B-XH-A_1x06_P2.50mm_Vertical",
          "J_MIC", "MIC", 126.0, 84.0, 0, MIC, "B6B-XH-A(LF)(SN)", ref_at=(132.0, 79.0))

    route(board)

    zone = pcbnew.ZONE(board)
    zone.SetLayer(pcbnew.B_Cu)
    zone.SetNet(net(board, "GND"))
    zone.SetAssignedPriority(0)
    poly = zone.Outline()
    poly.NewOutline()
    e = 0.5  # board-edge clearance; the fill does not pull itself back
    for px, py in [(x1 + e, y1 + e), (x2 - e, y1 + e), (x2 - e, y2 - e),
                   (x1 + e, y2 - e)]:
        poly.Append(MM(px), MM(py))
    zone.SetIsFilled(False)
    board.Add(zone)
    board.BuildConnectivity()
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
    board.Save(dest)
    return board


if __name__ == "__main__":
    b = main(sys.argv[1])
    if "--dump" in sys.argv:
        for fp in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
            for p in fp.Pads():
                pos = p.GetPosition()
                print(f"{fp.GetReference():9s} {p.GetNumber():3s} "
                      f"({pcbnew.ToMM(pos.x):7.2f},{pcbnew.ToMM(pos.y):7.2f}) "
                      f"{p.GetNetname()}")
