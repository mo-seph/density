"""Redraw density_proto.kicad_sch as four wired, titled blocks on one A1 sheet.

Every symbol keeps its UUID, value and footprint, so the board's links to the schematic survive.
Named nets use global labels and power symbols, so net names come out exactly as on the board ("GND", not "/GND").
usage: gen_schematic.py <existing .kicad_sch> <netlist.kicadsexpr of it> <output .kicad_sch>"""
import re, sys, math, uuid as _uuid

SRC, NETFILE, OUT = sys.argv[1:4]
KICAD_SYMS = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"
POWER = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V", "AVDD": "DensityMidi:AVDD"}

def block(text, start):
    d = 0
    for i in range(start, len(text)):
        if text[i] == "(": d += 1
        elif text[i] == ")":
            d -= 1
            if d == 0: return text[start:i+1]

src = open(SRC).read()
ROOT = re.search(r'\(uuid "?([0-9a-f-]+)"?\)', src).group(1)
li = src.index("(lib_symbols"); libblock = block(src, li); rest = src[li+len(libblock):]

# ---- library symbols: pin geometry ------------------------------------------------------------------------------
def libsyms(lb):
    out = {}
    for m in re.finditer(r'\n\t\t\(symbol "([^"]+)"', lb):
        out[m.group(1)] = block(lb, m.start() + 3)
    return out
LIB = libsyms(libblock)
def stock(libfile, name, newname=None, rename=None):
    t = open(f"{KICAD_SYMS}/{libfile}.kicad_sym").read()
    m = re.search(r'\(symbol "%s"\s' % re.escape(name), t); b = block(t, m.start())
    if rename: b = b.replace(name, rename)
    full = newname or f"{libfile}:{name}"
    b = b.replace(f'(symbol "{rename or name}"', f'(symbol "{full}"', 1)
    return full, "\n".join(("\t" + l) if l else l for l in b.split("\n")).lstrip("\t")
for libfile, name in (("power", "GND"), ("power", "+3V3"), ("power", "+5V")):
    full, b = stock(libfile, name)
    LIB.setdefault(full, b)
full, b = stock("power", "+3V3", "DensityMidi:AVDD", "AVDD"); LIB.setdefault(full, b)

PINS = {}
for name, b in LIB.items():
    for u in re.finditer(r'\(symbol "[^"]+_(\d+)_(\d+)"', b):
        ub = block(b, u.start())
        for p in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length ([-\d.]+)\).*?\(number "([^"]*)"', ub, re.S):
            PINS.setdefault(name, []).append((int(u.group(1)), p.group(5), float(p.group(1)), float(p.group(2)), int(p.group(3))))

# ---- existing symbol instances ----------------------------------------------------------------------------------
INST = {}
for m in re.finditer(r'\n\t\(symbol\s', rest):
    b = block(rest, m.start() + 2)
    ref = re.search(r'\(property "Reference" "([^"]+)"', b).group(1)
    if ref.startswith("#PWR"): continue
    unit = int(re.search(r'\(unit (\d+)\)', b).group(1))
    INST[(ref, unit)] = dict(
        lib=re.search(r'\(lib_id "([^"]+)"\)', b).group(1), uuid=re.search(r'\(uuid "([^"]+)"\)', b).group(1),
        flags={k: re.search(r'\(%s (\w+)\)' % k, b).group(1) for k in ("exclude_from_sim", "in_bom", "on_board", "in_pos_files", "dnp")},
        props=re.findall(r'\(property "([^"]+)" "([^"]*)"', b), pins=re.findall(r'\(pin "([^"]+)"\s*\(uuid "([^"]+)"\)', b))

# ---- nets -------------------------------------------------------------------------------------------------------
NET = {}
nt = open(NETFILE).read()
for m in re.finditer(r'\(net\s*\(code "\d+"\)\s*\(name "([^"]*)"\)(.*?)(?=\(net\s*\(code|\Z)', nt, re.S):
    name = m.group(1).lstrip("/")
    for r, p in re.findall(r'\(node\s*\(ref "([^"]+)"\)\s*\(pin "([^"]+)"\)', m.group(2)): NET[(r, p)] = name

