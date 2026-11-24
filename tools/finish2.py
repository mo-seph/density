"""Finish a board the autorouter left nearly done.
usage: finish2.py repair <drc.json>   delete track segments involved in shorts / clearance / hole-clearance errors
       finish2.py all                 join every split signal net, then tie every isolated ground patch to the plane"""
import sys, os, json, re, math, heapq, collections
import numpy as np
import pcbnew
PCB = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/density_proto.kicad_pcb"
OX, OY, W, H = 50.0, 40.0, 125.0, 130.0
mm, tomm = pcbnew.FromMM, pcbnew.ToMM
G = 0.1
NX, NY = int(W / G), int(H / G)
CLR, VIA_D, VIA_DRILL = float(os.environ.get("CLR", "0.17")), 0.5, 0.3
LAYERS = [pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
POWER = ("+5V", "+3V3", "AVDD")
def width_of(net): return 0.3 if net in POWER else 0.15

b = pcbnew.LoadBoard(PCB)
pads, segs, vias = [], [], []
for fp in b.GetFootprints():
    for p in fp.Pads():
        bb = p.GetBoundingBox(); a = p.GetAttribute()
        thr = a in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)
        pads.append(dict(kind="pad", ref=fp.GetReference(), num=p.GetNumber(), net=p.GetNetname(), thr=thr,
                         x0=tomm(bb.GetLeft()) - OX, y0=tomm(bb.GetTop()) - OY, x1=tomm(bb.GetRight()) - OX, y1=tomm(bb.GetBottom()) - OY,
                         cx=tomm(p.GetPosition().x) - OX, cy=tomm(p.GetPosition().y) - OY))
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        vias.append(dict(kind="via", net=t.GetNetname(), x=tomm(t.GetPosition().x) - OX, y=tomm(t.GetPosition().y) - OY, obj=t))
    elif t.GetLayer() in LAYERS:
        segs.append(dict(kind="seg", net=t.GetNetname(), L=LAYERS.index(t.GetLayer()), x1=tomm(t.GetStart().x) - OX, y1=tomm(t.GetStart().y) - OY,
                         x2=tomm(t.GetEnd().x) - OX, y2=tomm(t.GetEnd().y) - OY, w=tomm(t.GetWidth()), obj=t))

def add_via(net, x, y):
    v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))); v.SetNet(b.FindNet(net))
    v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetDrill(mm(VIA_DRILL))
    try: v.SetWidth(pcbnew.F_Cu, mm(VIA_D))
    except TypeError: v.SetWidth(mm(VIA_D))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
    vias.append(dict(kind="via", net=net, x=x, y=y, obj=v))
def add_track(net, L, x1, y1, x2, y2, w):
    if math.hypot(x2 - x1, y2 - y1) < 1e-4: return
    t = pcbnew.PCB_TRACK(b); t.SetStart(pcbnew.VECTOR2I(mm(OX + x1), mm(OY + y1))); t.SetEnd(pcbnew.VECTOR2I(mm(OX + x2), mm(OY + y2)))
    t.SetWidth(mm(w)); t.SetLayer(LAYERS[L]); t.SetNet(b.FindNet(net)); b.Add(t)
    segs.append(dict(kind="seg", net=net, L=L, x1=x1, y1=y1, x2=x2, y2=y2, w=w, obj=t))
def lift(s):
    b.Remove(s["obj"]); segs.remove(s)

if sys.argv[1] == "repair":
    d = json.load(open(sys.argv[2])); n = 0
    for x in d.get("violations", []):
        if x["type"] not in ("shorting_items", "clearance", "tracks_crossing", "hole_clearance"): continue
        cands = [i for i in x["items"] if i["description"].startswith("Track")]
        if not cands: continue
        cands.sort(key=lambda i: any(("[%s]" % p) in i["description"] for p in POWER + ("GND",)))
        i = cands[0]
        m = re.match(r"Track \[([^\]]*)\] on (\S+), length ([\d.]+)", i["description"])
        net, lay, ln = m.group(1), m.group(2), float(m.group(3))
        px, py = i["pos"]["x"] - OX, i["pos"]["y"] - OY
        best = None
        for s in segs:
            if s["net"] != net or s["obj"].GetLayerName() != lay: continue
            if abs(math.hypot(s["x2"] - s["x1"], s["y2"] - s["y1"]) - ln) > 0.002: continue
            dd = min(math.hypot(s["x1"] - px, s["y1"] - py), math.hypot(s["x2"] - px, s["y2"] - py))
            if best is None or dd < best[0]: best = (dd, s)
        if best and best[0] < 5:
            lift(best[1]); n += 1
    print("offending track segments removed:", n)
    pcbnew.ZONE_FILLER(b).Fill(b.Zones()); pcbnew.SaveBoard(PCB, b); sys.exit(0)

