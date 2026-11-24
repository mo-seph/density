"""Give an SMD ground pad its own via to the plane, placed inside the pour island it sits in.  usage: gvia.py <board> REF PAD [apply]"""
import pcbnew, sys, math
PCB, REF, NUM = sys.argv[1:4]; APPLY = len(sys.argv) > 4
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
pad = [p for p in b.FindFootprintByReference(REF).Pads() if p.GetNumber() == NUM][0]
pc = pad.GetPosition(); lay = pcbnew.F_Cu
island = None
for z in b.Zones():
    if lay in list(z.GetLayerSet().Seq()):
        poly = z.GetFilledPolysList(lay)
        for i in range(poly.OutlineCount()):
            if poly.Outline(i).PointInside(pc): island = poly.Outline(i)
if island is None: raise SystemExit("pad is not inside any pour island")
def pseg(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
T = [(t.GetNetname(), (mm(t.GetStart().x), mm(t.GetStart().y)), (mm(t.GetEnd().x), mm(t.GetEnd().y)), mm(t.GetWidth())) for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
Vs = [(t.GetNetname(), (mm(t.GetPosition().x), mm(t.GetPosition().y))) for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
Pd = [(p.GetNetname(), p.GetBoundingBox()) for fp in b.GetFootprints() for p in fp.Pads()]
def ok(q):
    v = pcbnew.VECTOR2I(MM(q[0]), MM(q[1]))
    for k in range(12):
        a = 2*math.pi*k/12
        if not island.PointInside(pcbnew.VECTOR2I(MM(q[0] + 0.4*math.cos(a)), MM(q[1] + 0.4*math.sin(a)))): return False
    for n, a, c, w in T:
        if n != "GND" and pseg(q, a, c) < 0.25 + w/2 + 0.17: return False        # every layer: it is a through via
    for n, p in Vs:
        if math.hypot(q[0]-p[0], q[1]-p[1]) < (0.75 if n != "GND" else 0.6): return False
    for n, bb in Pd:
        if n != "GND":
            dx = max(mm(bb.GetLeft()) - q[0], 0, q[0] - mm(bb.GetRight())); dy = max(mm(bb.GetTop()) - q[1], 0, q[1] - mm(bb.GetBottom()))
            if math.hypot(dx, dy) < 0.25 + 0.2: return False
        else:                                        # keep the via barrel out of solder pads
            dx = max(mm(bb.GetLeft()) - q[0], 0, q[0] - mm(bb.GetRight())); dy = max(mm(bb.GetTop()) - q[1], 0, q[1] - mm(bb.GetBottom()))
            if math.hypot(dx, dy) < 0.25 + 0.15: return False
    return True
best = None; cx, cy = mm(pc.x), mm(pc.y)
for i in range(-40, 41):
    for j in range(-40, 41):
        q = (round(cx + i*0.05, 2), round(cy + j*0.05, 2)); d = math.hypot(q[0]-cx, q[1]-cy)
        if (best is None or d < best[0]) and ok(q):
            # straight track from pad to via must stay inside the island
            if all(island.PointInside(pcbnew.VECTOR2I(MM(cx + (q[0]-cx)*s/10), MM(cy + (q[1]-cy)*s/10))) for s in range(11)): best = (d, q)
print(REF, NUM, "pad at", (cx, cy), "-> via", best)
if APPLY and best:
    q = best[1]; gnd = b.FindNet("GND")
    v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(MM(q[0]), MM(q[1]))); v.SetWidth(pcbnew.F_Cu, MM(0.5)); v.SetDrill(MM(0.3)); v.SetNet(gnd); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
    t = pcbnew.PCB_TRACK(b); t.SetStart(pc); t.SetEnd(pcbnew.VECTOR2I(MM(q[0]), MM(q[1]))); t.SetWidth(MM(0.25)); t.SetLayer(lay); t.SetNet(gnd); b.Add(t)
    pcbnew.SaveBoard(PCB, b); print("saved")
