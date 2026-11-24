"""Write simple block-and-prism STEP models for parts with no library model.
Coordinates are footprint-local in mm, with model Y = -footprint Y and Z up from the board top."""
import math, os

OUT = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/DensityMidi.3dshapes"
os.makedirs(OUT, exist_ok=True)

class Step:
    def __init__(self, name):
        self.name = name; self.lines = []; self.n = 100; self.solids = []
    def add(self, s):
        self.n += 1; self.lines.append(f"#{self.n} = {s};"); return self.n
    def pt(self, p): return self.add("CARTESIAN_POINT('',(%.6f,%.6f,%.6f))" % p)
    def dr(self, d):
        l = math.sqrt(sum(c * c for c in d)); return self.add("DIRECTION('',(%.9f,%.9f,%.9f))" % tuple(c / l for c in d))
    def prism(self, poly, z0, z1):
        """poly: list of (x, y) counter-clockwise seen from +z."""
        n = len(poly)
        area = sum(poly[i][0] * poly[(i + 1) % n][1] - poly[(i + 1) % n][0] * poly[i][1] for i in range(n))
        if area < 0: poly = poly[::-1]
        B = [(x, y, z0) for x, y in poly]; T = [(x, y, z1) for x, y in poly]
        vB = [self.add(f"VERTEX_POINT('',#{self.pt(p)})") for p in B]
        vT = [self.add(f"VERTEX_POINT('',#{self.pt(p)})") for p in T]
        def edge(pa, pb, va, vb):
            d = tuple(b - a for a, b in zip(pa, pb)); l = math.sqrt(sum(c * c for c in d))
            ln = self.add(f"LINE('',#{self.pt(pa)},#{self.add(f'VECTOR(chr(39)*2,#{self.dr(d)},{l:.6f})'.replace('chr(39)*2', chr(39)*2))})")
            return self.add(f"EDGE_CURVE('',#{va},#{vb},#{ln},.T.)")
        be = [edge(B[i], B[(i + 1) % n], vB[i], vB[(i + 1) % n]) for i in range(n)]
        te = [edge(T[i], T[(i + 1) % n], vT[i], vT[(i + 1) % n]) for i in range(n)]
        ve = [edge(B[i], T[i], vB[i], vT[i]) for i in range(n)]
        def face(oes, origin, normal, ref):
            loop = self.add("EDGE_LOOP('',(%s))" % ",".join(f"#{self.add(f'ORIENTED_EDGE(chr39,*,*,#{e},.{s}.)'.replace('chr39', chr(39)*2))}" for e, s in oes))
            bound = self.add(f"FACE_OUTER_BOUND('',#{loop},.T.)")
            ax = self.add(f"AXIS2_PLACEMENT_3D('',#{self.pt(origin)},#{self.dr(normal)},#{self.dr(ref)})")
            pl = self.add(f"PLANE('',#{ax})")
            return self.add(f"ADVANCED_FACE('',(#{bound}),#{pl},.T.)")
        faces = [face([(be[i], "F") for i in range(n - 1, -1, -1)], B[0], (0, 0, -1), (1, 0, 0)),
                 face([(te[i], "T") for i in range(n)], T[0], (0, 0, 1), (1, 0, 0))]
        for i in range(n):
            j = (i + 1) % n
            dx, dy = poly[j][0] - poly[i][0], poly[j][1] - poly[i][1]
            faces.append(face([(be[i], "T"), (ve[j], "T"), (te[i], "F"), (ve[i], "F")], B[i], (dy, -dx, 0), (dx, dy, 0)))
        shell = self.add("CLOSED_SHELL('',(%s))" % ",".join(f"#{f}" for f in faces))
        self.solids.append(self.add(f"MANIFOLD_SOLID_BREP('',#{shell})"))
    def box(self, x0, x1, y0, y1, z0, z1):
        self.prism([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], z0, z1)
    def cyl(self, cx, cy, d, z0, z1, sides=24):
        self.prism([(cx + d / 2 * math.cos(2 * math.pi * k / sides), cy + d / 2 * math.sin(2 * math.pi * k / sides)) for k in range(sides)], z0, z1)
    def dshaft(self, cx, cy, d, flat, z0, z1, sides=24):
        """Round shaft with a flat: keeps points with x-cx <= flat (flat faces +x)."""
        pts = []
        for k in range(sides):
            a = 2 * math.pi * k / sides
            pts.append((cx + min(d / 2 * math.cos(a), flat), cy + d / 2 * math.sin(a)))
        # drop duplicate consecutive points
        out = [p for i, p in enumerate(pts) if math.dist(p, pts[i - 1]) > 1e-6]
        self.prism(out, z0, z1)
    def write(self):
        nm = self.name
        head = f"""ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('{nm} simplified block model'),'2;1');
FILE_NAME('{nm}.step','2026-11-17T00:00:00',(''),(''),'density_midi','density_midi','');
FILE_SCHEMA(('AUTOMOTIVE_DESIGN {{ 1 0 10303 214 1 1 1 1 }}'));
ENDSEC;
DATA;
#1 = APPLICATION_PROTOCOL_DEFINITION('international standard','automotive_design',2000,#2);
#2 = APPLICATION_CONTEXT('core data for automotive mechanical design processes');
#3 = SHAPE_DEFINITION_REPRESENTATION(#4,#10);
#4 = PRODUCT_DEFINITION_SHAPE('','',#5);
#5 = PRODUCT_DEFINITION('design','',#6,#9);
#6 = PRODUCT_DEFINITION_FORMATION('','',#7);
#7 = PRODUCT('{nm}','{nm}','',(#8));
#8 = PRODUCT_CONTEXT('',#2,'mechanical');
#9 = PRODUCT_DEFINITION_CONTEXT('part definition',#2,'design');
#10 = ADVANCED_BREP_SHAPE_REPRESENTATION('',(#11,{",".join(f"#{s}" for s in self.solids)}),#15);
#11 = AXIS2_PLACEMENT_3D('',#12,#13,#14);
#12 = CARTESIAN_POINT('',(0.,0.,0.));
#13 = DIRECTION('',(0.,0.,1.));
#14 = DIRECTION('',(1.,0.,0.));
#15 = ( GEOMETRIC_REPRESENTATION_CONTEXT(3) GLOBAL_UNCERTAINTY_ASSIGNED_CONTEXT((#19)) GLOBAL_UNIT_ASSIGNED_CONTEXT((#16,#17,#18)) REPRESENTATION_CONTEXT('Context #1','3D Context with UNIT and UNCERTAINTY') );
#16 = ( LENGTH_UNIT() NAMED_UNIT(*) SI_UNIT(.MILLI.,.METRE.) );
#17 = ( NAMED_UNIT(*) PLANE_ANGLE_UNIT() SI_UNIT($,.RADIAN.) );
#18 = ( NAMED_UNIT(*) SI_UNIT($,.STERADIAN.) SOLID_ANGLE_UNIT() );
#19 = UNCERTAINTY_MEASURE_WITH_UNIT(LENGTH_MEASURE(1.E-07),#16,'distance_accuracy_value','confusion accuracy');
#20 = PRODUCT_RELATED_PRODUCT_CATEGORY('part',$,(#7));
"""
        open(f"{OUT}/{nm}.step", "w").write(head + "\n".join(self.lines) + "\nENDSEC;\nEND-ISO-10303-21;\n")
        print("wrote", nm, len(self.solids), "solids")