# ---- output primitives ------------------------------------------------------------------------------------------
items = []; npwr = [0]; placed = set()
def U(): return str(_uuid.uuid4())
def f(v):
    assert abs(v/1.27 - round(v/1.27)) < 1e-6, f"off grid: {v}"
    return ("%.4f" % v).rstrip("0").rstrip(".")
def ft(v): return ("%.4f" % v).rstrip("0").rstrip(".")
FONT = "(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t)%s\n\t\t\t)"
def prop(name, val, x, y, hide=False, just=None, ang=0, flip=False):
    if flip and just in ("left", "right"):
        x += (0.6 if just == "left" else -0.6)*len(val); just = "center"
    j = "" if not just or just == "center" else f"\n\t\t\t\t(justify {just})"
    h = "\n\t\t\t(hide yes)" if hide else ""
    return f'\t\t(property "{name}" "{val}"\n\t\t\t(at {ft(x)} {ft(y)} {ang}){h}\n\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n\t\t\t{FONT % j}\n\t\t)\n'
def xf(px, py, rot, mirror):
    dx, dy = px, -py
    if mirror == "x": dy = -dy
    if mirror == "y": dx = -dx
    c, s = round(math.cos(math.radians(rot))), round(math.sin(math.radians(rot)))
    return (dx*c + dy*s, -dx*s + dy*c)
def outward(ang, rot, mirror):
    ox, oy = -round(math.cos(math.radians(ang))), -round(math.sin(math.radians(ang)))
    return xf(ox, oy, rot, mirror)
DIRROT = {(1, 0): 0, (0, -1): 90, (-1, 0): 180, (0, 1): 270}
D = {"L": (-1, 0), "R": (1, 0), "U": (0, -1), "D": (0, 1)}

# where the reference and value texts go, relative to the symbol: (dx, dy, justify) or None to hide
def textpos(lib, rot, pins):
    ys = [p[1] for p in pins.values()] or [0]; top, bot = min(ys), max(ys)
    if lib == "Device:FerriteBead" and rot in (90, 270): return ((0, -5.08, "center"), (0, 5.08, "center"))
    if lib in ("Device:C", "Device:R", "Device:FerriteBead"):
        return ((2.54, -1.27, "left"), (2.54, 1.27, "left")) if rot in (0, 180) else ((0, -2.54, "center"), (0, 2.54, "center"))
    T = {"DensityMidi:RP2350_Zero": ((0, -19.05, "center"), (0, 19.05, "center")),
         "Analog_ADC:MCP3208": ((7.62, -13.97, "left"), (7.62, 16.51, "left")),
         "74xGxx:74AHCT1G125": ((2.54, -7.62, "left"), (-2.54, 12.7, "left")),
         "DensityMidi:SK6812MINI_RV": ((2.54, -6.35, "left"), None),
         "Device:R_Potentiometer_MountingPin": ((-12.7, -2.54, "left"), (-12.7, 13.97, "left")),
         "Connector_Generic:Conn_01x04": ((0, -7.62, "center"), (0, 10.16, "center")),
         "Connector_Generic:Conn_01x08": ((0, -11.43, "center"), (0, 13.97, "center")),
         "Connector_Audio:AudioJack4": ((-5.08, -7.62, "center"), (-5.08, 10.16, "center")),
         "Switch:SW_Push": ((0, -5.08, "center"), None),
         "Device:RotaryEncoder_Switch_MP": ((0, -8.89, "center"), (3.81, 8.89, "left")),
         "Device:R_Pack04": ((5.08, -1.27, "left"), (5.08, 1.27, "left")),
         "Jumper:SolderJumper_3_Bridged12": ((0, -3.81, "center"), (0, -6.35, "center")),
         "Connector:TestPoint": ((2.54, -2.54, "left"), None),
         "Mechanical:MountingHole": ((0, -5.08, "center"), (0, 5.08, "center")),
         "power:PWR_FLAG": (None, (0, -5.08, "center"))}
    if lib in T: return T[lib]
    if lib.startswith("Amplifier_Operational"): return ((2.54, -5.08, "left"), (2.54, 5.08, "left"))
    return ((1.27, top + 1.27, "left"), (1.27, bot - 1.27, "left"))

class Part:
    def __init__(self, ref, unit, pins, outs): self.ref, self.unit, self.p, self.o = ref, unit, pins, outs
