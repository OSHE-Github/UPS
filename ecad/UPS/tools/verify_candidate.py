"""Verify the placement deliverable against saved native KiCad evidence."""
from pathlib import Path
import collections
import json
import subprocess
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
read=lambda name:json.loads((root/name).read_text(encoding="utf-8-sig"))
r=read("review/candidate-drc.json")
counts=collections.Counter(v["type"] for v in r["violations"])
native_checks=["shorting_items","clearance","courtyards_overlap","hole_clearance","hole_to_hole",
               "npth_inside_courtyard","pth_inside_courtyard","items_not_allowed","copper_edge_clearance"]
assert not any(counts[x] for x in native_checks),{x:counts[x] for x in native_checks if counts[x]}
assert not r["schematic_parity"],r["schematic_parity"]
def nets(name):
    tree=ET.parse(root/name).getroot()
    return {n.get("name"):sorted((p.get("ref"),p.get("pin")) for p in n.findall("node")) for n in tree.findall("nets/net")}
assert nets("review/source-netlist.xml")==nets("review/candidate-netlist.xml")
baseline=read("review/source-base.json")
def git(*args):
    return subprocess.check_output(["git","-C",str(root.parents[1]),*args])
for path,blob in baseline["design_files"].items():
    assert git("rev-parse",baseline["commit"]+":"+path).decode().strip()==blob
project_from_git=json.loads(git("show",baseline["commit"]+":"+baseline["project_path"]))
assert read("UPS.kicad_pro")==project_from_git
geometry=read("review/candidate-placement.json")
fps={f["ref"]:f for f in geometry["footprints"]}
assert {r for r,f in fps.items() if f["layer"]=="B.Cu"}=={"B1","B2","B3","B4"}
holes=geometry["pi"]["holes"]
assert sorted({x for x,y in holes})==[83.5,141.5]
assert sorted({y for x,y in holes})==[103,152]
for ref in ["H1","H2","H3","H4"]:
    assert fps[ref]["pads"][0]["drill"]==[2.7,2.7]
for i in range(39,45):assert fps[f"C{i}"]["lib"]=="Capacitor_SMD:C_1210_3225Metric"
for i in range(51,58):assert fps[f"C{i}"]["lib"]=="Capacitor_SMD:C_0805_2012Metric"
summary={"status":"PLACEMENT CHECKS PASS; ROUTING AND ELECTRICAL REVIEW STILL REQUIRED",
         "native_checks":{x:counts[x] for x in native_checks},"schematic_parity_issues":0,
         "electrical_topology_matches_source":True,"source_base_preserved_in_git":baseline["commit"],
         "source_project_settings_preserved":True,"footprints":len(fps),
         "retained_or_translated_tracks_and_vias":len(geometry["tracks"]),
         "unconnected_items":len(r["unconnected_items"]),"remaining_drc":dict(sorted(counts.items())),
         "pi_hole_spacing_mm":[58,49],"pi_mount_holes_mm":holes,"bottom_components":["B1","B2","B3","B4"]}
(root/"review/validation-summary.json").write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