# Alps RK09K vertical pot, 15 mm flat shaft. Footprint origin = pad 1, shaft at (7, 2.5).
s = Step("pot_alps_rk09k_20mm")
s.box(1.0, 13.0, -7.4, 2.4, 0, 6.8)                 # body (footprint y -2.4..7.4), 6.8 mm tall per Alps drawing
s.dshaft(7.0, -2.5, 6.0, 1.5, 13.0, 20.0)          # flat section: top 7 mm of the 20 mm shaft
s.cyl(7.0, -2.5, 6.0, 6.8, 13.0)
s.write()

# Alps EC11E encoder with switch, 20 mm flat shaft. Origin = pad A, shaft at (7.5, 2.5).
s = Step("encoder_alps_ec11e_20mm")
s.box(1.5, 13.5, -8.3, 3.3, 0, 6.5)
s.cyl(7.5, -2.5, 7.0, 6.5, 11.5)                    # bushing
s.dshaft(7.5, -2.5, 6.0, 1.5, 11.5, 20.0)
s.write()

# Alps RS6011SP fader: 75 x 9 x 7 body, lever 15 mm above the board. Origin = centre.
s = Step("fader_alps_rs6011sp")
s.box(-37.5, 37.5, -4.5, 4.5, 0, 7.0)
s.box(-2.5, 2.5, -2.0, 2.0, 7.0, 15.0)              # lever at mid travel (travel is +/-30 mm)
s.write()

# Waveshare RP2350-Zero on pin headers. Origin = centre of USB edge; board runs to footprint y = 23.5.
s = Step("waveshare_rp2350_zero")
s.box(-8.9, -6.35, -23.2, -0.3, 0, 2.5)             # left header strip
s.box(6.35, 8.9, -23.2, -0.3, 0, 2.5)               # right header strip
s.box(-6.35, 6.35, -23.4, -20.85, 0, 2.5)           # bottom header strip
s.box(-9.0, 9.0, -23.5, 0.0, 2.5, 3.5)              # module PCB
s.box(-4.5, 4.5, -6.3, 1.0, 3.5, 6.7)               # USB-C socket
s.write()

# 0.96 in OLED module on a 4-pin header. Origin = header centre; module hangs toward footprint +y.
s = Step("oled_096_i2c_module")
s.box(-5.08, 5.08, -1.27, 1.27, 0, 2.5)             # header plastic
s.box(-13.8, 13.8, -27.5, 1.8, 2.5, 3.7)            # module PCB
s.box(-13.3, 13.3, -22.5, -3.0, 3.7, 5.2)           # glass
s.write()

# PJ-320D 3.5 mm jack. Footprint origin as library; opening toward -x.
s = Step("jack_pj320d")
s.box(-6.2, 5.575, -2.9, 2.9, 0, 5.0)
s.box(-8.225, -6.2, -2.5, 2.5, 0, 5.0)              # nose
s.write()

# WS2812B-2020
s = Step("led_sk6812mini_rv")
s.box(-1.85, 1.85, -1.75, 1.75, 0, 1.1)
s.write()
