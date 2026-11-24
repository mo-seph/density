import pcbnew, sys, json, math, itertools
b = pcbnew.LoadBoard(sys.argv[1]); E = sys.argv[2]
def ee(c):
    d = json.load(open(f"{E}/{c}.json"))["result"]["packageDetail"]["dataStr"]
    ox, oy = float(d["head"]["x"]), float(d["head"]["y"]); U = 0.254; pts = []
    for s in d["shape"]:
        f = s.split("~")
        if f[0] == "PAD": pts.append(((float(f[2])-ox)*U, (float(f[3])-oy)*U, f[8]))
        elif f[0] == "HOLE": pts.append(((float(f[1])-ox)*U, (float(f[2])-oy)*U, "hole"))
    return pts
def rot(p, a):
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a)); return (p[0]*c + p[1]*s, -p[0]*s + p[1]*c)
for ref, c in [("RV1", "C202373"), ("J3", "C19189088"), ("ENC1", "C470754"), ("SW1", "C127509"), ("D1", "C7279958"), ("RV17", "C370993")]:
    fp = b.FindFootprintByReference(ref); th = fp.GetOrientationDegrees(); o = fp.GetPosition()
    kp = []
    for p in fp.Pads():
        d = (pcbnew.ToMM(p.GetPosition().x - o.x), pcbnew.ToMM(p.GetPosition().y - o.y)); a = math.radians(th)
        kp.append((d[0]*math.cos(a) - d[1]*math.sin(a), d[0]*math.sin(a) + d[1]*math.cos(a), p.GetNumber() or "hole"))
    ep = ee(c); best = None
    for phi in (0, 90, 180, 270):
        r = [rot(e, phi) + (e[2],) for e in ep]
        # candidate translations: align each ee point with each kicad point
        for e0 in r:
            for k0 in kp:
                t = (k0[0]-e0[0], k0[1]-e0[1]); err = 0; names = []
                for e in r:
                    dd = min(((math.hypot(e[0]+t[0]-k[0], e[1]+t[1]-k[1]), k[2]) for k in kp)); err += dd[0]; names.append(f"{e[2]}->{dd[1]}")
                if best is None or err < best[0] - 1e-6: best = (err, phi, t, names)
    err, phi, t, names = best
    print(f"{ref:5} {fp.GetFPID().GetLibItemName().wx_str()[:38]:38} kicad rot {th:5.0f} | add rot {phi:3d} | JLC origin in footprint coords ({t[0]:.2f}, {t[1]:.2f}) | residual {err:.2f} mm | {' '.join(names)}")
    print("      kicad pads:", [(round(k[0], 2), round(k[1], 2), k[2]) for k in kp])
