"""Widen supply tracks in place as far as the neighbours allow, ground the mounting-lug pads, report the 5 V drop.
usage: fatten.py <board> [apply]"""
import pcbnew, sys, math, collections
import numpy as np
PCB = sys.argv[1]; APPLY = len(sys.argv) > 2
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
CAP = {"+5V": 0.8, "AVDD": 0.5, "+3V3": 0.5}; CLR = 0.17; EDGE = 0.35
OX, OY, W, H = 50.0, 40.0, 125.0, 130.0
def P(v): return (mm(v.x), mm(v.y))
def pseg(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
def cross(a, b_, c, d):
    def o(p, q, r): return (q[0]-p[0])*(r[1]-p[1]) - (q[1]-p[1])*(r[0]-p[0])
    return o(a, b_, c)*o(a, b_, d) < 0 and o(c, d, a)*o(c, d, b_) < 0
def sseg(a, b_, c, d):
    if cross(a, b_, c, d): return 0.0
    return min(pseg(a, c, d), pseg(b_, c, d), pseg(c, a, b_), pseg(d, a, b_))
T = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]; V = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
tinfo = [(t.GetNetname(), t.GetLayer(), P(t.GetStart()), P(t.GetEnd()), mm(t.GetWidth())) for t in T]
vinfo = [(v.GetNetname(), P(v.GetPosition()), mm(v.GetWidth(pcbnew.F_Cu))/2) for v in V]
pinfo = []
for fp in b.GetFootprints():
    for pad in fp.Pads():
        bb = pad.GetBoundingBox(); r = (mm(bb.GetLeft()), mm(bb.GetTop()), mm(bb.GetRight()), mm(bb.GetBottom()))
        pinfo.append((pad.GetNetname(), pad, r, pad.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH))
edges = [((OX, OY), (OX+W, OY)), ((OX+W, OY), (OX+W, OY+H)), ((OX+W, OY+H), (OX, OY+H)), ((OX, OY+H), (OX, OY))]
def room(net, lay, a, c):
    """largest half-width (centreline to obstacle edge, minus clearance) this segment can have"""
    best = 9.0
    for n2, l2, a2, c2, w2 in tinfo:
        if n2 != net and l2 == lay: best = min(best, sseg(a, c, a2, c2) - w2/2 - CLR)
    for n2, q, r in vinfo:
        if n2 != net: best = min(best, pseg(q, a, c) - r - CLR)
    for n2, pad, r, npth in pinfo:
        if n2 == net and not npth: continue
        if not (npth or pad.IsOnLayer(lay)): continue
        if min(a[0], c[0]) - 3 > r[2] or max(a[0], c[0]) + 3 < r[0] or min(a[1], c[1]) - 3 > r[3] or max(a[1], c[1]) + 3 < r[1]: continue
        cs = [(r[0], r[1]), (r[2], r[1]), (r[2], r[3]), (r[0], r[3])]
        d = min(sseg(a, c, cs[i], cs[(i+1) % 4]) for i in range(4))
        if r[0] <= a[0] <= r[2] and r[1] <= a[1] <= r[3]: d = 0
        best = min(best, d - (0.3 if npth else CLR))
    for e in edges: best = min(best, sseg(a, c, e[0], e[1]) - EDGE)
    return best
changes = collections.Counter(); plan = []
for i, t in enumerate(T):
    net, lay, a, c, w = tinfo[i]
    if net not in CAP: continue
    hw = room(net, lay, a, c); neww = min(CAP[net], math.floor(2*hw/0.05 + 1e-6)*0.05)
    if neww >= w + 0.049: plan.append((t, round(neww, 2))); changes[(net, round(neww, 2))] += mm(t.GetLength())
