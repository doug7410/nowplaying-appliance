#!/usr/bin/env python3
"""Build panelboard.kicad_pcb: outline, placement, routing, bottom GND pour.

One-off scaffolding; the .kicad_pcb it emits is the artifact that lives in git.

Layout (70 x 80 mm).  The MCU is a plug-in ESP32-WROOM-32 38-pin dev board on two
1x19 sockets, 22.86mm apart, running north-south with the USB end at the SOUTH:

  - J1 (west socket) carries all six HUB75 RGB pins, J3 (east) all eight control
    pins.  So the HUB75 IDC goes WEST: the RGB half fans straight across, and the
    control half crosses under the module on B.Cu.
  - Crossing under the module is free here.  A socketed module leaves the whole
    22.86mm inter-row corridor empty on both layers -- the one real advantage a
    dev board has over a soldered-down WROOM.
  - Power along the south edge: screw in -> AOD403 reverse-polarity PMOS -> P5V
    -> panel screw out, plus JP1 in series with the module's 5V pin.

Run:  python3 gen_pcb.py <dest.kicad_pcb> [--dump]
"""
import sys, pcbnew
from pcbnew import VECTOR2I, FromMM as MM

FPDIR = "/usr/share/kicad/footprints"

BOARD = (10.0, 18.0, 80.0, 90.0)
TRACK_W, CLEAR = 0.3, 0.2
PWR_W = 2.5          # 4A on 1oz outer copper ~1.8mm at 10C rise; 2.5 has margin
SILK_W = 0.16        # JLC minimum printable stroke is 0.153, NOT 0.15

# ---- the dev board.  22.86mm (0.9") row spacing -- measured on the board in hand.
# A 1.0" clone will NOT seat: that is one 0.1" grid step wider.
ROW = 22.86
ESP_X = 45.0         # centre-x of the module
ESP_Y = 21.0         # y of pin 1 (both columns), the ANTENNA end
PITCH = 2.54

NETS = {}
board = None


def esp_y(n):
    """y of pin `n` of either socket."""
    return ESP_Y + (n - 1) * PITCH


def net(name):
    if not name.startswith(("/", "unconnected-")):
        name = "/" + name
    if name not in NETS:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        NETS[name] = n
    return NETS[name]


def place(lib, fpname, ref, value, x, y, rot, netmap, mpn="", ref_at=None):
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
                f.SetLayer(pcbnew.F_Fab)
    fp.SetPosition(VECTOR2I(MM(x), MM(y)))
    fp.SetOrientationDegrees(rot)
    for pad in fp.Pads():
        if pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH:
            pad.SetLocalClearance(MM(0.3))
    for pad in fp.Pads():
        nm = netmap.get(pad.GetNumber())
        if nm:
            pad.SetNet(net(nm))
    for it in fp.GraphicalItems():   # stock silk 0.12mm is below JLC's 0.153 floor
        if it.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            if hasattr(it, "SetTextThickness"):
                it.SetTextThickness(MM(SILK_W))
            elif hasattr(it, "SetWidth"):
                it.SetWidth(MM(SILK_W))
    r = fp.Reference()
    r.SetTextSize(VECTOR2I(MM(1.0), MM(1.0)))
    r.SetTextThickness(MM(SILK_W))
    if ref_at:
        r.SetPosition(VECTOR2I(MM(ref_at[0]), MM(ref_at[1])))
        r.SetTextAngleDegrees(0)
    fp.Value().SetVisible(False)
    return fp


def P(ref, num):
    """Centre (x, y) in mm of pad `num` of footprint `ref`."""
    for fp in board.GetFootprints():
        if fp.GetReference() == ref:
            for pad in fp.Pads():
                if pad.GetNumber() == str(num):
                    pos = pad.GetPosition()
                    return (pcbnew.ToMM(pos.x), pcbnew.ToMM(pos.y))
    raise KeyError(f"{ref}:{num}")


def track(pts, netname, layer=pcbnew.F_Cu, width=TRACK_W):
    for a, b in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(VECTOR2I(MM(a[0]), MM(a[1])))
        t.SetEnd(VECTOR2I(MM(b[0]), MM(b[1])))
        t.SetLayer(layer)
        t.SetWidth(MM(width))
        t.SetNet(net(netname))
        board.Add(t)


def via(x, y, netname, drill=0.4, size=0.8):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(VECTOR2I(MM(x), MM(y)))
    v.SetDrill(MM(drill))
    v.SetWidth(MM(size))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu)
    v.SetNet(net(netname))
    board.Add(v)