# ------------------------------------------------------------------ obstacle maps
def raster_rect(arr, x0, y0, x1, y1, r):
    i0, i1 = max(0, int((x0 - r) / G) - 1), min(NX, int((x1 + r) / G) + 2)
    j0, j1 = max(0, int((y0 - r) / G) - 1), min(NY, int((y1 + r) / G) + 2)
    if i0 >= i1 or j0 >= j1: return
    xs = (np.arange(i0, i1) + 0.5) * G; ys = (np.arange(j0, j1) + 0.5) * G
    dx = np.maximum(np.maximum(x0 - xs, xs - x1), 0)[None, :]; dy = np.maximum(np.maximum(y0 - ys, ys - y1), 0)[:, None]
    arr[j0:j1, i0:i1] |= (dx * dx + dy * dy) <= r * r
def raster_seg(arr, x1, y1, x2, y2, r):
    i0, i1 = max(0, int((min(x1, x2) - r) / G) - 1), min(NX, int((max(x1, x2) + r) / G) + 2)
    j0, j1 = max(0, int((min(y1, y2) - r) / G) - 1), min(NY, int((max(y1, y2) + r) / G) + 2)
    if i0 >= i1 or j0 >= j1: return
    xs = ((np.arange(i0, i1) + 0.5) * G)[None, :]; ys = ((np.arange(j0, j1) + 0.5) * G)[:, None]
    dx, dy = x2 - x1, y2 - y1; l2 = dx * dx + dy * dy
    t = np.zeros((j1 - j0, i1 - i0)) if l2 == 0 else np.clip(((xs - x1) * dx + (ys - y1) * dy) / l2, 0, 1)
    arr[j0:j1, i0:i1] |= ((xs - (x1 + t * dx)) ** 2 + (ys - (y1 + t * dy)) ** 2) <= r * r
def build_maps(net, tw):
    blk = np.zeros((3, NY, NX), bool); via = np.zeros((NY, NX), bool)
    rt, rv = CLR + tw / 2, CLR + VIA_D / 2
    e = int(0.45 / G) + 1
    for a in (blk[0], blk[1], blk[2], via):
        a[:e, :] = True; a[-e:, :] = True; a[:, :e] = True; a[:, -e:] = True
    for p in pads:
        own = p["net"] == net and net != ""
        if p["thr"]:
            if not own:
                for L in range(3): raster_rect(blk[L], p["x0"], p["y0"], p["x1"], p["y1"], max(rt, 0.27 + tw / 2))
            raster_rect(via, p["x0"], p["y0"], p["x1"], p["y1"], rv + 0.1)
        else:
            if not own: raster_rect(blk[0], p["x0"], p["y0"], p["x1"], p["y1"], rt)
            raster_rect(via, p["x0"], p["y0"], p["x1"], p["y1"], rv if not own else 0.12 + VIA_D / 2)
    for s in segs:
        if s["net"] == net: continue
        raster_seg(blk[s["L"]], s["x1"], s["y1"], s["x2"], s["y2"], rt + s["w"] / 2)
        raster_seg(via, s["x1"], s["y1"], s["x2"], s["y2"], rv + s["w"] / 2)
    for v in vias:
        if v["net"] != net:
            for L in range(3): raster_seg(blk[L], v["x"], v["y"], v["x"], v["y"], rt + VIA_D / 2 + 0.05)
        raster_seg(via, v["x"], v["y"], v["x"], v["y"], 0.3 + VIA_D / 2 + 0.3)
    return blk, via

