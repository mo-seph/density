import pcbnew, sys
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.ToMM; x, y = float(sys.argv[2]), float(sys.argv[3])
vict = [t for t in b.GetTracks() if t.GetNetname() == "GND" and ((t.GetClass() == "PCB_VIA" and abs(mm(t.GetPosition().x) - x) < 0.01 and abs(mm(t.GetPosition().y) - y) < 0.01) or (t.GetClass() == "PCB_TRACK" and abs(mm(t.GetEnd().x) - x) < 0.01 and abs(mm(t.GetEnd().y) - y) < 0.01))]
for t in vict: b.Remove(t)
print("removed", len(vict)); pcbnew.SaveBoard(sys.argv[1], b)
