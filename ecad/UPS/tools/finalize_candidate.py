"""Synchronize descriptive fields and mark mechanical-only board items."""
import json
from pathlib import Path
import xml.etree.ElementTree as ET
import pcbnew as k
root=Path(__file__).resolve().parents[1]
b=k.LoadBoard(str(root/"UPS.kicad_pcb"))
comps={c.get("ref"):c for c in ET.parse(root/"review/source-netlist.xml").getroot().findall("components/comp")}
fps=list(b.GetFootprints())
for f in fps:
    ref=f.GetReference()
    if ref in comps:
        for fld in comps[ref].findall("fields/field"):
            if fld.get("name") not in {"Description","Datasheet"}:continue
            field=f.GetField(fld.get("name"))
            if field is not None:field.SetText(fld.text or "")
    elif ref in {"H1","H2","H3","H4"}:
        f.SetAttributes(f.GetAttributes()|k.FP_BOARD_ONLY|k.FP_EXCLUDE_FROM_BOM|k.FP_EXCLUDE_FROM_POS_FILES)
b.GetTitleBlock().SetTitle("UPS - persistent placement candidate")
b.GetTitleBlock().SetComment(0,"Review placement only - electrical/BOM issues remain; not for fabrication")
# Three orphaned ground vias from the old output location have no routed
# endpoints. Remove those remnants without changing fabrication constraints.
tracks=list(b.GetTracks());removed=[]
orphan_ids={"1a1802c4-2ea9-4cb6-8e15-ad71f330cd67","5232628e-56c8-4e88-b989-0a2f59098b1a","f66ea266-3497-49eb-8041-cff87bdf3378"}
for t in tracks:
    if t.m_Uuid.AsString() in orphan_ids:
        removed.append(t.m_Uuid.AsString());b.Remove(t)
k.SaveBoard(str(root/"UPS.kicad_pcb"),b,True)
(root/"review/finalize-actions.json").write_text(json.dumps({"removed_orphan_ground_vias":removed},indent=2))
print("Metadata synchronized; board-only mounts marked; removed",len(removed),"orphan ground vias.")
