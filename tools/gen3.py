"""Sync schematic -> board, then place everything, draw the outline and add planes."""
import os, json, sys, math
from kmcp import run
import pcbnew

PROJ_DIR = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130"
NAME = "density_proto"
SCH = f"{PROJ_DIR}/{NAME}.kicad_sch"
PCB = f"{PROJ_DIR}/{NAME}.kicad_pcb"
PRO = f"{PROJ_DIR}/{NAME}.kicad_pro"
HERE = os.path.dirname(os.path.abspath(__file__))
OX, OY = 50.0, 40.0          # board origin on the page
W, H = 125.0, 130.0
mm = pcbnew.FromMM

comps = json.load(open(f"{HERE}/comps.json"))
byref = {c["reference"]: c for c in comps}
def find(value=None, net=None, prefix=None):
    out = []
    for c in comps:
        if prefix and not c["reference"].startswith(prefix): continue
        if value and c["value"] != value: continue
        if net and net not in c["nets"].values(): continue
        out.append(c["reference"])
    return out
def one(**k):
    r = find(**k)
    assert len(r) == 1, (k, r)
    return r[0]

if "--sync" in sys.argv:
    # fresh empty board, then import the netlist
    r = run("sync_schematic_to_board", {"schematicPath": SCH, "boardPath": PCB})
    print("sync:", r.get("success"), str(r.get("message"))[:200])

b = pcbnew.LoadBoard(PCB)
b.SetCopperLayerCount(4)
print("footprints on board:", len(b.GetFootprints()))

placed = set()
def P(ref, x, y, rot=0):
    fp = b.FindFootprintByReference(ref)
    if fp is None:
        print("MISSING", ref); return None
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y)))
    placed.add(ref)
    return fp
def padpos(ref, num):
    fp = b.FindFootprintByReference(ref)
    for p in fp.Pads():
        if p.GetNumber() == num:
            return (pcbnew.ToMM(p.GetPosition().x) - OX, pcbnew.ToMM(p.GetPosition().y) - OY)

# ---------------------------------------------------------------- pots, their buttons and LEDs
CH = 0.6      # chips sit this far right of the channel centre (pot caps use the left edge)
for i in range(16):
    c, r = i % 4, i // 4
    sx, sy = 25 * c + 12.5, 30 + 25 * r + 8.5
    P(f"RV{i+1}", sx + 2.5, sy - 7, 270)
    P(one(value="10nF", net=f"POT{i+1}"), sx + 7.35, sy - 6.5, 90)
    P(f"SW{i+1}", sx - 2.45, sy + 10, 0)
    # LED sits right of its switch; the chain starts bottom right and snakes upward, ending top right.
    # Rows 3 and 1 run right-to-left (LED unrotated: DIN on its right), rows 2 and 0 left-to-right (LED turned 180).
    rtl = r in (3, 1)
    pos = (3 - r) * 4 + ((3 - c) if rtl else c) + 1
    P(f"D{pos}", sx + 5.05, sy + 10, 0 if rtl else 180)
# right column: encoder, four buttons, fader
P("ENC1", 105.0, 10.0, 0)
P(one(value="10nF", net="ENC_A"), 102.6, 9.0, 90)
P(one(value="10nF", net="ENC_B"), 102.6, 16.0, 90)
for k in range(4):
    P(f"SW{17+k}", 110.05, 28.5 + 7.0 * k, 0)
    P(f"D{17+k}", 117.55, 28.5 + 7.0 * k, 0)
# LED decoupling caps: C for LED n is the n-th 100nF on +5V after U8's
ledcaps = [c["reference"] for c in comps if c["value"] == "100nF" and "+5V" in c["nets"].values()]
u8cap, ledcaps = ledcaps[0], ledcaps[1:]
assert len(ledcaps) == 20
for n in range(20):
    fp = b.FindFootprintByReference(f"D{n+1}")
    x = pcbnew.ToMM(fp.GetPosition().x) - OX; y = pcbnew.ToMM(fp.GetPosition().y) - OY
    if abs(fp.GetOrientationDegrees()) < 1:
        P(ledcaps[n], x + 0.6, y - 2.5, 180)   # LED 5V pad is top right: cap above it, 5V pad to the right
    else:
        P(ledcaps[n], x - 0.6, y + 2.5, 0)     # LED turned 180: 5V pad is bottom left, cap below