def pads(ref, num):
    """Every copy of pad `num` -- the TO-252 tab repeats one."""
    for fp in board.GetFootprints():
        if fp.GetReference() == ref:
            return [(pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y))
                    for p in fp.Pads() if p.GetNumber() == str(num)]
    raise KeyError(ref)


def bond(ref, num, netname, width=TRACK_W):
    """Join the repeated copies of one pad -- they share a net but no copper."""
    track(pads(ref, num), netname, width=width)


def gvia(ref, num, dx=0.0, dy=1.2):
    """GND via next to an SMT pad + stub to it."""
    x, y = P(ref, num)
    track([(x, y), (x + dx, y + dy)], "GND")
    via(x + dx, y + dy, "GND")


# ---- netmaps, lifted verbatim from gen_sch.py so the two cannot drift
ESP_L = {
    "9": "HUB_R1", "10": "HUB_G1", "11": "HUB_B1",
    "12": "HUB_R2", "13": "HUB_G2", "14": "GND", "15": "HUB_B2",
    "19": "ESP_5V",
}
ESP_R = {
    "1": "GND", "2": "HUB_A", "7": "GND",
    "8": "HUB_B", "9": "HUB_E", "10": "HUB_C",
    "11": "HUB_D", "12": "HUB_CLK", "13": "HUB_LAT", "16": "HUB_OE",
}
# every unconnected socket pin still needs its own phantom net or DRC parity
# reports it as a mismatch against the schematic's no-connects
ESP_L_NC = {"1": "3V3", "2": "EN", "3": "IO36", "4": "IO39", "5": "IO34",
            "6": "IO35", "7": "IO32", "8": "IO33",
            "16": "IO9", "17": "IO10", "18": "IO11"}
ESP_R_NC = {"3": "IO22", "4": "TXD0", "5": "RXD0", "6": "IO21",
            "14": "IO0", "15": "IO2", "17": "IO8", "18": "IO7", "19": "IO6"}
for _n, _io in ESP_L_NC.items():
    ESP_L[_n] = f"unconnected-(J_ESP_L-Pin_{_n}-Pad{_n})"
for _n, _io in ESP_R_NC.items():
    ESP_R[_n] = f"unconnected-(J_ESP_R-Pin_{_n}-Pad{_n})"

HUB = {
    "1": "HUB_R1", "2": "HUB_G1", "3": "HUB_B1", "4": "GND",
    "5": "HUB_R2", "6": "HUB_G2", "7": "HUB_B2", "8": "HUB_E",
    "9": "HUB_A", "10": "HUB_B", "11": "HUB_C", "12": "HUB_D",
    "13": "HUB_CLK", "14": "HUB_LAT", "15": "HUB_OE", "16": "GND",
}