print("widening plan (net, new width): total length mm ->", {k: round(v, 1) for k, v in sorted(changes.items())})
def drop(widths):
    RS = {"F.Cu": 0.00049, "B.Cu": 0.00049, "In1.Cu": 0.00098, "In2.Cu": 0.00098}
    nodes = {}; links = []
    def node(p, lay): return nodes.setdefault((round(p[0], 2), round(p[1], 2), lay), len(nodes))
    tr = [(b.GetLayerName(l), a, c, widths.get(i, w)) for i, (n, l, a, c, w) in enumerate(tinfo) if n == "+5V"]
    ends = collections.defaultdict(list)
    for lay, a, c, w in tr: ends[lay] += [a, c]
    for lay, a, c, w in tr:
        pts = sorted(set([a, c] + [p for p in ends[lay] if pseg(p, a, c) < 0.011]), key=lambda p: math.hypot(p[0]-a[0], p[1]-a[1]))
        for p, q in zip(pts, pts[1:]):
            L = math.hypot(p[0]-q[0], p[1]-q[1])
            if L > 1e-6: links.append((node(p, lay), node(q, lay), RS[lay]*L/w))
    for n2, q, r in vinfo:
        if n2 == "+5V":
            ks = [i for k, i in list(nodes.items()) if math.hypot(k[0]-q[0], k[1]-q[1]) < r + 0.01]
            links += [(i, j, 0.001) for i, j in zip(ks, ks[1:])]
    padnode = {}
    for n2, pad, r, npth in pinfo:
        if n2 != "+5V": continue
        ks = [i for k, i in list(nodes.items()) if pad.IsOnLayer(b.GetLayerID(k[2])) and pad.HitTest(pcbnew.VECTOR2I(MM(k[0]), MM(k[1])), 0)]
        links += [(i, j, 0.0005) for i, j in zip(ks, ks[1:])]
        if ks: padnode[(pad.GetParentFootprint().GetReference(), pad.GetNumber())] = ks[0]
    n = len(nodes); G = np.zeros((n, n))
    for i, j, r in links:
        if i == j: continue
        g = 1/max(r, 1e-6); G[i, i] += g; G[j, j] += g; G[i, j] -= g; G[j, i] -= g
    src = padnode[("A1", "1")]; leds = sorted((k for k in padnode if k[0].startswith("D")), key=lambda k: int(k[0][1:]))
    I = np.zeros(n)
    for k in leds: I[padnode[k]] = -0.036
    keep = [i for i in range(n) if i != src]
    v = np.zeros(n); v[keep] = np.linalg.lstsq(G[np.ix_(keep, keep)], I[keep], rcond=None)[0]
    d = {k[0]: -v[padnode[k]]*1000 for k in leds}
    imax = max(abs(v[i]-v[j])/r for i, j, r in links if i != j and r > 0.002)
    return d, imax
idx = {t.m_Uuid.AsString(): i for i, t in enumerate(T)}
for label, widths in (("now", {}), ("after widening", {idx[t.m_Uuid.AsString()]: w for t, w in plan})):
    d, imax = drop(widths); worst = max(d, key=d.get)
    print(f"5 V drop with all 20 LEDs full white (0.72 A), {label}: worst {d[worst]:.0f} mV at {worst}; biggest current in one track {imax:.2f} A; " + " ".join(f"{k}:{x:.0f}" for k, x in d.items()))
mp = [(fp.GetReference(), pad) for fp in b.GetFootprints() for pad in fp.Pads() if pad.GetNumber() == "MP" and fp.GetReference().startswith(("RV", "ENC"))]
print("mounting-lug pads:", len(mp), "currently on nets", collections.Counter(p.GetNetname() for r, p in mp))
if APPLY:
    for t, w in plan: t.SetWidth(MM(w))
    gnd = b.FindNet("GND")
    for r, p in mp: p.SetNet(gnd)
    b.BuildConnectivity(); print("unconnected:", b.GetConnectivity().GetUnconnectedCount(False))
    pcbnew.SaveBoard(PCB, b); print("saved")
