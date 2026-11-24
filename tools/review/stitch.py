"""Ground stitching vias on a grid wherever the front pour, back pour and inner plane are all ground and every layer is clear,
plus pin labels beside the spare-I/O header J5.   usage: stitch.py <board> [pitch mm]"""
import pcbnew, sys, math
PCB = sys.argv[1]; PITCH = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
OX, OY, W, H = 50.0, 40.0, 125.0, 130.0
def pseg(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
fills = {}
for z in b.Zones():
    if z.GetIsRuleArea() or z.GetNetname() != "GND": continue
    for lay in z.GetLayerSet().Seq(): fills[lay] = z.GetFilledPolysList(lay)
need = [pcbnew.F_Cu, pcbnew.B_Cu, pcbnew.In1_Cu]
T = [((mm(t.GetStart().x), mm(t.GetStart().y)), (mm(t.GetEnd().x), mm(t.GetEnd().y)), mm(t.GetWidth()), t.GetNetname()) for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
Vs = [(mm(t.GetPosition().x), mm(t.GetPosition().y)) for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
Pd = []
for fp in b.GetFootprints():
    for p in fp.Pads():
        bb = p.GetBoundingBox(); Pd.append((mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom())))
# coarse buckets for speed
def near_tracks(q):
    return [t for t in T if min(t[0][0], t[1][0]) - 1 < q[0] < max(t[0][0], t[1][0]) + 1 and min(t[0][1], t[1][1]) - 1 < q[1] < max(t[0][1], t[1][1]) + 1]
def ok(q):
    for x0, y0, x1, y1 in Pd:
        if math.hypot(max(x0 - q[0], 0, q[0] - x1), max(y0 - q[1], 0, q[1] - y1)) < 0.25 + 0.6: return False
    for v in Vs:
        if math.hypot(q[0]-v[0], q[1]-v[1]) < 1.2: return False
    for a, c, w, net in near_tracks(q):
        d = pseg(q, a, c)
        if net != "GND" and d < 0.25 + w/2 + 0.2: return False
    for lay in need:
        f = fills[lay]
        for k in range(8):
            ang = 2*math.pi*k/8
            if not f.Contains(pcbnew.VECTOR2I(MM(q[0] + 0.5*math.cos(ang)), MM(q[1] + 0.5*math.sin(ang)))): return False
    return True
gnd = b.FindNet("GND"); added = []
ny = int((H - 3)/PITCH) + 1; nx = int((W - 3)/PITCH) + 1
for j in range(ny):
    for i in range(nx):
        base = (OX + 1.5 + i*PITCH + (PITCH/2 if j % 2 else 0), OY + 1.5 + j*PITCH)
        if base[0] > OX + W - 1.5: continue
        for dx, dy in ((0, 0), (0.8, 0), (-0.8, 0), (0, 0.8), (0, -0.8), (0.8, 0.8), (-0.8, -0.8), (0.8, -0.8), (-0.8, 0.8), (1.6, 0), (-1.6, 0), (0, 1.6), (0, -1.6)):
            q = (round(base[0] + dx, 2), round(base[1] + dy, 2))
            if OX + 1.2 < q[0] < OX + W - 1.2 and OY + 1.2 < q[1] < OY + H - 1.2 and ok(q):
                v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(MM(q[0]), MM(q[1]))); v.SetWidth(pcbnew.F_Cu, MM(0.5)); v.SetDrill(MM(0.3)); v.SetNet(gnd); v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
                Vs.append(q); added.append(q); break
print("stitching vias added:", len(added), "of", nx*ny, "grid points")
# J5 pin labels
j5 = b.FindFootprintByReference("J5"); names = {"+3V3": "3V", "GND": "G"}
n = 0
for pad in j5.Pads():
    net = pad.GetNetname(); lab = names.get(net, net.replace("GP", ""))
    t = pcbnew.PCB_TEXT(b); t.SetText(lab); t.SetLayer(pcbnew.F_SilkS)
    t.SetTextSize(pcbnew.VECTOR2I(MM(1.0), MM(1.0))); t.SetTextThickness(MM(0.15)); t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_LEFT)
    t.SetPosition(pcbnew.VECTOR2I(pad.GetPosition().x + MM(1.35), pad.GetPosition().y)); b.Add(t); n += 1
    print("   J5 pin", pad.GetNumber(), net, "->", lab)
pcbnew.SaveBoard(PCB, b); print("saved")
