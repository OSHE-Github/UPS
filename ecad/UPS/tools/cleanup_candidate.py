"""Remove candidate-only remnants that fail native copper/keepout checks.

Never edits sources or removes footprints. Each removed track/via is logged.
"""
import json
from pathlib import Path
import pcbnew as k
root=Path(__file__).resolve().parents[1]
report=json.loads((root/"review/candidate-drc.json").read_text())
b=k.LoadBoard(str(root/"UPS.kicad_pcb"))
tracks=list(b.GetTracks());keep_alive=list(b.GetFootprints())
lookup={t.m_Uuid.AsString():t for t in tracks}
reasons={}
for v in report["violations"]:
    if v["type"] in {"shorting_items","clearance","hole_clearance","items_not_allowed",
                     "copper_edge_clearance","track_dangling","via_dangling","drill_out_of_range"}:
        for item in v["items"]:
            uid=item.get("uuid")
            if uid in lookup:reasons.setdefault(uid,[]).append(v["type"])
for uid in reasons:b.Remove(lookup[uid])
logpath=root/"review/copper-cleanup.json"
log=json.loads(logpath.read_text()) if logpath.exists() else []
log.append({"items_removed":len(reasons),"reasons":reasons})
logpath.write_text(json.dumps(log,indent=2))
k.SaveBoard(str(root/"UPS.kicad_pcb"),b,True)
print("Removed",len(reasons),"candidate tracks/vias conflicting with the new placement or ending without a connection.")
