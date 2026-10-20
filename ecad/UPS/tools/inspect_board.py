"""Read-only geometry/connectivity inventory; run using KiCad's Python."""
import json
from pathlib import Path
import sys
import pcbnew as k

path = Path(sys.argv[1]).resolve()
b = k.LoadBoard(str(path))
mm = k.ToMM
def point(p):
    return [round(mm(p.x), 5), round(mm(p.y), 5)]
def box(r):
    return [round(mm(v), 5) for v in (r.GetX(), r.GetY(), r.GetWidth(), r.GetHeight())]
out = {"board": str(path), "outline": box(b.GetBoardEdgesBoundingBox()), "footprints": []}
for f in b.GetFootprints():
    gs = []
    for g in f.GraphicalItems():
        if g.GetClass() == "PCB_SHAPE":
            gs.append({"layer": g.GetLayerName(), "shape": int(g.GetShape()),
                       "start": point(g.GetStart()), "end": point(g.GetEnd()),
                       "width": mm(g.GetWidth())})
    out["footprints"].append({
        "ref": f.GetReference(), "value": f.GetValue(), "uuid": f.m_Uuid.AsString(),
        "lib": str(f.GetFPID().GetLibNickname()) + ":" + str(f.GetFPID().GetLibItemName()),
        "sheet": f.GetSheetname(), "path": f.GetPath().AsString(),
        "at": point(f.GetPosition()), "angle": f.GetOrientationDegrees(),
        "layer": f.GetLayerName(), "box": box(f.GetBoundingBox(False, False)),
        "graphics": gs,
        "pads": [{"num": p.GetNumber(), "net": p.GetNetname(), "at": point(p.GetPosition()),
                  "size": point(p.GetSize()), "drill": point(p.GetDrillSize()),
                  "angle": p.GetOrientationDegrees(), "shape": int(p.GetShape())}
                 for p in f.Pads()]})
out["tracks"] = [{"uuid": t.m_Uuid.AsString(), "type": t.GetClass(), "layer": t.GetLayerName(),
                  "net": t.GetNetname(), "start": point(t.GetStart()), "end": point(t.GetEnd()),
                  "width": mm(t.GetWidth(t.GetLayer()) if t.GetClass() == "PCB_VIA" else t.GetWidth())} for t in b.GetTracks()]
out["zones"] = [{"uuid": z.m_Uuid.AsString(), "layers": [b.GetLayerName(layer) for layer in z.GetLayerSet().Seq()],
                 "net": z.GetNetname()} for z in b.Zones()]
target = Path(sys.argv[2])
target.write_text(json.dumps(out, indent=2), encoding="utf-8")
print(f"Wrote {target}: {len(out['footprints'])} footprints, {len(out['tracks'])} tracks/vias")
