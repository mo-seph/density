import pcbnew, sys, math
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.ToMM
lay = sys.argv[2]; cx, cy, r = [float(v) for v in sys.argv[3:6]]
def segd(p, a, c):
    dx, dy = c[0]-a[0], c[1]-a[1]; L2 = dx*dx + dy*dy
    u = 0.0 if L2 == 0 else max(0.0, min(1.0, ((p[0]-a[0])*dx + (p[1]-a[1])*dy)/L2))
    return math.hypot(a[0]+u*dx-p[0], a[1]+u*dy-p[1])
L = b.GetLayerID(lay)
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        q = (mm(t.GetPosition().x), mm(t.GetPosition().y)); d = math.hypot(q[0]-cx, q[1]-cy)
        if d < r: print(f"VIA {t.GetNetname():9} ({q[0]:.2f},{q[1]:.2f}) dist {d:.2f}")
    elif t.GetLayer() == L:
        s = (mm(t.GetStart().x), mm(t.GetStart().y)); e = (mm(t.GetEnd().x), mm(t.GetEnd().y)); d = segd((cx, cy), s, e)
        if d < r: print(f"TRK {t.GetNetname():9} ({s[0]:.2f},{s[1]:.2f})->({e[0]:.2f},{e[1]:.2f}) w{mm(t.GetWidth())} dist {d:.2f}")
for fp in b.GetFootprints():
    for pad in fp.Pads():
        q = (mm(pad.GetPosition().x), mm(pad.GetPosition().y)); d = math.hypot(q[0]-cx, q[1]-cy)
        if d < r + 1 and pad.IsOnLayer(L): print(f"PAD {fp.GetReference()}.{pad.GetNumber()} {pad.GetNetname():9} ({q[0]:.2f},{q[1]:.2f}) {mm(pad.GetSizeX())}x{mm(pad.GetSizeY())} rot{pad.GetOrientationDegrees():.0f} dist {d:.2f}")
