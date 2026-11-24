"""Second-review fixes: move MUX_S1 (In2) away from hole H5, drop duplicate / fully-overlapped track segments,
take the three reference texts that sit outside the board off the silkscreen, thicken thin silkscreen strokes."""
import pcbnew, sys, math, collections
PCB = sys.argv[1]
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
def V(p): return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))
def P(v): return (round(mm(v.x), 3), round(mm(v.y), 3))
def D(p, q): return math.hypot(p[0]-q[0], p[1]-q[1])
def pseg(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
b.BuildConnectivity(); base = b.GetConnectivity().GetUnconnectedCount(False)
T = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]
info = [dict(t=t, net=t.GetNetname(), lay=t.GetLayer(), a=P(t.GetStart()), c=P(t.GetEnd()), w=mm(t.GetWidth())) for t in T]
kill = set()
# 1. MUX_S1 on In2
def find(net, lay, a, c):
    for i, k in enumerate(info):
        if k["net"] == net and k["lay"] == lay and ((D(k["a"], a) < 0.011 and D(k["c"], c) < 0.011) or (D(k["a"], c) < 0.011 and D(k["c"], a) < 0.011)): return i
    raise SystemExit(f"not found {net} {a} {c}")
L2 = b.GetLayerID("In2.Cu")
for a, c in (((111.05, 62.75), (108.35, 60.05)), ((111.05, 68.45), (111.05, 62.75)), ((111.25, 68.65), (111.05, 68.45)), ((111.25, 79.85), (111.25, 68.65))): kill.add(find("MUX_S1", L2, a, c))
code = info[next(iter(kill))]["t"].GetNetCode()
new = [(108.35, 60.05), (110.25, 61.95), (110.25, 69.55), (111.25, 70.55), (111.25, 79.85)]
for a, c in zip(new, new[1:]):
    t = pcbnew.PCB_TRACK(b); t.SetStart(V(a)); t.SetEnd(V(c)); t.SetWidth(MM(0.15)); t.SetLayer(L2); t.SetNetCode(code); b.Add(t)
print("MUX_S1 rerouted on In2:", new)
# 2. duplicates and fully-covered segments
groups = collections.defaultdict(list)
for i, k in enumerate(info):
    if i not in kill: groups[(k["net"], k["lay"], tuple(sorted((k["a"], k["c"]))))].append(i)
dup = 0
for g in groups.values():
    g.sort(key=lambda i: -info[i]["w"])
    for i in g[1:]: kill.add(i); dup += 1
cov = 0
bylay = collections.defaultdict(list)
for i, k in enumerate(info):
    if i not in kill: bylay[(k["net"], k["lay"])].append(i)
for g in bylay.values():
    for i in g:
        k = info[i]
        if i in kill: continue
        for j in g:
            o = info[j]
            if j == i or j in kill or o["w"] < k["w"] - 1e-6: continue
            if D(o["a"], o["c"]) > D(k["a"], k["c"]) + 1e-6 and pseg(k["a"], o["a"], o["c"]) < 0.005 and pseg(k["c"], o["a"], o["c"]) < 0.005: kill.add(i); cov += 1; break
print("exact duplicate segments removed:", dup, "| segments lying wholly on another removed:", cov)
# 3. silkscreen
moved = []
for fp in b.GetFootprints():
    if fp.GetReference() in ("A1", "J1", "J2"):
        r = fp.Reference()
        if r.GetLayer() == pcbnew.F_SilkS: r.SetLayer(pcbnew.F_Fab); moved.append(fp.GetReference())
    for g in fp.GraphicalItems():
        if g.GetLayer() == pcbnew.F_SilkS and g.GetClass() == "PCB_SHAPE" and 0 < g.GetWidth() < MM(0.15): g.SetWidth(MM(0.15))
print("reference texts moved off the silkscreen:", moved)
for i in kill: b.Remove(info[i]["t"])
b.BuildConnectivity(); now = b.GetConnectivity().GetUnconnectedCount(False)
print("unconnected before/after:", base, now)
if now != base: raise SystemExit("connectivity changed - not saving")
pcbnew.SaveBoard(PCB, b); print("saved")
