"""Slide the switch + LED rows under the pots down by DY mm without re-routing:
a continuous piecewise-linear stretch of the board in y. Above each row the copper is stretched, the row itself moves rigidly,
and the strip between the row and the next pot row is compressed. Nothing right of x = 100 mm moves.
usage: stretch.py <board> <dy>"""
import pcbnew, sys, math
PCB = sys.argv[1]; DY = float(sys.argv[2])
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
OX, OY = 50.0, 40.0
ROWS = []
for r in range(4):
    S = 38.5 + 25*r
    c = S + (14.7 if r == 1 else 13.6)            # row 1 has the mux U1, whose pads reach further down
    d = 129.7 if r == 3 else S + 16.9
    ROWS.append((S + 3.2, S + 6.4, c, d))
XA, XB = 96.0, 100.0
YB = sorted({v for row in ROWS for v in row})
def fy(y):
    for a, b_, c, d in ROWS:
        if a < y < d:
            if y <= b_: return DY*(y - a)/(b_ - a)
            if y <= c: return DY
            return DY*(d - y)/(d - c)
    return 0.0
def g(x): return 1.0 if x <= XA else (0.0 if x >= XB else (XB - x)/(XB - XA))
def sh(p): return fy(p[1] - OY)*g(p[0] - OX)       # p in page mm
def P(v): return (mm(v.x), mm(v.y))
def V(p): return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))
# footprints
fps = []
pads = []
for fp in b.GetFootprints():
    ref = fp.GetReference(); o = P(fp.GetPosition())
    s = 0.0 if ref.startswith(("H", "RV", "A", "J", "TP", "ENC")) else sh(o)
    fps.append((fp, s))
    for pad in fp.Pads():
        bb = pad.GetBoundingBox(); pads.append((mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()), pad, s))
def endshift(p, layer):
    for x0, y0, x1, y1, pad, s in pads:
        if x0 <= p[0] <= x1 and y0 <= p[1] <= y1 and pad.IsOnLayer(layer) and pad.HitTest(V(p), 0): return s
    return sh(p)
tracks = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]; vias = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
plan = []; newsegs = []
for t in tracks:
    a, c = P(t.GetStart()), P(t.GetEnd()); lay = t.GetLayer()
    cuts = []
    for yb in YB:
        y = yb + OY
        if (a[1] - y)*(c[1] - y) < 0: cuts.append((y - a[1])/(c[1] - a[1]))
    for xb in (XA, XB):
        x = xb + OX
        if (a[0] - x)*(c[0] - x) < 0: cuts.append((x - a[0])/(c[0] - a[0]))
    pts = [a] + [(a[0] + (c[0]-a[0])*u, a[1] + (c[1]-a[1])*u) for u in sorted(cuts)] + [c]
    sa, sc = endshift(a, lay), endshift(c, lay)
    mapped = [(a[0], a[1] + sa)] + [(p[0], p[1] + sh(p)) for p in pts[1:-1]] + [(c[0], c[1] + sc)]
    plan.append((t, mapped))
moved_v = 0
for v in vias:
    p = P(v.GetPosition()); s = sh(p)
    if s > 1e-6: v.SetPosition(V((p[0], p[1] + s))); moved_v += 1
nseg = 0
for t, m in plan:
    t.SetStart(V(m[0])); t.SetEnd(V(m[1]))
    for p, q in zip(m[1:], m[2:]):
        n = pcbnew.PCB_TRACK(b); n.SetStart(V(p)); n.SetEnd(V(q)); n.SetWidth(t.GetWidth()); n.SetLayer(t.GetLayer()); n.SetNetCode(t.GetNetCode()); b.Add(n); nseg += 1
movedfp = []
for fp, s in fps:
    if s > 1e-6:
        o = fp.GetPosition(); fp.SetPosition(pcbnew.VECTOR2I(o.x, o.y + MM(s))); movedfp.append((fp.GetReference(), round(s, 2)))
full = [r for r, s in movedfp if abs(s - DY) < 1e-6]; part = [(r, s) for r, s in movedfp if abs(s - DY) >= 1e-6]
print(f"dy {DY}: {len(full)} parts moved the full distance, {len(part)} partly: {part}")
print("vias moved:", moved_v, "| extra track segments from splitting:", nseg)
b.BuildConnectivity(); print("unconnected (before refill):", b.GetConnectivity().GetUnconnectedCount(False))
pcbnew.SaveBoard(PCB, b); print("saved")