def main(dest, dump=False):
    global board
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

    # ---- the module: two sockets, pin 1 north (the antenna end).  The board
    # stops at y=18 so the module's antenna end overhangs into free air: with a
    # socketed module that is cheaper and better than a copper keepout.
    place("Connector_PinSocket_2.54mm", "PinSocket_1x19_P2.54mm_Vertical",
          "J_ESP_L", "ESP32 J1", ESP_X - ROW / 2, ESP_Y, 0, ESP_L,
          ref_at=(28.5, 19.6))
    place("Connector_PinSocket_2.54mm", "PinSocket_1x19_P2.54mm_Vertical",
          "J_ESP_R", "ESP32 J3", ESP_X + ROW / 2, ESP_Y, 0, ESP_R,
          ref_at=(61.5, 19.6))

    # ---- HUB75 IDC, west of the module.  Its pin-1 y is deliberately 32.43 and
    # not a round number: that puts every IDC row exactly on a gap between two
    # J1 socket pads, so each even-column signal crosses the pad wall dead
    # straight with no jog, no via and no chance of crossing a neighbour.
    place("Connector_IDC", "IDC-Header_2x08_P2.54mm_Vertical",
          "J_HUB", "HUB75", 20.0, 32.43, 0, HUB, ref_at=(20.0, 58.0))

    # IO12 (G2) is the MTDI strapping pin: it has to read LOW at reset, and the
    # panel ribbon is the only other thing on the net.  It lives under the
    # module -- lying flat it is ~2.5mm tall against ~8.5mm of socket clearance.
    place("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal",
          "RPD", "10k", 38.0, 62.0, 0, {"1": "HUB_G2", "2": "GND"},
          ref_at=(41.8, 59.6))

    # ---- power, south edge
    place("Connector_Phoenix_MSTB",
          "PhoenixContact_MSTBVA_2,5_2-G_1x02_P5.00mm_Vertical",
          "J_PWR", "DC IN 5V", 16.0, 85.5, 0, {"1": "VIN", "2": "GND"},
          ref_at=(18.5, 78.0))
    place("Connector_Phoenix_MSTB",
          "PhoenixContact_MSTBVA_2,5_2-G_1x02_P5.00mm_Vertical",
          "J_PANEL", "PANEL 5V", 64.0, 85.5, 0, {"1": "P5V", "2": "GND"},
          ref_at=(66.5, 78.0))
    # Tab (drain) faces the input: the body diode then points the way current
    # normally flows, so a reversed supply meets a blocking junction.
    place("Package_TO_SOT_SMD", "TO-252-3_TabPin2", "Q1", "AOD403",
          32.0, 84.0, 270, {"1": "QG", "2": "VIN", "3": "P5V"}, "AOD403",
          ref_at=(26.0, 84.0))
    place("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal",
          "RG", "100k", 37.0, 82.0, 0, {"1": "QG", "2": "GND"},
          ref_at=(40.8, 79.6))
    # Both caps stay SOUTH of y=73.8.  The module's USB port overhangs its south
    # end at about 10mm height, and a D8 radial cap is ~11mm tall -- put it any
    # further north and the cable has nowhere to go.
    place("Capacitor_THT", "CP_Radial_D8.0mm_P3.50mm", "CB1", "470uF/16V",
          48.0, 78.0, 0, {"1": "P5V", "2": "GND"}, ref_at=(49.75, 71.0))
    place("Capacitor_THT", "C_Disc_D5.0mm_W2.5mm_P5.00mm", "CB2", "100nF",
          56.0, 78.0, 0, {"1": "P5V", "2": "GND"}, ref_at=(58.5, 74.5))
    # A dev board's 5V pin has no reverse-current protection.  Pull this shunt
    # before plugging a USB cable in, or the panel supply back-feeds the host
    # port.  It sits west of the module body so the shunt has headroom.
    place("Connector_PinHeader_2.54mm", "PinHeader_1x02_P2.54mm_Vertical",
          "JP1", "ESP 5V", 26.0, 66.0, 0, {"1": "ESP_5V", "2": "P5V"},
          ref_at=(27.3, 63.0))

    route()
    pour()

    board.BuildConnectivity()
    filler = pcbnew.ZONE_FILLER(board)
    filler.Fill(board.Zones())
    board.Save(dest)
    return board


SIG_W = 0.25          # threads the 0.84mm gap between 2.54mm-pitch pads


def route():
    hub75_rgb()
    hub75_ctrl()
    power()


# ---- RGB: IDC pins 1,2,3,5,6,7 -> J1 pins 9..15, all on F.Cu.  Odd-column pads
# escape NORTH-east through the pad gap, which puts the six escape lanes in the
# same order as the six destinations -- so the fan has no crossings at all, and
# the lanes just have to run x-descending as they go south.
# (net, IDC pad, J1 pad, lane x)
RGB_FAN = [
    ("HUB_R1", 1, 9, 31.0),
    ("HUB_G1", 2, 10, 29.6),
    ("HUB_B1", 3, 11, 28.2),
    ("HUB_R2", 5, 12, 26.8),
    ("HUB_G2", 6, 13, 25.4),
    ("HUB_B2", 7, 15, 24.0),
]


def hub75_rgb():
    for netname, jpin, upin, lane in RGB_FAN:
        src = P("J_HUB", jpin)
        dst = P("J_ESP_L", upin)
        pts = [src]
        if jpin % 2:                       # odd column sits behind the even one
            pts.append((src[0] + 1.27, src[1] - 1.27))
        pts += [(lane, pts[-1][1]), (lane, dst[1]), dst]
        track(pts, netname, width=SIG_W)

    # G2 is also IO12/MTDI: hold it low.  The stub runs down the corridor at
    # x=35, just clear of the J1 pad wall.
    track([P("J_ESP_L", 13), (35.0, 51.48), (35.0, 62.0), P("RPD", 1)],
          "HUB_G2", width=SIG_W)


