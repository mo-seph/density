import json, sys
for c in sys.argv[2:]:
    d = json.load(open(f"{sys.argv[1]}/{c}.json"))
    r = d.get("result") or {}
    pk = r.get("packageDetail") or {}
    ds = pk.get("dataStr") or {}
    head = ds.get("head", {})
    ox, oy = float(head.get("x", 0)), float(head.get("y", 0))
    print("==", c, r.get("title"), "| package", pk.get("title"), "| origin", ox, oy, "| bbox", ds.get("BBox"))
    U = 0.254  # 10 mil units -> mm
    for s in ds.get("shape", []):
        f = s.split("~")
        if f[0] == "PAD":
            # PAD~shape~x~y~w~h~layer~net~number~holeR~points~rot~id~holeLen...
            print("   pad %-4s %-7s at (%.2f, %.2f) size %.2fx%.2f hole r %.2f rot %s slot %s" % (f[8], f[1], (float(f[2])-ox)*U, (float(f[3])-oy)*U, float(f[4])*U, float(f[5])*U, float(f[9])*U, f[11], f[13] if len(f) > 13 else ""))
        elif f[0] == "HOLE":
            print("   hole at (%.2f, %.2f) d %.2f" % ((float(f[1])-ox)*U, (float(f[2])-oy)*U, float(f[3])*2*U))
        elif f[0] == "SVGNODE":
            try:
                a = json.loads(f[1])["attrs"]; print("   3D model origin", a.get("c_origin"), "rot", a.get("c_rotation"), "z", a.get("z"), "title", a.get("title"))
            except Exception as e: print("   svgnode?", e)