def place(ref, x, y, rot=0, mirror=None, unit=1, ref_at=None, val_at="keep", stub=2.54):
    d = INST[(ref, unit)]; lib = d["lib"]; placed.add((ref, unit))
    pins, outs = {}, {}
    for u, num, px, py, ang in PINS.get(lib, []):
        if u in (0, unit):
            dx, dy = xf(px, py, rot, mirror); pins[num] = (x + dx, y + dy); outs[num] = outward(ang, rot, mirror)
    rp, vp = textpos(lib, rot, {k: (v[0]-x, v[1]-y) for k, v in pins.items()})
    if ref_at is not None: rp = ref_at
    if val_at != "keep": vp = val_at
    if ref.startswith("#"): rp = None
    if lib.startswith("Amplifier_Operational"): vp = (2.54, -7.62, "left") if unit == 1 else None
    ang = 90 if rot in (90, 270) else 0; flip = rot != 0 or mirror == "y"
    s = f'\t(symbol\n\t\t(lib_id "{lib}")\n\t\t(at {f(x)} {f(y)} {rot})\n'
    if mirror: s += f"\t\t(mirror {mirror})\n"
    s += f"\t\t(unit {unit})\n\t\t(body_style 1)\n"
    for k in ("exclude_from_sim", "in_bom", "on_board", "in_pos_files", "dnp"): s += f"\t\t({k} {d['flags'][k]})\n"
    s += f'\t\t(uuid "{d["uuid"]}")\n'
    for name, val in d["props"]:
        if name == "Reference": s += prop(name, val, x + (rp[0] if rp else 0), y + (rp[1] if rp else 0), rp is None, rp and rp[2], ang, flip)
        elif name == "Value": s += prop(name, val, x + (vp[0] if vp else 0), y + (vp[1] if vp else 0), vp is None, vp and vp[2], ang, flip)
        else: s += prop(name, val, x, y, True)
    for num, pu in d["pins"]: s += f'\t\t(pin "{num}"\n\t\t\t(uuid "{pu}")\n\t\t)\n'
    s += f'\t\t(instances\n\t\t\t(project "density_proto"\n\t\t\t\t(path "/{ROOT}"\n\t\t\t\t\t(reference "{ref}")\n\t\t\t\t\t(unit {unit})\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n'
    items.append(s)
    return Part(ref, unit, pins, outs)

def wire(*pts):
    for a, b in zip(pts, pts[1:]):
        assert abs(a[0]-b[0]) < 1e-6 or abs(a[1]-b[1]) < 1e-6, f"diagonal wire {a} {b}"
        if abs(a[0]-b[0]) < 1e-6 and abs(a[1]-b[1]) < 1e-6: continue
        items.append(f'\t(wire\n\t\t(pts\n\t\t\t(xy {f(a[0])} {f(a[1])}) (xy {f(b[0])} {f(b[1])})\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n\t\t(uuid "{U()}")\n\t)\n')
def junction(p):
    items.append(f'\t(junction\n\t\t(at {f(p[0])} {f(p[1])})\n\t\t(diameter 0)\n\t\t(color 0 0 0 0)\n\t\t(uuid "{U()}")\n\t)\n')
def noconnect(p): items.append(f'\t(no_connect\n\t\t(at {f(p[0])} {f(p[1])})\n\t\t(uuid "{U()}")\n\t)\n')
def glabel(net, p, d):
    rot = DIRROT[d]; just = "left" if rot in (0, 90) else "right"
    items.append(f'\t(global_label "{net}"\n\t\t(shape passive)\n\t\t(at {f(p[0])} {f(p[1])} {rot})\n\t\t(fields_autoplaced yes)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify {just})\n\t\t)\n\t\t(uuid "{U()}")\n'
                 f'\t\t(property "Intersheetrefs" "${{INTERSHEET_REFS}}"\n\t\t\t(at {f(p[0])} {f(p[1])} 0)\n\t\t\t(hide yes)\n\t\t\t(show_name no)\n\t\t\t(do_not_autoplace no)\n\t\t\t{FONT % ""}\n\t\t)\n\t)\n')
