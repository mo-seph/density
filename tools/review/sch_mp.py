"""Switch pots/fader/encoder to KiCad's stock symbols that have a mounting pin, and tie that pin to GND with a label."""
import re, sys, uuid
sys.path.insert(0, sys.argv[2]); from symx import block, sym
P = sys.argv[1]; sch = open(P).read()
lib = open("/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols/Device.kicad_sym").read()
JOBS = [("R_Potentiometer_MountingPin", "Device:R_Potentiometer", (-5.08, 0.0), 180, "right bottom"),
        ("RotaryEncoder_Switch_MP", "Device:RotaryEncoder_Switch", (0.0, 7.62), 270, "right bottom")]
labels = []
for stock, old, off, ang, just in JOBS:
    new = "Device:" + stock
    b = sym(lib, stock).replace(f'(symbol "{stock}"', f'(symbol "{new}"', 1)
    b = "\n".join(("\t" + l) if l else l for l in b.split("\n")).lstrip("\t")       # one level deeper inside lib_symbols
    e = sym(sch, old); sch = sch.replace(e, b, 1)
    n = 0; pos = 0
    while True:
        m = re.search(r'\(symbol\s*\(lib_id "%s"\)\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)' % re.escape(old), sch[pos:])
        if not m: break
        s0 = pos + m.start(); inst = block(sch, s0)
        x, y, rot = float(m.group(1)), float(m.group(2)), int(m.group(3))
        assert rot == 0, "rotated instance not handled"
        ni = inst.replace(f'(lib_id "{old}")', f'(lib_id "{new}")', 1)
        k = ni.rindex("(instances")
        ni = ni[:k] + f'(pin "MP"\n\t\t\t(uuid "{uuid.uuid4()}")\n\t\t)\n\t\t' + ni[k:]
        sch = sch[:s0] + ni + sch[s0+len(inst):]; pos = s0 + len(ni); n += 1
        labels.append(f'\t(label "GND"\n\t\t(at {round(x+off[0], 4)} {round(y+off[1], 4)} {ang})\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify {just})\n\t\t)\n\t\t(uuid "{uuid.uuid4()}")\n\t)\n')
    print(old, "->", new, ":", n, "instances")
k = sch.index('\t(label "')
sch = sch[:k] + "".join(labels) + sch[k:]
open(P, "w").write(sch); print("labels added:", len(labels))