P("RV17", 112.5, 91.0, 90)
wx, wy = padpos("RV17", "2")
P(one(value="10nF", net="FADER1"), 117.9, wy, 90)

# ---------------------------------------------------------------- top strip
P("A1", 50.0, 0.0, 0)
P("J1", 14.8, 2.5, 0); P("J2", 85.2, 2.5, 0)
P("JP1", 11.0, 6.5, 0); P("JP2", 18.6, 6.5, 0); P("JP3", 81.4, 6.5, 0); P("JP4", 89.0, 6.5, 0)
P(one(value="4.7k", net="SCL"), 14.0, 9.4, 90); P(one(value="4.7k", net="SDA"), 15.6, 9.4, 90)
for j, jx in enumerate((34.8, 65.2)):
    n = j + 1
    P(f"J{3+j}", jx, 6.7, 270)
    P(one(value="470R", net=f"PED{n}_RING"), jx - 2.3, 14.7, 90)
    P(one(value="1k", net=f"PED{n}_TIP"), jx - 0.8, 14.7, 90)
    P(one(value="100k", net=f"PEDAL{n}"), jx + 0.7, 14.7, 90)
    P(one(value="10nF", net=f"PEDAL{n}"), jx + 2.2, 14.7, 90)
c5v = find(value="22uF")
P(c5v[0], 43.5, 25.2, 0); P(c5v[1], 47.0, 25.2, 0)
P(one(value="10uF", net="+3V3"), 50.5, 25.2, 0)
P("FB1", 45.0, 27.7, 0)
P(one(value="10uF", net="AVDD"), 48.6, 27.7, 0)

# ---------------------------------------------------------------- chips in the channels between pot columns
X50, X25, X75 = 50 + CH, 25 + CH, 75 + CH
P("U3", X50, 37.0, 0)
a100 = [c["reference"] for c in comps if c["value"] == "100nF" and "AVDD" in c["nets"].values()]   # U1, U4, U3 in creation order
P(a100[2], X50 - 1.4, 43.5, 0)
P(one(value="1uF"), X50 + 1.4, 43.5, 0)
P(one(value="100R", net="ADC0"), X50 - 1.4, 45.6, 0); P(one(value="1nF", net="ADC0"), X50 + 1.4, 45.6, 0)
P("U4", X50, 49.5, 0)
P(a100[1], X50, 54.75, 0)
P(one(value="100R", net="ADC1"), X50 - 1.4, 53.2, 0); P(one(value="1nF", net="ADC1"), X50 + 1.4, 53.2, 0)
P("U1", X50, 73.5, 90)
P(a100[0], X50 + 2.9, 67.9, 0)
d100 = [c["reference"] for c in comps if c["value"] == "100nF" and "+3V3" in c["nets"].values()]   # U5, U6, U7
for u, (ux, uy) in enumerate([(X25, 37.0), (X25, 87.0), (X75, 37.0)]):
    P(f"U{5+u}", ux, uy, 0)
    P(d100[u], ux, uy + 6.5, 0)
    P(f"RN{2*u+1}", ux - CH - 3.3, uy + 10.2, 0)      # button pads face outwards, 3V3 pads face each other
    P(f"RN{2*u+2}", ux - CH + 3.3, uy + 10.2, 180)
P("U8", 98.5, 110.0, 0); P(u8cap, 98.5, 107.3, 0); P(one(value="33R"), 98.5, 112.8, 0)
P("J5", 2.6, 64.5, 0)

