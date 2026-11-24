"""After routing: add GND pours on the outer layers, fill all zones, report tracks/vias."""
import sys
import pcbnew
PCB = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/density_proto.kicad_pcb"
OX, OY, W, H = 50.0, 40.0, 125.0, 130.0
mm = pcbnew.FromMM
b = pcbnew.LoadBoard(PCB)
layers = {z.GetLayer() for z in b.Zones() if not z.GetIsRuleArea()}
def zone(layer, netname):
    z = pcbnew.ZONE(b); z.SetLayer(layer); z.SetNet(b.FindNet(netname))
    o = z.Outline(); o.NewOutline()
    for x, y in ((0, 0), (W, 0), (W, H), (0, H)):
        o.Append(mm(OX + x), mm(OY + y))
    z.SetMinThickness(mm(0.2)); z.SetLocalClearance(mm(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.3)); z.SetThermalReliefSpokeWidth(mm(0.4))
    b.Add(z)
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    if layer not in layers:
        zone(layer, "GND")
copper = [z for z in b.Zones() if not z.GetIsRuleArea()]      # leave keepout areas alone
for z in copper:
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)      # solid on SMD pads and vias, spokes on through-hole pins
    z.SetThermalReliefSpokeWidth(mm(0.3)); z.SetThermalReliefGap(mm(0.25)); z.SetMinThickness(mm(0.15))
    try: z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    except AttributeError: z.SetIslandRemovalMode(0)
filler = pcbnew.ZONE_FILLER(b)
filler.Fill(b.Zones())      # keepouts must be in the list so the fill respects them
tracks = [t for t in b.GetTracks()]
vias = [t for t in tracks if t.GetClass() == "PCB_VIA"]
print("zones:", len(b.Zones()), "| track segments:", len(tracks) - len(vias), "| vias:", len(vias))
pcbnew.SaveBoard(PCB, b)
print("saved")