def power(net, p, d):
    """power symbol with its pin at p, drawn pointing in direction d"""
    lib = POWER[net]; npwr[0] += 1; ref = f"#PWR{npwr[0]:03d}"
    base = {"GND": {(0, 1): 0, (1, 0): 90, (0, -1): 180, (-1, 0): 270}}.get(net, {(0, -1): 0, (-1, 0): 90, (0, 1): 180, (1, 0): 270})
    rot = base[d]; x, y = p
    vx, vy = x + d[0]*(3.4 + 0.6*len(net)), y + d[1]*3.81
    just = "center"; pang = 90 if d[0] else 0
    s = f'\t(symbol\n\t\t(lib_id "{lib}")\n\t\t(at {f(x)} {f(y)} {rot})\n\t\t(unit 1)\n\t\t(body_style 1)\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n\t\t(on_board yes)\n\t\t(in_pos_files yes)\n\t\t(dnp no)\n\t\t(uuid "{U()}")\n'
    s += prop("Reference", ref, x, y, True) + prop("Value", net, vx, vy, net == "GND", just, pang)
    s += prop("Footprint", "", x, y, True) + prop("Datasheet", "", x, y, True) + prop("Description", f"Power symbol, net {net}", x, y, True)
    s += f'\t\t(pin "1"\n\t\t\t(uuid "{U()}")\n\t\t)\n\t\t(instances\n\t\t\t(project "density_proto"\n\t\t\t\t(path "/{ROOT}"\n\t\t\t\t\t(reference "{ref}")\n\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)\n\t)\n'
    items.append(s)
def text(s, x, y, size=1.5, bold=False):
    b = "\n\t\t\t\t(thickness %s)\n\t\t\t\t(bold yes)" % ft(size/5) if bold else ""
    s = s.replace('"', "'").replace("\n", "\\n")
    items.append(f'\t(text "{s}"\n\t\t(exclude_from_sim no)\n\t\t(at {ft(x)} {ft(y)} 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size {ft(size)} {ft(size)}){b}\n\t\t\t)\n\t\t\t(justify left top)\n\t\t)\n\t\t(uuid "{U()}")\n\t)\n')
def rect(x0, y0, x1, y1):
    items.append(f'\t(rectangle\n\t\t(start {ft(x0)} {ft(y0)})\n\t\t(end {ft(x1)} {ft(y1)})\n\t\t(stroke\n\t\t\t(width 0.3)\n\t\t\t(type dash)\n\t\t)\n\t\t(fill\n\t\t\t(type none)\n\t\t)\n\t\t(uuid "{U()}")\n\t)\n')

def net(part, pin): return NET.get((part.ref, pin), "unconnected-")
def stubpin(part, pin, L=2.54, d=None):
    """connect one pin by what its net is: power symbol on the pin, no-connect flag, or a short stub and a label"""
    n = net(part, pin); p = part.p[pin]; o = d or part.o[pin]
    if n.startswith("unconnected-"): noconnect(p)
    elif n in POWER: power(n, p, o)
    else:
        e = (p[0] + o[0]*L, p[1] + o[1]*L); wire(p, e); glabel(n, e, o)
def auto(part, skip=(), L=2.54):
    for pin in part.p:
        if pin not in skip: stubpin(part, pin, L)
def tag(netname, p, d, L=2.54):
    """name a wire: a short stub off the wire vertex p, ending in the net's label"""
    e = (p[0] + D[d][0]*L, p[1] + D[d][1]*L); wire(p, e); junction(p); glabel(netname, e, D[d])
def cap_to_gnd(ref, node, rot=0):
    """capacitor hanging from a node: pin 1 on the node, pin 2 to a ground symbol (rot 0 hangs down, 180 stands up)"""
    c = place(ref, node[0], node[1] + (3.81 if rot == 0 else -3.81), rot)
    assert abs(c.p["1"][0]-node[0]) < 1e-6 and abs(c.p["1"][1]-node[1]) < 1e-6, ref
    power(net(c, "2"), c.p["2"], c.o["2"]); return c
def decouple(ref, x, y):
    c = place(ref, x, y); power(net(c, "1"), c.p["1"], c.o["1"]); power(net(c, "2"), c.p["2"], c.o["2"])
def capfor(netname):
    return [r for (r, p), n in NET.items() if n == netname and r.startswith("C") and p == "1"][0]

