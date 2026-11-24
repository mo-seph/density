"""Copper keepouts round the six M3 mounting holes: 3.5 mm radius on the outer layers (clear of a screw head or standoff),
2.6 mm radius on the inner layers (1 mm from the hole wall). Moves the AVDD track at H5 out of the way first."""
import pcbnew, sys, math
PCB = sys.argv[1]
b = pcbnew.LoadBoard(PCB); mm = pcbnew.ToMM; MM = pcbnew.FromMM
def V(p): return pcbnew.VECTOR2I(MM(p[0]), MM(p[1]))
def D(p, q): return math.hypot(p[0]-q[0], p[1]-q[1])
T = [(t, (mm(t.GetStart().x), mm(t.GetStart().y)), (mm(t.GetEnd().x), mm(t.GetEnd().y))) for t in b.GetTracks() if t.GetClass() == "PCB_TRACK" and t.GetNetname() == "AVDD" and t.GetLayer() == pcbnew.F_Cu]
def find(a, c):
    for t, s, e in T:
        if (D(s, a) < 0.011 and D(e, c) < 0.011) or (D(s, c) < 0.011 and D(e, a) < 0.011): return t
    raise SystemExit(f"not found {a} {c}")
old = [(112.75, 62.95), (110.25, 62.95), (109.55, 63.65), (109.55, 71.05), (110.0, 71.5)]
kill = [find(a, c) for a, c in zip(old, old[1:])]
stem = find((112.75, 55.35), (112.75, 62.95)); code = stem.GetNetCode()
if D((mm(stem.GetEnd().x), mm(stem.GetEnd().y)), (112.75, 62.95)) < 0.011: stem.SetEnd(V((112.75, 62.3)))
else: stem.SetStart(V((112.75, 62.3)))
new = [(112.75, 62.3), (110.85, 62.3), (109.15, 64.0), (109.15, 70.65), (110.0, 71.5)]
for a, c in zip(new, new[1:]):
    t = pcbnew.PCB_TRACK(b); t.SetStart(V(a)); t.SetEnd(V(c)); t.SetWidth(MM(0.3)); t.SetLayer(pcbnew.F_Cu); t.SetNetCode(code); b.Add(t)
print("AVDD at H5 rerouted:", new)
holes = [(fp.GetReference(), fp.GetPosition()) for fp in b.GetFootprints() if fp.GetReference().startswith("H")]
def keepout(name, pos, layers, r):
    z = pcbnew.ZONE(b); z.SetIsRuleArea(True)
    ls = pcbnew.LSET()
    for l in layers: ls.AddLayer(l)
    z.SetLayerSet(ls); z.SetZoneName(name)
    for setter, val in (("SetDoNotAllowZoneFills", True), ("SetDoNotAllowCopperPour", True), ("SetDoNotAllowTracks", True), ("SetDoNotAllowVias", True), ("SetDoNotAllowPads", False), ("SetDoNotAllowFootprints", False)):
        if hasattr(z, setter): getattr(z, setter)(val)
    o = z.Outline(); o.NewOutline()
    for k in range(64):
        a = 2*math.pi*k/64; o.Append(int(pos.x + MM(r)*math.cos(a)), int(pos.y + MM(r)*math.sin(a)))
    b.Add(z)
for ref, pos in holes:
    keepout(f"{ref} screw keepout", pos, (pcbnew.F_Cu, pcbnew.B_Cu), 3.5)
    keepout(f"{ref} inner keepout", pos, (pcbnew.In1_Cu, pcbnew.In2_Cu), 2.6)
print("keepouts added for", [r for r, p in holes])
for t in kill: b.Remove(t)
pcbnew.SaveBoard(PCB, b); print("saved")
