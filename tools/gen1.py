"""Create the project, custom libraries and schematic for the 125x130 controller."""
import os, json, sys, shutil
from kmcp import run

PROJ_DIR = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130"
NAME = "density_proto"
SCH = f"{PROJ_DIR}/{NAME}.kicad_sch"
LIB = f"{PROJ_DIR}/DensityMidi.pretty"
SYMLIB = f"{PROJ_DIR}/DensityMidi.kicad_sym"

if os.path.exists(PROJ_DIR) and "--fresh" in sys.argv:
    shutil.rmtree(PROJ_DIR)
os.makedirs(LIB, exist_ok=True)

# ---------------------------------------------------------------- footprints
def fp_header(name, descr, attr="through_hole"):
    return f'''(footprint "{name}"
	(version 20240108)
	(generator "density_midi")
	(layer "F.Cu")
	(descr "{descr}")
	(property "Reference" "REF**" (at 0 -6 0) (layer "F.SilkS") (effects (font (size 1 1) (thickness 0.15))))
	(property "Value" "{name}" (at 0 6 0) (layer "F.Fab") (effects (font (size 1 1) (thickness 0.15))))
	(attr {attr})
'''
def rect(x1, y1, x2, y2, layer, w=0.1):
    return f'\t(fp_rect (start {x1} {y1}) (end {x2} {y2}) (stroke (width {w}) (type solid)) (fill none) (layer "{layer}"))\n'
def line(x1, y1, x2, y2, layer, w=0.12):
    return f'\t(fp_line (start {x1} {y1}) (end {x2} {y2}) (stroke (width {w}) (type solid)) (layer "{layer}"))\n'
def text(s, x, y, layer="F.SilkS", size=0.8):
    return f'\t(fp_text user "{s}" (at {x} {y} 0) (layer "{layer}") (effects (font (size {size} {size}) (thickness 0.12))))\n'
def tht(num, x, y, drill, size, shape="circle"):
    return f'\t(pad "{num}" thru_hole {shape} (at {x} {y}) (size {size} {size}) (drill {drill}) (layers "*.Cu" "*.Mask"))\n'
def npth(x, y, d):
    return f'\t(pad "" np_thru_hole circle (at {x} {y}) (size {d} {d}) (drill {d}) (layers "*.Cu" "*.Mask"))\n'

# Alps RS6011SP (single unit, 60 mm travel). Hole pattern from Alps drawing
# "product_detail_fig_rs6011p_d_12", taken as viewed from the component side.
f = fp_header("Fader_Alps_RS6011SP_Single", "Alps RS6011SP 60mm travel slide pot, single unit. Origin at centre of terminal span. VERIFY against a real part before ordering.")
f += rect(-37.5, -4.5, 37.5, 4.5, "F.Fab")
f += rect(-37.75, -4.75, 37.75, 4.75, "F.CrtYd", 0.05)
f += rect(-37.5, -4.5, 37.5, 4.5, "F.SilkS", 0.12)
f += line(-30, 0, 30, 0, "F.Fab")            # lever travel
f += text("1", -36.3, -1.25 + 0.0, "F.Fab", 0.6)
f += tht("1", -35, -1.25, 1.2, 1.8, "rect")
f += tht("2", -35, -3.75, 1.2, 1.8)
f += tht("MP", -35, 1.25, 1.2, 1.8)
f += tht("MP", -35, 3.75, 1.2, 1.8)
f += tht("3", 35, -1.25, 1.2, 1.8)
f += tht("MP", 35, 3.75, 1.2, 1.8)
f += npth(-25, 0, 2.1)
f += npth(22.5, 0, 2.1)
f += ")\n"
open(f"{LIB}/Fader_Alps_RS6011SP_Single.kicad_mod", "w").write(f)