# ------------------------------------------------------------------ connectivity of one net
def dps(px, py, s):
    dx, dy = s["x2"] - s["x1"], s["y2"] - s["y1"]; l2 = dx * dx + dy * dy
    tt = 0 if l2 == 0 else max(0, min(1, ((px - s["x1"]) * dx + (py - s["y1"]) * dy) / l2))
    return math.hypot(px - (s["x1"] + tt * dx), py - (s["y1"] + tt * dy))
def touch(a, c):
    ka, kc = a["kind"], c["kind"]
    if ka > kc: a, c, ka, kc = c, a, kc, ka
    if ka == "pad" and kc == "seg":
        if not a["thr"] and c["L"] != 0: return False
        return any(a["x0"] - 0.02 <= x <= a["x1"] + 0.02 and a["y0"] - 0.02 <= y <= a["y1"] + 0.02 for x, y in ((c["x1"], c["y1"]), (c["x2"], c["y2"])))
    if ka == "pad" and kc == "via": return a["x0"] <= c["x"] <= a["x1"] and a["y0"] <= c["y"] <= a["y1"]
    if ka == "seg" and kc == "seg":
        if a["L"] != c["L"]: return False
        return (min(dps(a["x1"], a["y1"], c), dps(a["x2"], a["y2"], c)) <= c["w"] / 2 + 0.005 or
                min(dps(c["x1"], c["y1"], a), dps(c["x2"], c["y2"], a)) <= a["w"] / 2 + 0.005)
    if ka == "seg" and kc == "via": return dps(c["x"], c["y"], a) <= 0.25
    return False
def clusters(net, with_strays=False):
    items = [p for p in pads if p["net"] == net] + [s for s in segs if s["net"] == net] + [v for v in vias if v["net"] == net]
    parent = list(range(len(items)))
    def find(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]; i = parent[i]
        return i
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            if find(i) != find(j) and touch(items[i], items[j]): parent[find(i)] = find(j)
    groups = collections.defaultdict(list)
    for i, it in enumerate(items): groups[find(i)].append(it)
    # only clusters that contain a pad matter; pad-less leftovers are returned separately
    if with_strays:
        return ([g for g in groups.values() if any(it["kind"] == "pad" for it in g)], [g for g in groups.values() if not any(it["kind"] == "pad" for it in g)])
    return [g for g in groups.values() if any(it["kind"] == "pad" for it in g)]
def prune(net):
    """Delete copper of this net that is attached to no pad (leftovers from lifted segments)."""
    n = 0
    for g in clusters(net, True)[1]:
        for it in g:
            b.Remove(it["obj"]); (segs if it["kind"] == "seg" else vias).remove(it); n += 1
    return n

def cells_of(it):
    out = set()
    if it["kind"] == "pad":
        for L in (range(3) if it["thr"] else [0]):
            for i in range(int(it["x0"] / G), int(it["x1"] / G) + 1):
                for j in range(int(it["y0"] / G), int(it["y1"] / G) + 1):
                    cx, cy = (i + 0.5) * G, (j + 0.5) * G
                    if it["x0"] + 0.1 <= cx <= it["x1"] - 0.1 and it["y0"] + 0.1 <= cy <= it["y1"] - 0.1: out.add((L, j, i))
            out.add((L, int(it["cy"] / G), int(it["cx"] / G)))
    elif it["kind"] == "seg":
        n = max(1, int(math.hypot(it["x2"] - it["x1"], it["y2"] - it["y1"]) / (G / 2)))
        for k in range(n + 1):
            out.add((it["L"], int((it["y1"] + (it["y2"] - it["y1"]) * k / n) / G), int((it["x1"] + (it["x2"] - it["x1"]) * k / n) / G)))
    else:
        for L in range(3): out.add((L, int(it["y"] / G), int(it["x"] / G)))
    return out
def anchor(it, cell):
    L, j, i = cell; cx, cy = (i + 0.5) * G, (j + 0.5) * G
    if it["kind"] == "pad": return (it["cx"], it["cy"])
    if it["kind"] == "via": return (it["x"], it["y"])
    dx, dy = it["x2"] - it["x1"], it["y2"] - it["y1"]; l2 = dx * dx + dy * dy
    t = 0 if l2 == 0 else max(0, min(1, ((cx - it["x1"]) * dx + (cy - it["y1"]) * dy) / l2))
    return (it["x1"] + t * dx, it["y1"] + t * dy)