# =================================================================================================================
# 1  Brain module, power, displays
# =================================================================================================================
rect(25.4, 25.4, 292.1, 259.08)
text("1  Brain module, power and displays", 30.48, 29.21, 3.5, True)
text("A1 is a plug-in Waveshare RP2350-Zero (not fitted by JLCPCB). 5 V is USB power straight from the module; 3V3 is the module's regulator.", 30.48, 36.83)
a1 = place("A1", 88.9, 86.36); auto(a1, L=5.08)
decouple("C3", 142.24, 76.2); decouple("C4", 154.94, 76.2); decouple("C2", 172.72, 76.2)
text("bulk capacitors", 140.97, 62.23)
fb = place("FB1", 208.28, 76.2, 90)
wire((198.12, 76.2), fb.p["1"]); power("+3V3", (198.12, 76.2), D["U"])
wire(fb.p["2"], (220.98, 76.2), (228.6, 76.2)); power("AVDD", (228.6, 76.2), D["U"]); junction((220.98, 76.2))
cap_to_gnd("C1", (220.98, 76.2))
text("AVDD: 3V3 through a ferrite, for the pots, mux, buffer and ADC", 196.85, 58.42)
fl = [k for k in INST if k[0].startswith("#FLG")]
place(fl[0][0], 243.84, 86.36); power("GND", (243.84, 86.36), D["D"])
place(fl[1][0], 256.54, 76.2); wire((228.6, 76.2), (256.54, 76.2))
for n, (jx, ja, jb, jref) in enumerate(((119.38, "JP1", "JP2", "J1"), (233.68, "JP3", "JP4", "J2"))):
    j = place(jref, jx, 144.78)
    a = place(ja, jx - 15.24, 127.0); b_ = place(jb, jx - 35.56, 127.0)
    wire(a.p["2"], (a.p["2"][0], 137.16), (a.p["2"][0], j.p["1"][1]), j.p["1"]); tag(net(j, "1"), (a.p["2"][0], 137.16), "L")
    wire(b_.p["2"], (b_.p["2"][0], 137.16), (b_.p["2"][0], j.p["2"][1]), j.p["2"]); tag(net(j, "2"), (b_.p["2"][0], 137.16), "L")
    auto(a, skip=("2",)); auto(b_, skip=("2",)); stubpin(j, "3"); stubpin(j, "4")
    text(f"OLED {n+1}: 0.96 in SSD1306 module. Jumpers pick which of pins 1 and 2 is ground.\nAs made: pin 1 = GND, pin 2 = 3V3. Cut the bridge before changing one.", jx - 48.26, 111.76)
r10 = place("R10", 259.08, 144.78); auto(r10); r11 = place("R11", 271.78, 144.78); auto(r11)
text("I2C pull-ups", 255.27, 135.89)
j5 = place("J5", 76.2, 198.12); auto(j5, L=5.08)
text("Spare I/O header (not fitted)", 53.34, 180.34)
for k, tp in enumerate(("TP1", "TP2", "TP3", "TP4", "TP11", "TP12")):
    t = place(tp, 124.46 + k*12.7, 190.5); stubpin(t, "1")
text("Test points", 119.38, 180.34)
for k in range(6): place(f"H{k+1}", 124.46 + k*12.7, 233.68)
text("M3 mounting holes, isolated from copper", 119.38, 222.25)

# =================================================================================================================
# 2  Analogue inputs
# =================================================================================================================
rect(25.4, 266.7, 424.18, 500.38)
text("2  Analogue inputs: 16 pots, fader, 2 pedals", 30.48, 270.51, 3.5, True)
text("Each wiper has 10 nF to ground. The mux picks one pot (address 0-15 = RV1-RV16), a unity-gain buffer drives the 12-bit ADC.\nADC reference is AVDD, the same rail that feeds the pots, so readings are ratiometric.", 30.48, 278.13)
for n in range(16):
    c, r = n % 4, n // 4; X, Y = 50.8 + c*40.64, 304.8 + r*30.48
    rv = place(f"RV{n+1}", X, Y, mirror="x")
    power("AVDD", rv.p["3"], D["U"]); power("GND", rv.p["1"], D["D"])
    wire(rv.p["MP"], (rv.p["MP"][0], rv.p["1"][1]), rv.p["1"])
    node = (X + 8.89, Y); wire(rv.p["2"], node, (X + 13.97, Y)); junction(node); glabel(net(rv, "2"), (X + 13.97, Y), D["R"])
    cap_to_gnd(capfor(net(rv, "2")), node)