# ---- control: J3 -> IDC pins 8..15.  Every horizontal is on B.Cu and every
# vertical on F.Cu, so no two of the eight can collide no matter what order they
# leave the module in -- which matters, because the source order and the
# destination order genuinely disagree (E and B invert, and the four odd-column
# pins have to wrap around the west side of the connector).
# The corridor between the two socket rows is empty on both layers: that free
# 22.86mm channel is the one thing a socketed module buys over a soldered WROOM.
# (net, J3 pad, IDC pad, wall-gap y, corridor lane x, west lane x or None)
CTRL_FAN = [
    ("HUB_A",    2,  9, 27.35, 51.0, 18.5),
    ("HUB_B",    8, 10, 42.59, 49.0, None),
    ("HUB_E",    9,  8, 40.05, 47.0, None),
    ("HUB_C",   10, 11, 55.29, 45.0, 17.0),
    ("HUB_D",   11, 12, 45.13, 43.0, None),
    ("HUB_CLK", 12, 13, 57.83, 41.0, 15.5),
    ("HUB_LAT", 13, 14, 47.67, 39.0, None),
    ("HUB_OE",  16, 15, 60.37, 37.0, 14.0),
]


def hub75_ctrl():
    for netname, jpad, ipad, ygap, xc, xw in CTRL_FAN:
        src = P("J_ESP_R", jpad)
        dst = P("J_HUB", ipad)
        track([src, (xc, src[1])], netname, pcbnew.B_Cu, SIG_W)
        via(xc, src[1], netname)
        track([(xc, src[1]), (xc, ygap)], netname, pcbnew.F_Cu, SIG_W)
        via(xc, ygap, netname)
        if xw is None:
            # even column: ygap IS the pad's row, so this is one straight line
            track([(xc, ygap), dst], netname, pcbnew.B_Cu, SIG_W)
            continue
        # odd column: the even column blocks a direct approach, so go round it
        track([(xc, ygap), (xw, ygap)], netname, pcbnew.B_Cu, SIG_W)
        via(xw, ygap, netname)
        track([(xw, ygap), (xw, dst[1])], netname, pcbnew.F_Cu, SIG_W)
        via(xw, dst[1], netname)
        track([(xw, dst[1]), dst], netname, pcbnew.B_Cu, SIG_W)


def power():
    # VIN: screw in -> the FET tab.  The tab is the drain and faces the input, so
    # the body diode points the way current normally flows and a reversed supply
    # meets a blocking junction instead of a free path.
    track([P("J_PWR", 1), (16.0, 81.0), (27.0, 81.0), (32.0, 84.5)],
          "VIN", width=PWR_W)
    bond("Q1", 2, "VIN", width=1.0)
    track([P("Q1", 1), P("RG", 1)], "QG")

    # P5V spine: west out of the FET, then straight along y=72 to the panel
    # terminal, with the bulk cap, the bypass and JP1 hanging off it.
    track([P("Q1", 3), (26.0, 76.0), (26.0, 72.0), (64.0, 72.0), P("J_PANEL", 1)],
          "P5V", width=PWR_W)
    track([(48.0, 72.0), P("CB1", 1)], "P5V", width=0.8)
    track([(56.0, 72.0), P("CB2", 1)], "P5V", width=0.8)
    track([(26.0, 72.0), P("JP1", 2)], "P5V", width=1.0)
    track([P("JP1", 1), P("J_ESP_L", 19)], "ESP_5V", width=1.0)



def pour():
    """GND on both layers.  The eight B.Cu control lanes run most of the width of
    the board and slice the bottom pour into bands; the top pour is what ties
    those bands back together through the through-hole pads that land in both."""
    x1, y1, x2, y2 = BOARD
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(net("GND"))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetLocalClearance(MM(0.25))
        z.SetMinThickness(MM(0.25))
        ol = z.Outline()
        ol.NewOutline()
        for x, y in [(x1 + 0.6, y1 + 0.6), (x2 - 0.6, y1 + 0.6),
                     (x2 - 0.6, y2 - 0.6), (x1 + 0.6, y2 - 0.6)]:
            ol.Append(MM(x), MM(y))
        board.Add(z)


if __name__ == "__main__":
    b = main(sys.argv[1], "--dump" in sys.argv)
    if "--dump" in sys.argv:
        for fp in sorted(b.GetFootprints(), key=lambda f: f.GetReference()):
            for p in fp.Pads():
                pos = p.GetPosition()
                print(f"{fp.GetReference():7s} {p.GetNumber():4s} "
                      f"({pcbnew.ToMM(pos.x):6.2f},{pcbnew.ToMM(pos.y):6.2f}) "
                      f"{p.GetNetname()}")
