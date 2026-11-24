"""Review fixes: move two tracks out from under M3 screw heads, add board name / version / date to the silkscreen."""
import pcbnew, sys, math
PCB = sys.argv[1]
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
def V(p): return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))
def find(net, a, c):
    for t in b.GetTracks():
        if t.GetClass() == "PCB_TRACK" and t.GetNetname() == net:
            s = (mm(t.GetStart().x), mm(t.GetStart().y)); e = (mm(t.GetEnd().x), mm(t.GetEnd().y))
            if (math.hypot(s[0]-a[0], s[1]-a[1]) < 0.011 and math.hypot(e[0]-c[0], e[1]-c[1]) < 0.011) or (math.hypot(s[0]-c[0], s[1]-c[1]) < 0.011 and math.hypot(e[0]-a[0], e[1]-a[1]) < 0.011): return t
    raise SystemExit(f"track not found: {net} {a} {c}")
def reroute(net, old, new, width, layer=pcbnew.F_Cu):
    olds = [find(net, a, c) for a, c in zip(old, old[1:])]
    code = olds[0].GetNetCode()
    for t in olds: b.Remove(t)
    for a, c in zip(new, new[1:]):
        t = pcbnew.PCB_TRACK(b); t.SetStart(V(a)); t.SetEnd(V(c)); t.SetWidth(MM(width)); t.SetLayer(layer); t.SetNetCode(code); b.Add(t)
    print("rerouted", net, "->", new)
# H5 (113.0, 66.6): AVDD ran 1.9 mm from the hole centre
reroute("AVDD", [(112.75, 62.95), (110.95, 64.75), (110.95, 70.55), (110.0, 71.5)],
        [(112.75, 62.95), (110.25, 62.95), (109.55, 63.65), (109.55, 71.05), (110.0, 71.5)], 0.3)
# H6 (100.5, 132.0): BTN12 ran 1.9 mm from the hole centre
reroute("BTN12", [(107.65, 135.35), (102.35, 130.05), (97.55, 130.05), (93.65, 126.15)],
        [(107.65, 135.35), (104.75, 132.45), (104.75, 130.6), (102.3, 128.15), (95.65, 128.15), (93.65, 126.15)], 0.15)
OX, OY = 50.0, 40.0
def text(s, x, y, size, thick, layer, angle=0, mirror=False):
    t = pcbnew.PCB_TEXT(b); t.SetText(s); t.SetLayer(layer); t.SetPosition(V((OX + x, OY + y)))
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size))); t.SetTextThickness(MM(thick))
    t.SetTextAngle(pcbnew.EDA_ANGLE(angle, pcbnew.DEGREES_T)); t.SetMirrored(mirror); b.Add(t)
NAME, VER = "Density Controller Test", "v0.1  2026-11-17"
text(NAME, 1.9, 97.0, 1.1, 0.17, pcbnew.F_SilkS, 90)
text(VER, 4.0, 97.0, 1.0, 0.15, pcbnew.F_SilkS, 90)
text(NAME, 50.0, 70.5, 3.0, 0.4, pcbnew.B_SilkS, 0, True)
text(VER, 50.0, 76.0, 2.2, 0.3, pcbnew.B_SilkS, 0, True)
b.BuildConnectivity(); print("unconnected:", b.GetConnectivity().GetUnconnectedCount(False))
pcbnew.SaveBoard(PCB, b); print("saved")