# Waveshare RP2350-Zero (also RP2040-Zero). Origin = centre of the USB (top) edge.
left = ["5V", "GND", "3V3", "GP29", "GP28", "GP27", "GP26", "GP15", "GP14"]
bottom = ["GP13", "GP12", "GP11", "GP10", "GP9"]
right_bt = ["GP8", "GP7", "GP6", "GP5", "GP4", "GP3", "GP2", "GP1", "GP0"]   # bottom -> top
ZERO_PINS = left + bottom + right_bt     # pad numbers 1..23
f = fp_header("Waveshare_RP2350_Zero_THT", "Waveshare RP2350-Zero / RP2040-Zero, 18x23.5mm, through-hole pin headers. Origin at centre of USB edge.")
f += rect(-9, 0, 9, 23.5, "F.Fab")
f += rect(-9.25, -0.25, 9.25, 23.75, "F.CrtYd", 0.05)
f += rect(-9, 0, 9, 23.5, "F.SilkS", 0.12)
f += rect(-4.5, -1.0, 4.5, 6.3, "F.Fab")       # USB-C
f += text("USB", 0, 3, "F.SilkS", 0.8)
n = 1
for i, nm in enumerate(left):
    f += tht(str(n), -7.62, round(1.59 + 2.54 * i, 3), 1.0, 1.7, "rect" if n == 1 else "circle"); n += 1
for i, nm in enumerate(bottom):
    f += tht(str(n), round(-5.08 + 2.54 * i, 3), 22.12, 1.0, 1.7); n += 1
for i, nm in enumerate(right_bt):
    f += tht(str(n), 7.62, round(1.59 + 2.54 * (8 - i), 3), 1.0, 1.7); n += 1
f += ")\n"
open(f"{LIB}/Waveshare_RP2350_Zero_THT.kicad_mod", "w").write(f)

# 0.96in I2C OLED module on a 4-pin header. Origin = centre of the header.
f = fp_header("OLED_096in_I2C_4Pin", "0.96in SSD1306 I2C OLED module, 4-pin 2.54mm header, pins at top. Pin1=left (GND on the usual blue module, see jumpers).")
f += rect(-13.8, -1.8, 13.8, 27.5, "F.Fab")
f += rect(-13.8, -1.8, 13.8, 27.5, "Dwgs.User")
f += rect(-5.35, -1.55, 5.35, 1.55, "F.CrtYd", 0.05)
f += rect(-5.1, -1.3, 5.1, 1.3, "F.SilkS", 0.12)
f += text("1", -6.2, 0, "F.SilkS", 0.8)
for i in range(4):
    f += tht(str(i + 1), round(-3.81 + 2.54 * i, 2), 0, 1.0, 1.7, "rect" if i == 0 else "circle")
f += ")\n"
open(f"{LIB}/OLED_096in_I2C_4Pin.kicad_mod", "w").write(f)

# OPSCO SK6812MINI-RV, 3.7 x 3.5 mm. Land pattern from the OPSCO datasheet (top view).
def smd(num, x, y, w, h):
    return f'\t(pad "{num}" smd rect (at {x} {y}) (size {w} {h}) (layers "F.Cu" "F.Paste" "F.Mask"))\n'
f = fp_header("LED_SK6812MINI-RV_3.7x3.5mm", "OPSCO SK6812MINI-RV addressable RGB LED, 3.7x3.5x1.1mm. Pin 1 DIN bottom right, 2 VDD top right, 3 DOUT top left, 4 GND bottom left.", "smd")
f += rect(-1.85, -1.75, 1.85, 1.75, "F.Fab")
f += rect(-2.4, -2.0, 2.4, 2.0, "F.CrtYd", 0.05)
f += line(2.55, 0.4, 2.55, 1.6, "F.SilkS")            # pin 1 side marker
f += line(-1.85, -1.95, 1.85, -1.95, "F.SilkS"); f += line(-1.85, 1.95, 1.85, 1.95, "F.SilkS")
f += smd("1", 1.5, 0.85, 1.3, 0.9)
f += smd("2", 1.05, -0.7, 2.2, 1.2)
f += smd("3", -1.4, -0.65, 1.5, 1.3)
f += smd("4", -0.95, 0.9, 2.4, 0.8)
f += ")\n"
open(f"{LIB}/LED_SK6812MINI-RV_3.7x3.5mm.kicad_mod", "w").write(f)