MOVES = [(0, 1, 1.0), (0, -1, 1.0), (1, 0, 1.0), (-1, 0, 1.0), (1, 1, 1.4142), (1, -1, 1.4142), (-1, 1, 1.4142), (-1, -1, 1.4142)]
def astar(blk, via, start, goal, limit=2_500_000):
    gj = [c[1] for c in goal]; gi = [c[2] for c in goal]
    gj0, gj1, gi0, gi1 = min(gj), max(gj), min(gi), max(gi)
    def h(j, i):
        dj = max(gj0 - j, 0, j - gj1); di = max(gi0 - i, 0, i - gi1)
        return max(dj, di) + 0.4142 * min(dj, di)
    g = {}; prev = {}; pq = []
    for c in start:
        g[c] = 0.0; heapq.heappush(pq, (h(c[1], c[2]), 0.0, c))
    n = 0
    while pq:
        f, gc, c = heapq.heappop(pq)
        if gc > g.get(c, 1e18): continue
        if c in goal:
            path = [c]
            while c in prev: c = prev[c]; path.append(c)
            return path[::-1]
        n += 1
        if n > limit: return None
        L, j, i = c
        for dj, di, cost in MOVES:
            jj, ii = j + dj, i + di
            if not (0 <= jj < NY and 0 <= ii < NX): continue
            nc = (L, jj, ii)
            if blk[L, jj, ii] and nc not in goal: continue
            if dj and di and (blk[L, j, ii] or blk[L, jj, i]): continue
            ng = gc + cost
            if ng < g.get(nc, 1e18):
                g[nc] = ng; prev[nc] = c; heapq.heappush(pq, (ng + h(jj, ii), ng, nc))
        if not via[j, i]:
            for L2 in range(3):
                if L2 == L: continue
                nc = (L2, j, i)
                if blk[L2, j, i] and nc not in goal: continue
                ng = gc + 40.0
                if ng < g.get(nc, 1e18):
                    g[nc] = ng; prev[nc] = c; heapq.heappush(pq, (ng + h(j, i), ng, nc))
    return None

def emit(net, pts, w):
    k = 0
    while k < len(pts) - 1:
        x1, y1, L = pts[k]
        if pts[k + 1][2] != L:
            add_via(net, x1, y1); k += 1; continue
        m2 = k + 1
        while m2 + 1 < len(pts) and pts[m2 + 1][2] == L:
            dx1, dy1 = pts[m2][0] - pts[m2 - 1][0], pts[m2][1] - pts[m2 - 1][1]
            dx2, dy2 = pts[m2 + 1][0] - pts[m2][0], pts[m2 + 1][1] - pts[m2][1]
            if abs(dx1 * dy2 - dy1 * dx2) > 1e-9 or dx1 * dx2 + dy1 * dy2 <= 0: break
            m2 += 1
        add_track(net, L, x1, y1, pts[m2][0], pts[m2][1], w); k = m2

def near_segs(q, exclude, radius, allow_power):
    out = []
    for s in segs:
        if s["L"] != 0 or s["net"] in exclude or s["net"] == "GND": continue
        if s["net"] in POWER and not allow_power: continue
        n = max(1, int(math.hypot(s["x2"] - s["x1"], s["y2"] - s["y1"]) / 0.1))
        for k2 in range(n + 1):
            x = s["x1"] + (s["x2"] - s["x1"]) * k2 / n; y = s["y1"] + (s["y2"] - s["y1"]) * k2 / n
            if math.hypot(max(q["x0"] - x, 0, x - q["x1"]), max(q["y0"] - y, 0, y - q["y1"])) < radius:
                out.append(s); break
    return out