u1 = place("U1", 243.84, 347.98, mirror="y")
auto(u1, skip=("1",)); decouple("C24", 261.62, 317.5)
u4a = place("U4", 294.64, 337.82, unit=1)
wire(u1.p["1"], (271.78, u1.p["1"][1]), u4a.p["3"]); tag("MUXA", (271.78, u1.p["1"][1]), "U")
n1 = (307.34, 337.82); wire(u4a.p["1"], n1); junction(n1)
wire(n1, (307.34, 345.44), (294.64, 345.44), (284.48, 345.44), (284.48, u4a.p["2"][1]), u4a.p["2"]); tag("BUFA", (294.64, 345.44), "D")
r7 = place("R7", 314.96, 337.82, 90); wire(n1, r7.p["1"])
n2 = (326.39, 337.82); u3 = place("U3", 368.3, 345.44)
wire(r7.p["2"], n2, (335.28, 337.82), u3.p["1"]); junction(n2); cap_to_gnd("C25", n2); tag("ADC0", (335.28, 337.82), "U")
auto(u3, skip=("1", "2", "3", "4", "5", "6", "7", "8"))
for pin in ("5", "6", "7", "8"): wire(u3.p[pin], (350.52, u3.p[pin][1]))
wire((350.52, u3.p["5"][1]), (350.52, u3.p["6"][1]), (350.52, u3.p["7"][1]), (350.52, u3.p["8"][1]), (350.52, u3.p["8"][1] + 2.54))
junction((350.52, u3.p["6"][1])); junction((350.52, u3.p["7"][1])); junction((350.52, u3.p["8"][1])); power("GND", (350.52, u3.p["8"][1] + 2.54), D["D"])
decouple("C28", 396.24, 322.58); decouple("C29", 411.48, 322.58)
u4p = place("U4", 294.64, 370.84, unit=3); auto(u4p); decouple("C27", 309.88, 370.84)
rv = place("RV17", 243.84, 398.78, mirror="x")
power("AVDD", rv.p["3"], D["U"]); power("GND", rv.p["1"], D["D"]); wire(rv.p["MP"], (rv.p["MP"][0], rv.p["1"][1]), rv.p["1"])
u4b = place("U4", 294.64, 401.32, unit=2)
nf = (256.54, 398.78); wire(rv.p["2"], nf, (271.78, 398.78), u4b.p["5"]); junction(nf); cap_to_gnd("C21", nf); tag("FADER1", (271.78, 398.78), "U")
text("60 mm fader", 231.14, 386.08)
n3 = (307.34, 401.32); wire(u4b.p["7"], n3); junction(n3)
wire(n3, (307.34, 408.94), (294.64, 408.94), (284.48, 408.94), (284.48, u4b.p["6"][1]), u4b.p["6"]); tag("BUFB", (294.64, 408.94), "D")
r8 = place("R8", 314.96, 401.32, 90); wire(n3, r8.p["1"])
n4 = (326.39, 401.32)
wire(r8.p["2"], n4, (332.74, 401.32), (340.36, 401.32), (340.36, u3.p["2"][1]), u3.p["2"]); junction(n4); cap_to_gnd("C26", n4); tag("ADC1", (332.74, 401.32), "U")
for k, (jref, rring, rtip, rpd, cc, ch, vx) in enumerate((("J3", "R1", "R2", "R3", "C22", "3", 342.9), ("J4", "R4", "R5", "R6", "C23", "4", 345.44))):
    Y = 439.42 + k*38.1
    j = place(jref, 243.84, Y)
    gx = j.p["S"][0] + 2.54
    wire(j.p["S"], (gx, j.p["S"][1]), (gx, j.p["S"][1] - 5.08)); wire(j.p["R2"], (gx, j.p["R2"][1]), (gx, j.p["S"][1])); junction((gx, j.p["S"][1])); power("GND", (gx, j.p["S"][1] - 5.08), D["U"])
    rr = place(rring, 266.7, j.p["R1"][1], 270, ref_at=(-2.54, -2.54, "center"), val_at=(3.81, -2.54, "center"))
    wire(j.p["R1"], (259.08, j.p["R1"][1]), rr.p["2"]); tag(net(j, "R1"), (259.08, j.p["R1"][1]), "U")
    wire(rr.p["1"], (rr.p["1"][0] + 2.54, rr.p["1"][1])); power("AVDD", (rr.p["1"][0] + 2.54, rr.p["1"][1]), D["R"])
    ty = j.p["T"][1]; rt = place(rtip, 289.56, ty, 90)
    wire(j.p["T"], (266.7, ty), rt.p["1"]); tag(net(j, "T"), (266.7, ty), "D")
    na, nb = (299.72, ty), (309.88, ty)
    wire(rt.p["2"], na, nb, (320.04, ty), (vx, ty), (vx, u3.p[ch][1]), u3.p[ch]); junction(na); junction(nb)
    pd = place(rpd, 299.72, ty + 3.81); power("GND", pd.p["2"], D["D"]); cap_to_gnd(cc, nb); tag(net(pd, "1"), (320.04, ty), "U")
    text(f"Pedal {k+1}: tip = wiper, ring = AVDD through 470 R, sleeve = ground", 231.14, Y - 16.51)