# M3 mounting hole with a courtyard that is just the screw head (5.5 mm) plus a little
f = fp_header("MountingHole_M3_tight", "3.2 mm M3 mounting hole, courtyard radius 2.85 mm", "exclude_from_pos_files exclude_from_bom")
f += '\t(fp_circle (center 0 0) (end 2.85 0) (stroke (width 0.05) (type solid)) (fill none) (layer "F.CrtYd"))\n'
f += '\t(fp_circle (center 0 0) (end 2.75 0) (stroke (width 0.1) (type solid)) (fill none) (layer "Cmts.User"))\n'
f += npth(0, 0, 3.2)
f += ")\n"
open(f"{LIB}/MountingHole_M3_tight.kicad_mod", "w").write(f)

open(f"{PROJ_DIR}/fp-lib-table", "w").write('(fp_lib_table\n  (version 7)\n  (lib (name "DensityMidi")(type "KiCad")(uri "${KIPRJMOD}/DensityMidi.pretty")(options "")(descr "Density MIDI project footprints"))\n)\n')
open(f"{PROJ_DIR}/sym-lib-table", "w").write('(sym_lib_table\n  (version 7)\n  (lib (name "DensityMidi")(type "KiCad")(uri "${KIPRJMOD}/DensityMidi.kicad_sym")(options "")(descr "Density MIDI project symbols"))\n)\n')

# ---------------------------------------------------------------- project
if not os.path.exists(SCH):
    r = run("create_project", {"path": PROJ_DIR, "name": NAME})
    print("create_project:", {k: r.get(k) for k in ("success", "message")})
print(sorted(os.listdir(PROJ_DIR)))

# ---------------------------------------------------------------- symbol
if os.path.exists(SYMLIB):
    os.remove(SYMLIB)
lpins = ZERO_PINS[:12]            # pads 1..12 on the left of the symbol
rpins = ZERO_PINS[12:][::-1]      # GP0..GP10 on the right, top to bottom
pins = []
for i, nm in enumerate(lpins):
    t = "power_out" if nm in ("5V", "3V3") else ("power_in" if nm == "GND" else "bidirectional")
    pins.append({"name": nm, "number": str(ZERO_PINS.index(nm) + 1), "type": t, "at": {"x": -17.78, "y": 13.97 - 2.54 * i, "angle": 0}, "length": 5.08})
for i, nm in enumerate(rpins):
    pins.append({"name": nm, "number": str(ZERO_PINS.index(nm) + 1), "type": "bidirectional", "at": {"x": 17.78, "y": 13.97 - 2.54 * i, "angle": 180}, "length": 5.08})
r = run("create_symbol", {"libraryPath": SYMLIB, "name": "RP2350_Zero", "referencePrefix": "A",
                          "description": "Waveshare RP2350-Zero module", "footprint": "DensityMidi:Waveshare_RP2350_Zero_THT",
                          "rectangles": [{"x1": -12.7, "y1": 16.51, "x2": 12.7, "y2": -16.51}], "pins": pins, "overwrite": True})
print("create_symbol:", r.get("success"), str(r.get("message"))[:120])
r = run("create_symbol", {"libraryPath": SYMLIB, "name": "SK6812MINI_RV", "referencePrefix": "D",
                          "description": "OPSCO SK6812MINI-RV addressable RGB LED", "footprint": "DensityMidi:LED_SK6812MINI-RV_3.7x3.5mm",
                          "rectangles": [{"x1": -7.62, "y1": 5.08, "x2": 7.62, "y2": -5.08}], "overwrite": True,
                          "pins": [{"name": "DIN", "number": "1", "type": "input", "at": {"x": -10.16, "y": 0, "angle": 0}, "length": 2.54},
                                   {"name": "DOUT", "number": "3", "type": "output", "at": {"x": 10.16, "y": 0, "angle": 180}, "length": 2.54},
                                   {"name": "VDD", "number": "2", "type": "power_in", "at": {"x": 0, "y": 7.62, "angle": 270}, "length": 2.54},
                                   {"name": "GND", "number": "4", "type": "power_in", "at": {"x": 0, "y": -7.62, "angle": 90}, "length": 2.54}]})
print("create_symbol LED:", r.get("success"), str(r.get("message"))[:120])
