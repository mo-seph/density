"""For every BOM line: compare JLCPCB's own footprint (EasyEDA API json) with the KiCad footprint on the board, by pad number.
Reports package, pad count, the rotation JLCPCB needs added, where JLCPCB's origin sits, worst pad-centre mismatch, and what the placement file gives."""
import pcbnew, sys, json, math, csv
b = pcbnew.LoadBoard(sys.argv[1]); E = sys.argv[2]; BOM = sys.argv[3]; CPL = sys.argv[4]
mm = pcbnew.ToMM
def ee(c):
    r = json.load(open(f"{E}/{c}.json"))["result"]; d = r["packageDetail"]["dataStr"]
    ox, oy = float(d["head"]["x"]), float(d["head"]["y"]); U = 0.254; pts = []
    for s in d["shape"]:
        f = s.split("~")
        if f[0] == "PAD": pts.append(((float(f[2])-ox)*U, (float(f[3])-oy)*U, f[8], float(f[4])*U, float(f[5])*U, float(f[11] or 0)))
    return r["packageDetail"]["title"], pts
def rot(p, a):
    c, s = math.cos(math.radians(a)), math.sin(math.radians(a)); return (p[0]*c + p[1]*s, -p[0]*s + p[1]*c)
cpl = {r["Designator"]: r for r in csv.DictReader(open(CPL))}
pos = {}
print(f"{'ref':5} {'JLCPCB package':40} pads  add-rot  JLC origin (fp mm)  worst pad err  CPL rot - KiCad rot   CPL pos - JLC origin")
for row in csv.DictReader(open(BOM)):
    ref = row["Designator"].split(",")[0]; c = row["LCSC Part #"]
    fp = b.FindFootprintByReference(ref); th = fp.GetOrientationDegrees(); o = fp.GetPosition(); a = math.radians(th)
    kp = []
    for p in fp.Pads():
        if p.GetNumber() == "" : continue
        d = (mm(p.GetPosition().x - o.x), mm(p.GetPosition().y - o.y))
        kp.append((d[0]*math.cos(a) - d[1]*math.sin(a), d[0]*math.sin(a) + d[1]*math.cos(a), p.GetNumber(), mm(p.GetSizeX()), mm(p.GetSizeY())))
    title, ep = ee(c)
    knum = {}; [knum.setdefault(k[2], []).append(k) for k in kp]
    best = None
    for phi in (0, 90, 180, 270):
        r = [rot(e, phi) + (e[2],) for e in ep]
        # translation = mean offset over pads matched by number (nearest among same-numbered pads after a first guess)
        t = (0.0, 0.0)
        for _ in range(3):
            pairs = []
            for e in r:
                cands = knum.get(e[2]) or kp
                k = min(cands, key=lambda k: math.hypot(e[0]+t[0]-k[0], e[1]+t[1]-k[1])); pairs.append((e, k))
            t = (sum(k[0]-e[0] for e, k in pairs)/len(pairs), sum(k[1]-e[1] for e, k in pairs)/len(pairs))
        err = max(math.hypot(e[0]+t[0]-k[0], e[1]+t[1]-k[1]) for e, k in pairs)
        named = all(e[2] in knum for e in r)
        if best is None or err < best[0] - 1e-3: best = (err, phi, t, named)
    err, phi, t, named = best
    # where does the CPL put this part, and at what rotation?
    cr = cpl[ref]; cx, cy = float(cr["Mid X"][:-2]), -float(cr["Mid Y"][:-2])
    jx = mm(o.x) + (t[0]*math.cos(a) + t[1]*math.sin(a)); jy = mm(o.y) + (-t[0]*math.sin(a) + t[1]*math.cos(a))
    drot = (float(cr["Rotation"]) - th) % 360
    flag = "" if (abs(drot - phi) < 0.5 or (len(ep) == 2 and abs((drot - phi) % 180) < 0.5)) and math.hypot(cx-jx, cy-jy) < 0.15 and err < 0.35 and len(ep) == len(kp) else "  <-- CHECK"
    print(f"{ref:5} {title[:40]:40} {len(ep):2d}/{len(kp):<2d}  {phi:4d}    ({t[0]:6.2f},{t[1]:6.2f})     {err:5.2f} mm      {drot:5.0f}               ({cx-jx:+.2f},{cy-jy:+.2f}){'' if named else '  (pads matched by position)'}{flag}")