for k, tp in enumerate(("TP5", "TP6", "TP7", "TP13", "TP14")):
    t = place(tp, 375.92 + k*10.16, 393.7); stubpin(t, "1")
text("Test points", 370.84, 383.54)

# =================================================================================================================
# 3  Buttons and encoder
# =================================================================================================================
rect(299.72, 25.4, 815.34, 259.08)
text("3  Buttons and encoder", 304.8, 29.21, 3.5, True)
text("Three 74HC165 in a chain. Pulse SR_LOAD low, then clock out 24 bits on SR_DATA: U5 D7 first ... U5 D0, U6 D7 ... D0, U7 D7 ... D0.\nEvery input has a 10 k pull-up, so pressed = 0.", 304.8, 36.83)
regs = {}
for k, (uref, cref) in enumerate((("U7", "C34"), ("U6", "C33"), ("U5", "C32"))):
    u = place(uref, 355.6 + k*86.36, 106.68); regs[uref] = u
    auto(u, skip=("9", "10")); decouple(cref, u.p["9"][0] + 15.24, 116.84)
u7, u6, u5 = regs["U7"], regs["U6"], regs["U5"]
stubpin(u7, "10")
for a, b_, nm in ((u7, u6, "SR_CH2"), (u6, u5, "SR_CH1")):
    mid = ((a.p["9"][0] + b_.p["10"][0])/2, a.p["9"][1]); mid = (round(mid[0]/2.54)*2.54, mid[1])
    wire(a.p["9"], mid, b_.p["10"]); tag(nm, mid, "U")
stubpin(u5, "9", L=7.62)
t = place("TP10", 566.42, 76.2); stubpin(t, "1")
enc = place("ENC1", 680.72, 96.52)
for pin, cref, up in (("A", "C30", True), ("B", "C31", False)):
    y = enc.p[pin][1]; node = (660.4, y)
    wire(enc.p[pin], node, (652.78, y)); junction(node); glabel(net(enc, pin), (652.78, y), D["L"])
    cap_to_gnd(cref, node, 180 if up else 0)
