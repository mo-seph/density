import pcbnew, sys, math
b = pcbnew.LoadBoard(sys.argv[1]); mm = pcbnew.ToMM; MM = pcbnew.FromMM
for z in b.Zones():
    for lay in z.GetLayerSet().Seq():
        poly = z.GetFilledPolysList(lay)
        for i in range(poly.OutlineCount()):
            o = poly.Outline(i); bb = o.BBox(); area = abs(o.Area())/1e12
            if area > 40: continue
            touch = []
            for fp in b.GetFootprints():
                for pad in fp.Pads():
                    if pad.GetNetname() == "GND" and pad.IsOnLayer(lay) and pad.GetBoundingBox().Intersects(bb):
                        c = pad.GetPosition()
                        touch.append(f"{fp.GetReference()}.{pad.GetNumber()}{'(in)' if o.PointInside(c) else ''}")
            for t in b.GetTracks():
                if t.GetNetname() != "GND": continue
                if t.GetClass() == "PCB_VIA":
                    if bb.Contains(t.GetPosition()) and o.PointInside(t.GetPosition()): touch.append("via")
                elif t.GetLayer() == lay and (o.PointInside(t.GetStart()) or o.PointInside(t.GetEnd())): touch.append("track")
            print(f"{b.GetLayerName(lay)} island {area:6.2f} mm2 at board ({mm(bb.GetLeft())-50:.1f},{mm(bb.GetTop())-40:.1f})-({mm(bb.GetRight())-50:.1f},{mm(bb.GetBottom())-40:.1f}) touches: {sorted(set(touch))}")
