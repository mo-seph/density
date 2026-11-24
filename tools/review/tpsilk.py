"""Show each test point's net name on the front silkscreen right beside its pad.
The ring round each test point goes (the label marks it), TPn references move to the Fab layer,
and any other reference text a label would sit on is moved to the Fab layer too."""
import pcbnew, sys
PCB = sys.argv[1]
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
SIZE, THICK, GAP = 1.0, 0.15, 0.3
SHORT = {"ADC_SCK": "SCK", "ADC_MISO": "MISO"}
tps = sorted((fp for fp in b.GetFootprints() if fp.GetReference().startswith("TP")), key=lambda f: int(f.GetReference()[2:]))
def box(bb, grow=0.0): return (mm(bb.GetLeft()) - grow, mm(bb.GetTop()) - grow, mm(bb.GetRight()) + grow, mm(bb.GetBottom()) + grow)
def hit(a, c): return a[0] < c[2] and c[0] < a[2] and a[1] < c[3] and c[1] < a[3]
hard = []; soft = []; rings = []
for fp in b.GetFootprints():
    istp = fp.GetReference().startswith("TP")
    for pad in fp.Pads():
        if pad.IsOnLayer(pcbnew.F_Cu) or pad.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH): hard.append((box(pad.GetBoundingBox(), 0.15), fp.GetReference() + " pad"))
    for f in (fp.Reference(), fp.Value()):
        if f.IsVisible() and f.GetLayer() == pcbnew.F_SilkS and not istp: soft.append((box(f.GetBoundingBox(), 0.05), fp.GetReference(), f))
    for g in fp.GraphicalItems():
        if g.GetLayer() != pcbnew.F_SilkS: continue
        if istp: rings.append((fp, g)); continue
        bb = box(g.GetBoundingBox(), 0.1)
        if g.GetClass() == "PCB_TEXT" or min(bb[2]-bb[0], bb[3]-bb[1]) < 1.0: hard.append((bb, fp.GetReference() + " silk"))
        else:
            for e in ((bb[0], bb[1], bb[2], bb[1]+0.2), (bb[0], bb[3]-0.2, bb[2], bb[3]), (bb[0], bb[1], bb[0]+0.2, bb[3]), (bb[2]-0.2, bb[1], bb[2], bb[3])): hard.append((e, fp.GetReference() + " outline"))
for d in b.GetDrawings():
    if d.GetLayer() == pcbnew.F_SilkS: hard.append((box(d.GetBoundingBox(), 0.1), "board text"))
EDGE = (50.6, 40.6, 174.4, 169.4)
def tbox(v, grow):
    bb = v.GetBoundingBox(); x, y = mm(v.GetPosition().x), mm(v.GetPosition().y)
    if abs(v.GetTextAngle().AsDegrees()) > 45: return (x - SIZE/2 - 0.05 - grow, mm(bb.GetTop()) - grow, x + SIZE/2 + 0.05 + grow, mm(bb.GetBottom()) + grow)
    return (mm(bb.GetLeft()) - grow, y - SIZE/2 - 0.05 - grow, mm(bb.GetRight()) + grow, y + SIZE/2 + 0.05 + grow)
L, C, R = pcbnew.GR_TEXT_H_ALIGN_LEFT, pcbnew.GR_TEXT_H_ALIGN_CENTER, pcbnew.GR_TEXT_H_ALIGN_RIGHT
hidden = []
for fp in tps:
    pad = list(fp.Pads())[0]; p = pad.GetPosition(); r = mm(pad.GetSizeX())/2
    ref, v = fp.Reference(), fp.Value()
    ref.SetLayer(pcbnew.F_Fab)
    if fp.GetValue() in SHORT: fp.SetValue(SHORT[fp.GetValue()])
    v.SetLayer(pcbnew.F_SilkS); v.SetVisible(True); v.SetTextSize(pcbnew.VECTOR2I(MM(SIZE), MM(SIZE))); v.SetTextThickness(MM(THICK)); v.SetTextAngle(pcbnew.EDA_ANGLE(0, pcbnew.DEGREES_T))
    dy = r + GAP + SIZE/2
    cands = [("below", 0, dy, C), ("above", 0, -dy, C), ("right", r+GAP, 0, L), ("left", -(r+GAP), 0, R),
             ("below, ending at pad", r, dy, R), ("above, ending at pad", r, -dy, R), ("below, starting at pad", -r, dy, L), ("above, starting at pad", -r, -dy, L), ("right, vertical", dy, 0, C, 90), ("left, vertical", -dy, 0, C, 90)]
    best = None
    for cand in cands:
        name, ox, oy, j = cand[:4]; ang = cand[4] if len(cand) > 4 else 0
        v.SetTextAngle(pcbnew.EDA_ANGLE(ang, pcbnew.DEGREES_T)); v.SetHorizJustify(j); v.SetPosition(pcbnew.VECTOR2I(p.x + MM(ox), p.y + MM(oy)))
        bb = tbox(v, 0.05)
        hh = sorted(set(w for o, w in hard if hit(bb, o)))
        if hh or not (bb[0] > EDGE[0] and bb[1] > EDGE[1] and bb[2] < EDGE[2] and bb[3] < EDGE[3]):
            if '-v' in sys.argv: print(f'      {fp.GetReference()} {name}: box {tuple(round(x-50 if i%2==0 else x-40, 2) for i, x in enumerate(bb))} blocked by {hh[:5]}')
            continue
        clash = [(w, f) for o, w, f in soft if hit(bb, o)]
        if best is None: best = (name, ox, oy, j, clash, ang)
    if best is None: print(f"{fp.GetReference():5} {fp.GetValue():8} NO SPOT"); v.SetLayer(pcbnew.F_Fab); continue
    name, ox, oy, j, clash, ang = best
    v.SetTextAngle(pcbnew.EDA_ANGLE(ang, pcbnew.DEGREES_T)); v.SetHorizJustify(j); v.SetPosition(pcbnew.VECTOR2I(p.x + MM(ox), p.y + MM(oy)))
    hard.append((tbox(v, 0.1), fp.GetReference() + " label"))
    for w, f in clash:
        f.SetLayer(pcbnew.F_Fab); hidden.append(w); soft[:] = [s for s in soft if s[2] is not f]
    print(f"{fp.GetReference():5} {fp.GetValue():8} {name:6}" + (f"  (moved to Fab layer: {', '.join(w for w, f in clash)})" if clash else ""))
for fp, g in rings: fp.Remove(g)
pcbnew.SaveBoard(PCB, b); print("saved; reference texts moved off the silkscreen:", sorted(set(hidden)))
