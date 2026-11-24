"""Give every LED ground pad a fixed stub to a via in the open space beside it (down to the ground plane).
Run after placement (the autorouter then routes around them) and again after routing (the import drops them)."""
import math, pcbnew
PCB = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/density_proto.kicad_pcb"
mm = pcbnew.FromMM
b = pcbnew.LoadBoard(PCB); gnd = b.FindNet("GND")
vias = [(t.GetPosition().x, t.GetPosition().y) for t in b.GetTracks() if t.GetClass() == "PCB_VIA"]
n = 0
for k in range(1, 21):
    fp = b.FindFootprintByReference(f"D{k}"); c = fp.GetPosition()
    p = [q for q in fp.Pads() if q.GetNumber() == "4"][0]
    assert p.GetNetname() == "GND"
    pp = p.GetPosition()
    vx, vy = pp.x, pp.y + mm(math.copysign(1.45, pp.y - c.y))
    if any(abs(vx - x) < 50000 and abs(vy - y) < 50000 for x, y in vias): continue
    v = pcbnew.PCB_VIA(b); v.SetPosition(pcbnew.VECTOR2I(int(vx), int(vy))); v.SetNet(gnd); v.SetViaType(pcbnew.VIATYPE_THROUGH); v.SetDrill(mm(0.3))
    try: v.SetWidth(pcbnew.F_Cu, mm(0.5))
    except TypeError: v.SetWidth(mm(0.5))
    v.SetLayerPair(pcbnew.F_Cu, pcbnew.B_Cu); b.Add(v)
    t = pcbnew.PCB_TRACK(b); t.SetStart(pp); t.SetEnd(pcbnew.VECTOR2I(int(vx), int(vy))); t.SetWidth(mm(0.3)); t.SetLayer(pcbnew.F_Cu); t.SetNet(gnd); b.Add(t)
    vias.append((vx, vy)); n += 1
pcbnew.SaveBoard(PCB, b)
print("LED ground stubs added:", n)