lift_count = collections.Counter()
def join_net(net, queue):
    """Route until all pads of this net are one cluster. Returns list of failures."""
    w = width_of(net); fails = []
    for _ in range(12):
        cl = clusters(net)
        if len(cl) <= 1: return fails
        cl.sort(key=len)
        A = cl[0]; rest = [it for g in cl[1:] for it in g]
        blk, via = build_maps(net, w)
        cmA = {}
        for it in A:
            for c in cells_of(it): cmA.setdefault(c, it)
        cmB = {}
        for it in rest:
            for c in cells_of(it): cmB.setdefault(c, it)
        start = {c for c in cmA if not blk[c]} or set(cmA)
        path = astar(blk, via, start, set(cmB))
        if not path and lift_count[net] < 3:
            # the small cluster is boxed in: lift foreign top-layer segments crowding its SMD pads, and re-route those nets afterwards
            lift_count[net] += 1
            targets = [it for it in A if it["kind"] == "pad" and not it["thr"]]
            if lift_count[net] >= 2:        # second try: also clear around the other small clusters' pads
                targets += [it for g in cl[1:] if len(g) <= 4 for it in g if it["kind"] == "pad" and not it["thr"]]
            for q in targets:
                near = near_segs(q, {net}, 1.1 if lift_count[net] < 3 else 1.7, False)
                for s in near:
                    queue.add(s["net"]); lift(s)
                if near: print(f"  lifted {len(near)} segment(s) of {sorted(set(s['net'] for s in near))} around {q['ref']}.{q['num']} for {net}")
            continue
        if not path:
            fails.append((net, [f"{it['ref']}.{it['num']}" for it in A if it["kind"] == "pad"])); return fails
        pts = [((c[2] + 0.5) * G, (c[1] + 0.5) * G, c[0]) for c in path]
        ax, ay = anchor(cmA[path[0]], path[0]); bx, by = anchor(cmB[path[-1]], path[-1])
        if math.hypot(ax - pts[0][0], ay - pts[0][1]) < 0.12: pts[0] = (ax, ay, pts[0][2])
        else: pts.insert(0, (ax, ay, pts[0][2]))
        if math.hypot(bx - pts[-1][0], by - pts[-1][1]) < 0.12: pts[-1] = (bx, by, pts[-1][2])
        else: pts.append((bx, by, pts[-1][2]))
        emit(net, pts, w)
        print(f"  joined {net} ({len(path) * G:.1f} mm)")
    return fails

def join_all(nets):
    queue = set(nets); fails = []; rounds = collections.Counter()
    while queue:
        net = sorted(queue)[0]; queue.discard(net)
        rounds[net] += 1
        if rounds[net] > 4:
            fails.append((net, "gave up")); continue
        fails = [f for f in fails if f[0] != net] + join_net(net, queue)
    return fails
def prune_all():
    n = sum(prune(net) for net in signal_nets)
    if n: print("leftover stubs removed:", n)

def stitch_pad(p, blk, via):
    start = cells_of(p)
    g = {c: 0.0 for c in start}; prev = {}; pq = [(0.0, c) for c in start]; heapq.heapify(pq); found = None; n = 0
    while pq:
        gc, c = heapq.heappop(pq)
        if gc > g.get(c, 1e18): continue
        L, j, i = c
        if not blk[0, j, i] and not via[j, i]:
            found = c; break
        n += 1
        if n > 400000: break
        for dj, di, cost in MOVES:
            jj, ii = j + dj, i + di
            if not (0 <= jj < NY and 0 <= ii < NX) or blk[0, jj, ii]: continue
            if dj and di and (blk[0, j, ii] or blk[0, jj, i]): continue
            nc = (0, jj, ii); ng = gc + cost
            if ng < g.get(nc, 1e18):
                g[nc] = ng; prev[nc] = c; heapq.heappush(pq, (ng, nc))
    if not found: return None
    path = [found]; c = found
    while c in prev: c = prev[c]; path.append(c)
    path = path[::-1]
    pts = [(p["cx"], p["cy"], 0)] + [((c[2] + 0.5) * G, (c[1] + 0.5) * G, 0) for c in path]
    emit("GND", pts, 0.2)
    add_via("GND", pts[-1][0], pts[-1][1]); raster_seg(via, pts[-1][0], pts[-1][1], pts[-1][0], pts[-1][1], 0.3 + VIA_D / 2 + 0.3)
    return math.hypot(pts[-1][0] - p["cx"], pts[-1][1] - p["cy"])