wire(enc.p["C"], (enc.p["C"][0] - 2.54, enc.p["C"][1])); power("GND", (enc.p["C"][0] - 2.54, enc.p["C"][1]), D["L"])
stubpin(enc, "S1"); wire(enc.p["S2"], (enc.p["S2"][0] + 2.54, enc.p["S2"][1])); power("GND", (enc.p["S2"][0] + 2.54, enc.p["S2"][1]), D["R"]); stubpin(enc, "MP")
text("Push encoder, 30 detents / 15 pulses.\nA and B: 10 k pull-up + 10 nF.\nPoll at 2 kHz or faster.", 701.04, 86.36)
for k, rn in enumerate(("RN5", "RN6", "RN3", "RN4", "RN1", "RN2")):
    X = 342.9 + (k // 2)*86.36 + (k % 2)*20.32
    p = place(rn, X, 162.56)
    tops = sorted((p.p[n] for n in ("5", "6", "7", "8")), key=lambda q: q[0]); wire(*tops); power("+3V3", tops[0], D["U"])
    for n in ("1", "2", "3", "4"): stubpin(p, n)
text("Pull-ups: 10 k to 3V3 on every button and encoder line", 304.8, 147.32)
for n in range(20):
    c, r = (n % 4, n // 4) if n < 16 else (4, n - 16)
    sw = place(f"SW{n+1}", 337.82 + c*50.8, 205.74 + r*11.43)
    stubpin(sw, "1"); wire(sw.p["2"], (sw.p["2"][0] + 2.54, sw.p["2"][1])); power("GND", (sw.p["2"][0] + 2.54, sw.p["2"][1]), D["R"])
text("Buttons: SW1-SW16 sit below RV1-RV16; SW17-SW20 are the column under the encoder. All 6 x 6 mm tact switches.", 304.8, 193.04)
text("U5 D0-D7 = BTN1-8\nU6 D0-D7 = BTN9-16\nU7 D0-D3 = BTN17-20, D4 = ENC_A, D5 = ENC_B, D6 = ENC_SW, D7 = spare (reads 1)", 610.87, 132.08)

# =================================================================================================================
# 4  LEDs
# =================================================================================================================
rect(431.8, 266.7, 815.34, 500.38)
text("4  Button LEDs", 436.88, 270.51, 3.5, True)
text("20 SK6812MINI-RV in one chain, 5 V, each with 100 nF. Data is level-shifted from 3.3 V by U8.", 436.88, 278.13)
u8 = place("U8", 474.98, 309.88); auto(u8, skip=("4",), L=5.08); decouple("C35", 452.12, 332.74)
r9 = place("R9", 496.57, 309.88, 90)
wire(u8.p["4"], (490.22, 309.88), r9.p["1"]); tag("LED_5V", (490.22, 309.88), "D")
leds = []
for n in range(20):
    c, r = n % 5, n // 5; X, Y = 530.86 + c*40.64, 309.88 + r*45.72
    d = place(f"D{n+1}", X, Y); leds.append(d)
    power("GND", d.p["4"], D["D"])
    rail = Y - 16.51
    wire(d.p["2"], (X, rail)); cx = X + 20.32
    cap = place(f"C{36+n}", cx, rail + 3.81); power("GND", cap.p["2"], D["D"])
    if c == 0: power("+5V", (X, rail), D["U"])
    else: wire((X - 20.32, rail), (X, rail)); junction((X, rail))
    wire((X, rail), (cx, rail))
    if c < 4: junction((cx, rail))
for n, d in enumerate(leds):
    c = n % 5; X, Y = d.p["3"][0] - 10.16, d.p["3"][1]
    if n == 0:
        wire(r9.p["2"], (508.0, Y), (513.08, Y), d.p["1"]); tag("LED_DIN", (508.0, Y), "D"); place("TP8", 513.08, Y)
    elif c == 0: stubpin(d, "1")
    if n == 19:
        wire(d.p["3"], (d.p["3"][0] + 5.08, Y), (d.p["3"][0] + 10.16, Y)); tag("LED_END", (d.p["3"][0] + 5.08, Y), "D"); place("TP9", d.p["3"][0] + 10.16, Y)
    elif c == 4: stubpin(d, "3")
    else:
        mid = (X + 20.32, Y); wire(d.p["3"], mid, leds[n+1].p["1"]); tag(net(d, "3"), mid, "D")
text("Chain position -> beside button\n 1-4    BTN16, 15, 14, 13\n 5-8    BTN9, 10, 11, 12\n 9-12   BTN8, 7, 6, 5\n13-16   BTN1, 2, 3, 4\n17-20   BTN17, 18, 19, 20\n\nCap total LED current at about\n350-400 mA in firmware: full white\non all 20 is 0.7-0.8 A, more than\nUSB gives.", 741.68, 289.56)

missing = sorted(set(INST) - placed)
if missing: raise SystemExit(f"not placed: {missing}")

# ---- assemble ---------------------------------------------------------------------------------------------------
lib_out = "\t(lib_symbols\n" + "".join("\t\t" + b + "\n" for b in LIB.values()) + "\t)\n"
head = src[:li]
if "(title_block" not in head:
    head = head.rstrip() + '\n\t(title_block\n\t\t(title "Density Controller Test")\n\t\t(date "2026-11-18")\n\t\t(rev "0.1")\n\t\t(comment 1 "USB-MIDI controller prototype, 125 x 130 mm, 4 layers")\n\t)\n\t'
open(OUT, "w").write(head + lib_out.lstrip("\t") + "".join(items) + '\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)\n)\n')
print("symbols placed:", len(placed), "| power symbols:", npwr[0], "| items:", len(items))