# ---------------------------------------------------------------- test points and mounting holes
TPXY = {"TP1": (X75 - 2.3, 58.0), "TP2": (X75 + 2.3, 58.0), "TP4": (X75 - 2.3, 61.5), "TP8": (98.5, 116.0),
        "TP9": (122.6, 50.4), "TP10": (X75 + 2.3, 65.0),
        "TP5": (X50 - 2.2, 57.0), "TP6": (X50 + 2.2, 57.0), "TP7": (X50 - 2.2, 60.5), "TP13": (X50 + 2.2, 60.5),
        "TP14": (X50 - 2.2, 64.0), "TP3": (X50 + 2.2, 64.0), "TP11": (38.5, 18.3), "TP12": (38.5, 21.6)}
for tpn, (x, y) in TPXY.items():
    P(tpn, x, y, 0)
for h, (x, y) in zip(["H1", "H2", "H3", "H4", "H5", "H6"], [(3.0, 38.5), (121.8, 58.0), (3.0, 113.5), (121.8, 125.5), (63.0, 26.6), (50.5, 92.0)]):
    P(h, x, y, 0)

allrefs = {fp.GetReference() for fp in b.GetFootprints()}
print("unplaced:", sorted(allrefs - placed))

# ---------------------------------------------------------------- outline (rounded rectangle)
have_outline = any(d.GetLayer() == pcbnew.Edge_Cuts for d in b.GetDrawings())
R = 2.0
def seg(x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_SEGMENT); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    s.SetStart(pcbnew.VECTOR2I(mm(OX + x1), mm(OY + y1))); s.SetEnd(pcbnew.VECTOR2I(mm(OX + x2), mm(OY + y2))); b.Add(s)
def arc(cx, cy, a0):
    s = pcbnew.PCB_SHAPE(b); s.SetShape(pcbnew.SHAPE_T_ARC); s.SetLayer(pcbnew.Edge_Cuts); s.SetWidth(mm(0.1))
    pt = lambda a: pcbnew.VECTOR2I(mm(OX + cx + R * math.cos(math.radians(a))), mm(OY + cy + R * math.sin(math.radians(a))))
    s.SetArcGeometry(pt(a0), pt(a0 + 45), pt(a0 + 90)); b.Add(s)
if not have_outline:
    seg(R, 0, W - R, 0); seg(W, R, W, H - R); seg(W - R, H, R, H); seg(0, H - R, 0, R)
    arc(R, R, 180); arc(W - R, R, 270); arc(W - R, H - R, 0); arc(R, H - R, 90)

# ---------------------------------------------------------------- planes and pours
have_zones = len(b.Zones()) > 0
def zone(layer, netname, prio=0):
    net = b.FindNet(netname) or b.FindNet(netname.lstrip("/"))
    assert net is not None, netname
    z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(net); z.SetAssignedPriority(prio)
    o = z.Outline(); o.NewOutline()
    for x, y in ((0, 0), (W, 0), (W, H), (0, H)):
        o.Append(mm(OX + x), mm(OY + y))
    z.SetMinThickness(mm(0.2)); z.SetLocalClearance(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
    b.Add(z); return z
if not have_zones:
    zone(pcbnew.In1_Cu, "/GND")
    zone(pcbnew.In2_Cu, "/AVDD")
if "--pours" in sys.argv:
    zone(pcbnew.F_Cu, "/GND"); zone(pcbnew.B_Cu, "/GND")

for z in b.Zones():
    if z.GetLayer() == pcbnew.In2_Cu:
        z.SetNet(b.FindNet("GND"))          # In2 is a routing layer with a GND pour; AVDD is routed as traces
b.SetLayerType(pcbnew.In1_Cu, pcbnew.LT_POWER)   # keep the autorouter off the solid ground plane
pcbnew.SaveBoard(PCB, b)
print("saved", PCB)

# sanity: where did the pot shaft / pins land?
for ref in ("RV1", "RV17", "J3", "A1", "ENC1"):
    fp = b.FindFootprintByReference(ref)
    print(ref, [(p.GetNumber(), round(pcbnew.ToMM(p.GetPosition().x) - OX, 2), round(pcbnew.ToMM(p.GetPosition().y) - OY, 2)) for p in fp.Pads()][:9])
