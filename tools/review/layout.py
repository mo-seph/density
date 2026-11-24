import pcbnew, sys, math, collections
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.ToMM; MM = pcbnew.FromMM
def Pt(v): return (round(mm(v.x), 3), round(mm(v.y), 3))
def segd(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
T = [t for t in b.GetTracks() if t.GetClass() == "PCB_TRACK"]; V = [t for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
print("== A. power track widths (mm: total length) per layer")
for net in ("+5V", "+3V3", "AVDD", "GND"):
    w = collections.defaultdict(float)
    for t in T:
        if t.GetNetname() == net: w[(t.GetLayerName(), mm(t.GetWidth()))] += mm(t.GetLength())
    print("  ", net, {k: round(v, 1) for k, v in sorted(w.items())}, "vias", sum(1 for v in V if v.GetNetname() == net))
print("== B. +5V drop: resistor network from A1 pin 1 to every LED VDD pad")
RS = {"F.Cu": 0.00049, "B.Cu": 0.00049, "In1.Cu": 0.00098, "In2.Cu": 0.00098}
nodes = {}; G = collections.defaultdict(dict)
def node(p, lay): return nodes.setdefault((round(p[0], 2), round(p[1], 2), lay), len(nodes))
def link(i, j, r):
    if i == j: return
    g = 1.0/max(r, 1e-6); G[i][j] = G[i].get(j, 0) + g; G[j][i] = G[j].get(i, 0) + g
tr5 = [t for t in T if t.GetNetname() == "+5V"]
ends = collections.defaultdict(list)
for t in tr5:
    for p in (Pt(t.GetStart()), Pt(t.GetEnd())): ends[t.GetLayerName()].append(p)
for t in tr5:
    a, c, lay = Pt(t.GetStart()), Pt(t.GetEnd()), t.GetLayerName()
    # split at T-junction points lying on this track
    pts = [a, c] + [p for p in ends[lay] if segd(p, a, c) < 0.011 and p not in (a, c)]
    pts = sorted(set(pts), key=lambda p: math.hypot(p[0]-a[0], p[1]-a[1]))
    for p, q in zip(pts, pts[1:]): link(node(p, lay), node(q, lay), RS[lay] * math.hypot(p[0]-q[0], p[1]-q[1]) / mm(t.GetWidth()))
for v in V:
    if v.GetNetname() == "+5V":
        ls = [l for l in RS if any(k[2] == l and math.hypot(k[0]-mm(v.GetPosition().x), k[1]-mm(v.GetPosition().y)) < 0.26 for k in nodes)]
        ks = [i for k, i in list(nodes.items()) if math.hypot(k[0]-mm(v.GetPosition().x), k[1]-mm(v.GetPosition().y)) < 0.26]
        for i, j in zip(ks, ks[1:]): link(i, j, 0.001)
padnode = {}
for fp in b.GetFootprints():
    for pad in fp.Pads():
        if pad.GetNetname() != "+5V": continue
        ks = [i for k, i in list(nodes.items()) if pad.IsOnLayer(b.GetLayerID(k[2])) and pad.HitTest(pcbnew.VECTOR2I(MM(k[0]), MM(k[1])), 0)]
        if not ks: print("   pad without track node:", fp.GetReference(), pad.GetNumber()); continue
        for i, j in zip(ks, ks[1:]): link(i, j, 0.0005)
        padnode[(fp.GetReference(), pad.GetNumber())] = ks[0]
src = padnode[("A1", "1")]; n = len(nodes)
def solve(inj):
    # Gauss-Seidel on G v = i with v[src] = 0
    v = [0.0]*n
    for it in range(20000):
        dmax = 0
        for i in range(n):
            if i == src or not G[i]: continue
            s = sum(g*v[j] for j, g in G[i].items()); new = (s - inj.get(i, 0.0)) / sum(G[i].values())
            dmax = max(dmax, abs(new - v[i])); v[i] = new
        if dmax < 1e-7: break
    return v
leds = sorted((r for r in padnode if r[0].startswith("D")), key=lambda r: int(r[0][1:]))
for name, each in (("all 20 LEDs at 12 mA (dim white / one colour full)", 0.012), ("all 20 LEDs full white, 36 mA each = 0.72 A", 0.036)):
    v = solve({padnode[r]: each for r in leds})
    drops = {r[0]: -v[padnode[r]] for r in leds}
    worst = max(drops, key=drops.get)
    print(f"   {name}: worst drop {drops[worst]*1000:.0f} mV at {worst}; " + " ".join(f"{k}:{d*1000:.0f}" for k, d in drops.items()))
print("== C. decoupling: distance from each IC supply pin to nearest capacitor pad on the same net")
for fp in b.GetFootprints():
    if not fp.GetReference().startswith("U"): continue
    for pad in fp.Pads():
        if pad.GetNetname() in ("+5V", "+3V3", "AVDD"):
            best = min(((math.hypot(mm(q.GetPosition().x-pad.GetPosition().x), mm(q.GetPosition().y-pad.GetPosition().y)), f2.GetReference()) for f2 in b.GetFootprints() if f2.GetReference().startswith("C") for q in f2.Pads() if q.GetNetname() == pad.GetNetname()))
            print(f"   {fp.GetReference()}.{pad.GetNumber()} {pad.GetNetname()}: {best[1]} at {best[0]:.1f} mm")
print("== D. copper (not GND) within 3.4 mm of M3 hole centres on outer layers")
for fp in b.GetFootprints():
    if not fp.GetReference().startswith("H"): continue
    h = (mm(fp.GetPosition().x), mm(fp.GetPosition().y)); near = collections.Counter()
    for t in T:
        if t.GetNetname() != "GND" and t.GetLayerName() in ("F.Cu", "B.Cu"):
            d = segd(h, Pt(t.GetStart()), Pt(t.GetEnd())) - mm(t.GetWidth())/2
            if d < 3.4: near[(t.GetNetname(), t.GetLayerName())] = min(near.get((t.GetNetname(), t.GetLayerName()), 9), round(d, 2))
    for v in V:
        d = math.hypot(mm(v.GetPosition().x)-h[0], mm(v.GetPosition().y)-h[1]) - 0.25
        if v.GetNetname() != "GND" and d < 3.4: near[(v.GetNetname(), "via")] = round(d, 2)
    for f2 in b.GetFootprints():
        for q in f2.Pads():
            d = math.hypot(mm(q.GetPosition().x)-h[0], mm(q.GetPosition().y)-h[1])
            if f2.GetReference() != fp.GetReference() and d < 4.5: near[(f2.GetReference()+"."+q.GetNumber(), "pad centre")] = round(d, 2)
    print("  ", fp.GetReference(), (round(h[0]-50, 1), round(h[1]-40, 1)), dict(near))
print("== E. smallest annular rings")
rings = []
for fp in b.GetFootprints():
    for pad in fp.Pads():
        if pad.GetDrillSize().x > 0 and pad.GetAttribute() == pcbnew.PAD_ATTRIB_PTH:
            rings.append((round((min(mm(pad.GetSizeX()), mm(pad.GetSizeY())) - min(mm(pad.GetDrillSize().x), mm(pad.GetDrillSize().y)))/2, 3), fp.GetReference(), pad.GetNumber(), mm(pad.GetSizeX()), mm(pad.GetDrillSize().x)))
print("  ", sorted(rings)[:6]); print("   via sizes", collections.Counter((mm(v.GetWidth(pcbnew.F_Cu)), mm(v.GetDrillValue())) for v in V))
print("== F. zones")
for z in b.Zones():
    for lay in z.GetLayerSet().Seq():
        poly = z.GetFilledPolysList(lay)
        print("  ", z.GetNetname(), b.GetLayerName(lay), "filled outlines:", poly.OutlineCount(), "area mm2:", round(poly.Area()/1e12, 0), "holes in biggest:", poly.HoleCount(0) if poly.OutlineCount() else 0)
print("== W. analogue nets running beside fast digital nets on the same layer (gap < 0.35 mm), total parallel length mm")
ANA = ("MUXA", "ADC0", "ADC1", "BUFA", "BUFB", "FADER1", "PEDAL1", "PEDAL2", "PED1_TIP", "PED2_TIP")
DIG = ("SR_CLK", "SR_LOAD", "SR_DATA", "ADC_SCK", "ADC_MOSI", "ADC_MISO", "ADC_CS", "LED_DATA", "LED_5V", "LED_DIN", "MUX_S0", "MUX_S1", "MUX_S2", "MUX_S3", "SCL", "SDA")
par = collections.defaultdict(float)
for t in T:
    if t.GetNetname() not in ANA and not t.GetNetname().startswith("POT"): continue
    a, c = Pt(t.GetStart()), Pt(t.GetEnd()); L = math.hypot(a[0]-c[0], a[1]-c[1]); steps = max(1, int(L/0.2))
    for o in T:
        if (o.GetNetname() in DIG or o.GetNetname().startswith("LEDC")) and o.GetLayer() == t.GetLayer():
            oa, oc = Pt(o.GetStart()), Pt(o.GetEnd())
            for s in range(steps):
                p = (a[0] + (c[0]-a[0])*(s+0.5)/steps, a[1] + (c[1]-a[1])*(s+0.5)/steps)
                if segd(p, oa, oc) - 0.15 < 0.35: par[(t.GetNetname(), o.GetNetname())] += L/steps
for k, v in sorted(par.items(), key=lambda kv: -kv[1])[:14]: print("  ", k, round(v, 1))
