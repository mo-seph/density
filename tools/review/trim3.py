"""Tidy dead-end copper without changing connectivity:
 - a track with a free end is cut back to the last junction on it, or removed if there is none
 - a via that only touches one copper layer is removed
 - listed endpoint mismatches are snapped together
usage: trim3.py <board>"""
import pcbnew, sys, math
PCB = sys.argv[1]
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
SNAP = [("ADC_MOSI", (105.15, 75.95), (105.15, 75.85))]
# leftovers that sit inside a via pad or on top of another track, so the generic rules see them as attached
DROP = [("BTN5", (74.45, 80.25), (74.55, 80.35)), ("POT1", (100.65, 114.45), (100.55, 114.45)), ("BTN5", (75.15, 80.95), (75.15, 81.25))]
def proj(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return u, math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
def D(p, q): return math.hypot(p[0]-q[0], p[1]-q[1])
b.BuildConnectivity(); base = b.GetConnectivity().GetUnconnectedCount(False); print("unconnected before:", base)
zone_nets = {z.GetNetname() for z in b.Zones()}
allpads = [p for fp in b.GetFootprints() for p in fp.Pads()]
obj = {t.m_Uuid.AsString(): t for t in b.GetTracks()}
kill = []; moves = []
for net in sorted({t.GetNetname() for t in obj.values()} - zone_nets):
    pads = [p for p in allpads if p.GetNetname() == net]
    def onpad(p, lay): return any(pd.IsOnLayer(lay) and pd.HitTest(pcbnew.VECTOR2I(MM(p[0]), MM(p[1])), 0) for pd in pads)
    tr = {}; vi = {}
    for u, t in obj.items():
        if t.GetNetname() != net: continue
        if t.GetClass() == "PCB_VIA":
            q = (mm(t.GetPosition().x), mm(t.GetPosition().y)); vi[u] = dict(q=q, r=mm(t.GetWidth(pcbnew.F_Cu))/2, pad=onpad(q, pcbnew.F_Cu) or onpad(q, pcbnew.B_Cu))
        elif t.GetClass() == "PCB_TRACK":
            tr[u] = dict(a=(mm(t.GetStart().x), mm(t.GetStart().y)), c=(mm(t.GetEnd().x), mm(t.GetEnd().y)), w=mm(t.GetWidth()), lay=t.GetLayer(), name=t.GetLayerName())
    for n_, frm, to in SNAP:
        if n_ == net:
            for u, t in tr.items():
                for k in ("a", "c"):
                    if D(t[k], frm) < 0.011: t[k] = to; moves.append((u, k, to)); print(f"  {net}: snapped end {frm} -> {to}")
    for n_, a_, c_ in DROP:
        if n_ == net:
            for u, t in list(tr.items()):
                if (D(t["a"], a_) < 0.011 and D(t["c"], c_) < 0.011) or (D(t["a"], c_) < 0.011 and D(t["c"], a_) < 0.011): tr.pop(u); kill.append(u); print(f"  {net}: removed leftover {t['name']} {a_}->{c_}")
    def attached(u, p):
        t = tr[u]
        if onpad(p, t["lay"]): return True
        if any(D(p, v["q"]) <= v["r"] for v in vi.values()): return True
        return any(k != u and o["lay"] == t["lay"] and proj(p, o["a"], o["c"])[1] <= o["w"]/2 for k, o in tr.items())
    while True:
        hit = False
        for u, t in list(tr.items()):
            fa, fc = attached(u, t["a"]), attached(u, t["c"])
            if fa and fc: continue
            if not fa and not fc: tr.pop(u); kill.append(u); print(f"  {net}: removed floating track {t['name']} {t['a']}->{t['c']}"); hit = True; break
            keep, free = ("a", "c") if fa else ("c", "a")
            best = 0.0; bq = None
            for k, o in tr.items():
                if k == u or o["lay"] != t["lay"]: continue
                for e in (o["a"], o["c"]):
                    uu, dd = proj(e, t[keep], t[free])
                    if dd <= 0.011 and 0.001 < uu < 0.999 and uu > best: best, bq = uu, e
            for v in vi.values():
                uu, dd = proj(v["q"], t[keep], t[free])
                if dd <= 0.011 and 0.001 < uu < 0.999 and uu > best: best, bq = uu, v["q"]
            if bq is None:
                tr.pop(u); kill.append(u); print(f"  {net}: removed dead-end track {t['name']} ({t['a'][0]:.2f},{t['a'][1]:.2f})->({t['c'][0]:.2f},{t['c'][1]:.2f})")
            else:
                print(f"  {net}: cut tail of {t['name']} track back from ({t[free][0]:.2f},{t[free][1]:.2f}) to junction ({bq[0]:.2f},{bq[1]:.2f})")
                t[free] = bq; moves.append((u, free, bq))
            hit = True; break
        if hit: continue
        for u, v in list(vi.items()):
            lays = {o["lay"] for o in tr.values() if proj(v["q"], o["a"], o["c"])[1] <= o["w"]/2 + v["r"]}
            if len(lays) <= 1 and not v["pad"]:
                vi.pop(u); kill.append(u); print(f"  {net}: removed via at ({v['q'][0]:.2f},{v['q'][1]:.2f}) that only touched {len(lays)} layer"); hit = True; break
        if not hit: break
for u, k, q in moves:
    if u in kill: continue
    v = pcbnew.VECTOR2I(MM(q[0]), MM(q[1])); (obj[u].SetStart if k == "a" else obj[u].SetEnd)(v)
for u in kill: b.Remove(obj[u])
b.BuildConnectivity(); now = b.GetConnectivity().GetUnconnectedCount(False)
print("removed", len(kill), "items, adjusted", len(moves), "ends; unconnected after:", now)
if now != base: print("!! connectivity changed - not saving"); sys.exit(1)
pcbnew.SaveBoard(PCB, b); print("saved")
