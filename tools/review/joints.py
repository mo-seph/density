"""Audit how every track end is joined: exact / T-junction on centreline / partial overlap only / free."""
import pcbnew, sys, math, collections
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.ToMM; MM = pcbnew.FromMM
def segd(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
zone_nets = {z.GetNetname() for z in b.Zones()}
bynet = collections.defaultdict(lambda: dict(tr=[], vi=[], pads=[]))
for t in b.GetTracks():
    n = bynet[t.GetNetname()]
    if t.GetClass() == "PCB_VIA": n["vi"].append(dict(q=(mm(t.GetPosition().x), mm(t.GetPosition().y)), r=mm(t.GetWidth(pcbnew.F_Cu))/2))
    else: n["tr"].append(dict(a=(mm(t.GetStart().x), mm(t.GetStart().y)), c=(mm(t.GetEnd().x), mm(t.GetEnd().y)), w=mm(t.GetWidth()), lay=t.GetLayer(), name=t.GetLayerName()))
for fp in b.GetFootprints():
    for p in fp.Pads(): bynet[p.GetNetname()]["pads"].append(p)
stats = collections.Counter(); out = []
for net, n in bynet.items():
    for i, t in enumerate(n["tr"]):
        for p in (t["a"], t["c"]):
            kind = None; best = 9
            for pad in n["pads"]:
                if pad.IsOnLayer(t["lay"]) and pad.HitTest(pcbnew.VECTOR2I(MM(p[0]), MM(p[1])), 0): kind = "pad"
            if not kind:
                for v in n["vi"]:
                    d = math.hypot(p[0]-v["q"][0], p[1]-v["q"][1])
                    if d <= 0.011: kind = "via"
                    elif d <= v["r"] + t["w"]/2: best = min(best, d); kind = kind or None
            if not kind:
                for j, o in enumerate(n["tr"]):
                    if j == i or o["lay"] != t["lay"]: continue
                    if math.hypot(p[0]-o["a"][0], p[1]-o["a"][1]) <= 0.011 or math.hypot(p[0]-o["c"][0], p[1]-o["c"][1]) <= 0.011: kind = "end"; break
                if not kind:
                    for j, o in enumerate(n["tr"]):
                        if j == i or o["lay"] != t["lay"]: continue
                        d = segd(p, o["a"], o["c"])
                        if d <= 0.011: kind = "tee"; break
                        if d <= (o["w"] + t["w"])/2: best = min(best, d)
            if not kind:
                kind = "overlap" if best < 9 else ("zone?" if net in zone_nets else "free")
                out.append((kind, net, t["name"], tuple(round(x, 3) for x in p), round(best, 3) if best < 9 else None, round(math.hypot(t["a"][0]-t["c"][0], t["a"][1]-t["c"][1]), 3)))
            stats[kind] += 1
print(dict(stats))
for o in sorted(out): print(" ", o)