def ground_pass():
    """Tie every top-layer ground pour patch that has no via or through-hole ground pin down to the plane."""
    pcbnew.ZONE_FILLER(b).Fill(b.Zones())
    blk, via = build_maps("GND", 0.2)
    gsmd = [q for q in pads if q["net"] == "GND" and not q["thr"]]; gtht = [q for q in pads if q["net"] == "GND" and q["thr"]]
    added, stuck, lifted_nets = 0, [], set()
    for z in b.Zones():
        if z.GetLayer() != pcbnew.F_Cu: continue
        ps = z.GetFilledPolysList(pcbnew.F_Cu)
        for k in range(ps.OutlineCount()):
            def inside(x, y): return ps.Contains(pcbnew.VECTOR2I(mm(OX + x), mm(OY + y)), k)
            bb = ps.Outline(k).BBox()
            x0, y0, x1, y1 = tomm(bb.GetLeft()) - OX, tomm(bb.GetTop()) - OY, tomm(bb.GetRight()) - OX, tomm(bb.GetBottom()) - OY
            def inbb(x, y): return x0 - 0.3 <= x <= x1 + 0.3 and y0 - 0.3 <= y <= y1 + 0.3
            if any(v["net"] == "GND" and inbb(v["x"], v["y"]) and any(inside(v["x"] + dx, v["y"] + dy) for dx, dy in ((0, 0), (0.28, 0), (-0.28, 0), (0, 0.28), (0, -0.28))) for v in vias): continue
            tied = False
            for q in gtht:
                if inbb(q["cx"], q["cy"]):
                    r = (q["x1"] - q["x0"]) / 2 + 0.3
                    if any(inside(q["cx"] + r * math.cos(a), q["cy"] + r * math.sin(a)) for a in [i * math.pi / 8 for i in range(16)]): tied = True; break
            if tied: continue
            mine = [q for q in gsmd if inbb(q["cx"], q["cy"]) and inside(q["cx"], q["cy"])]
            if not mine: continue                      # a patch with no pad on it does not matter
            best = None
            for j in range(max(0, int(y0 / G)), min(NY, int(y1 / G) + 1)):
                for i in range(max(0, int(x0 / G)), min(NX, int(x1 / G) + 1)):
                    if via[j, i]: continue
                    cx, cy = (i + 0.5) * G, (j + 0.5) * G
                    if all(inside(cx + dx, cy + dy) for dx, dy in ((0, 0), (0.3, 0), (-0.3, 0), (0, 0.3), (0, -0.3))):
                        best = (cx, cy); break
                if best: break
            if best:
                add_via("GND", best[0], best[1]); raster_seg(via, best[0], best[1], best[0], best[1], 0.3 + VIA_D / 2 + 0.3); added += 1; continue
            ok = any(stitch_pad(q, blk, via) is not None for q in mine[:1]) if mine else False
            if not ok:
                for allow_power in (False, True):
                    for q in mine:
                        near = near_segs(q, set(), 1.0, allow_power)
                        if not near: continue
                        for s in near:
                            lifted_nets.add(s["net"]); lift(s)
                        blk, via = build_maps("GND", 0.2)
                        if stitch_pad(q, blk, via) is not None:
                            ok = True; print(f"  lifted {len(near)} segment(s) of {sorted(set(s['net'] for s in near))} to ground {q['ref']}.{q['num']}"); break
                    if ok: break
            if ok: added += 1
            else: stuck.append([f"{q['ref']}.{q['num']}" for q in mine])
    print("ground patches tied down:", added, "| still isolated:", stuck)
    return lifted_nets, stuck

signal_nets = sorted({p["net"] for p in pads if p["net"] and p["net"] != "GND"})
fails = join_all(signal_nets)
print("signal nets joined; failures:", fails)
for rnd in range(3):
    lifted, stuck = ground_pass()
    if not lifted: break
    f2 = join_all(lifted)
    fails = [f for f in fails if f[0] not in lifted] + f2
    print("re-routed lifted nets; failures:", f2)
prune_all()
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
pcbnew.SaveBoard(PCB, b)
print("FINAL signal failures:", fails)
