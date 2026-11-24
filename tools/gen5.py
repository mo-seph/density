"""Write JLCPCB BOM + CPL and a cost estimate from the placed board."""
import os, json, csv, sqlite3, subprocess, sys, collections
PROJ_DIR = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130"
NAME = "density_proto"
PCB = f"{PROJ_DIR}/{NAME}.kicad_pcb"
OUT = f"{PROJ_DIR}/jlcpcb"
HERE = os.path.dirname(os.path.abspath(__file__))
KCLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
os.makedirs(OUT, exist_ok=True)
comps = json.load(open(f"{HERE}/comps.json"))
comps.append({"reference": "U4", "value": "MCP6002T-I/SN", "footprint": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm", "nets": {}})

LCSC = {
    "10k lin RK09K1130AAU": "C202373", "10k lin RS6011SP6003": "C370993",
    "100nF": "C1525", "10nF": "C15195", "1nF": "C1523", "1uF": "C52923", "10uF": "C15850", "22uF": "C45783",
    "470R": "C25117", "1k": "C11702", "100k": "C25741", "100R": "C25076", "33R": "C25105", "4.7k": "C25900",
    "10k x4": "C29718", "600R@100MHz": "C1017", "6x6 tact": "C127509", "SK6812MINI-RV": "C7279958",
    "CD74HC4067SM96": "C98457", "MCP3208T-CI/SL": "C626764", "MCP6002T-I/SN": "C7377", "74HC165D": "C5613",
    "74AHCT1G125GV": "C52140417", "EC11E15244B2": "C470754", "PEDAL1": "C19189088", "PEDAL2": "C19189088",
}
groups = collections.OrderedDict()
skipped = []
for c in comps:
    ref = c["reference"]
    if ref.startswith(("#", "TP", "H", "JP")) or ref in ("A1", "J1", "J2", "J5"):
        skipped.append(ref); continue
    l = LCSC[c["value"]]
    g = groups.setdefault(l, {"comment": c["value"] if not c["value"].startswith("PEDAL") else "PJ-320D", "fp": c["footprint"].split(":")[1], "refs": []})
    g["refs"].append(ref)
with open(f"{OUT}/bom_jlcpcb.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Comment", "Designator", "Footprint", "LCSC Part #"])
    for l, g in groups.items():
        w.writerow([g["comment"], ",".join(g["refs"]), g["fp"], l])
assembled = {r for g in groups.values() for r in g["refs"]}

# placement file
pos = f"{OUT}/_pos_raw.csv"
subprocess.run([KCLI, "pcb", "export", "pos", "--format", "csv", "--units", "mm", "--side", "front", "-o", pos, PCB], check=True, capture_output=True)
rows = list(csv.DictReader(open(pos)))
import math
# JLCPCB's part orientation differs from KiCad's for these packages (degrees added to the KiCad rotation)
ROT = [("SOIC-", 270), ("SSOP-", 270), ("SOT-23-5", 270), ("R_Array_Convex", 270),   # both checked pad-number by pad-number against JLCPCB footprints (tools/review/matchall.py)
      
       ("Potentiometer_Alps_RK09K", 270), ("RotaryEncoder_Alps_EC11E", 270),
       ("SW_SPST_PTS645", 90), ("Jack_3.5mm_PJ320D", 90)]   # JLCPCB models have the 3 pins at the bottom at 0 degrees
# Where JLCPCB's own footprint origin sits, in KiCad footprint coordinates (x, y in mm). Worked out by matching the pads of
# JLCPCB's footprint (EasyEDA API, see tools/review/match.py) against the KiCad footprint; the placement file must give this point.
CENTRE = {"Potentiometer_Alps_RK09K_Single_Vertical": (3.5, 2.5),            # midway between the pin row and the lugs, not the shaft
          "RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm": (7.25, 2.5),
          "Jack_3.5mm_PJ320D_Horizontal": (0.83, 0.0),
          "LED_SK6812MINI-RV_3.7x3.5mm": (0.05, 0.10)}
for r in rows:
    rot = float(r["Rot"]); x = float(r["PosX"]); y = float(r["PosY"])
    if r["Package"] in CENTRE:
        dx, dy = CENTRE[r["Package"]]; a = math.radians(rot)
        x += dx * math.cos(a) + dy * math.sin(a); y += dx * math.sin(a) - dy * math.cos(a)     # pos file has Y up
    for key, add in ROT:
        if r["Package"].startswith(key): rot = (rot + add) % 360
    r["Rot"] = f"{rot:.1f}"; r["PosX"] = f"{x:.4f}"; r["PosY"] = f"{y:.4f}"
with open(f"{OUT}/cpl_jlcpcb.csv", "w", newline="") as f:
    w = csv.writer(f); w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    n = 0
    for r in rows:
        if r["Ref"] in assembled:
            w.writerow([r["Ref"], f'{float(r["PosX"]):.3f}mm', f'{float(r["PosY"]):.3f}mm', "Top", r["Rot"]]); n += 1
os.remove(pos)
print("BOM lines:", len(groups), "| placed parts:", n, "of", len(assembled), "| not assembled by JLCPCB:", len(skipped))
missing = assembled - {r["Ref"] for r in rows}
if missing: print("NOT IN POS FILE:", sorted(missing))

# cost estimate
db = sqlite3.connect("/Users/dmurrayrust/Library/Application Support/kicad-mcp/jlcpcb_parts.db")
def price(l, qty):
    row = db.execute("select price_json, library_type, solder_joints, stock, mfr_part from components where lcsc=?", (l,)).fetchone()
    tiers = json.loads(row[0]); p = None
    for t in tiers:
        lo = t.get("qFrom") or t.get("qty") or 1
        if qty >= lo: p = t.get("price")
    return (p if p is not None else tiers[0].get("price")), row[1], row[2], row[3], row[4]
THT = {"C202373", "C370993", "C470754"}
for boards in (2, 5):
    parts = 0.0; smt_j = 0; tht_j = 0; ext = 0; lines = []
    for l, g in groups.items():
        q = len(g["refs"]) * boards
        p, lib, joints, stock, mpn = price(l, q)
        parts += p * q
        if l in THT: tht_j += (joints or 0) * q
        else: smt_j += (joints or 0) * q
        if lib == "Extended": ext += 1
        if boards == 2: lines.append((l, mpn, len(g["refs"]), p, lib, stock))
    fixed = 8.18 + 1.53 + 3.07 * ext + 3.58
    joints = smt_j * 0.0016 + tht_j * 0.0164
    if boards == 2:
        print(f"{'LCSC':10} {'part':24} {'qty/board':>9} {'each $':>8}  library     stock")
        for l, mpn, q, p, lib, stock in lines: print(f"{l:10} {mpn[:24]:24} {q:9d} {p:8.4f}  {lib:10} {stock}")
    print(f"--- {boards} boards: parts ${parts:.2f} | extended types {ext} | SMT joints {smt_j} THT joints {tht_j} | fixed fees ${fixed:.2f} | joint fees ${joints:.2f} | assembly+parts total ${parts+fixed+joints:.2f} (bare PCBs and shipping not included)")
