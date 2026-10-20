"""Verify the placement deliverable against saved native KiCad evidence."""
from pathlib import Path
import collections
import hashlib
import json
import subprocess
import xml.etree.ElementTree as ET
root=Path(__file__).resolve().parents[1]
read=lambda name:json.loads((root/name).read_text(encoding="utf-8-sig"))
r=read("review/candidate-drc.json")
counts=collections.Counter(v["type"] for v in r["violations"])
native_checks=["shorting_items","clearance","courtyards_overlap","hole_clearance","hole_to_hole",
               "npth_inside_courtyard","pth_inside_courtyard","items_not_allowed","copper_edge_clearance",
               "tracks_crossing","solder_mask_bridge","starved_thermal","connection_width","track_dangling"]
assert not any(counts[x] for x in native_checks),{x:counts[x] for x in native_checks if counts[x]}
assert not r["schematic_parity"],r["schematic_parity"]
def nets(name):
    tree=ET.parse(root/name).getroot()
    return {n.get("name"):sorted((p.get("ref"),p.get("pin")) for p in n.findall("node")) for n in tree.findall("nets/net")}
source_nets=nets("review/source-netlist.xml")
candidate_nets=nets("review/candidate-netlist.xml")
expected_nets={name:list(nodes) for name,nodes in source_nets.items()}
approved=read("review/approved-net-changes.json") if (root/"review/approved-net-changes.json").exists() else []
for change in approved:
    nodes=[tuple(node) for node in change["nodes"]]
    if change.get("mode")=="merge":
        assert change["to_net"] in expected_nets
    else:
        assert change["to_net"] not in expected_nets
    for node in nodes:expected_nets[change["from_net"]].remove(node)
    expected_nets[change["to_net"]]=sorted(expected_nets.get(change["to_net"],[])+nodes)
    if change.get("mode")=="merge" and not expected_nets[change["from_net"]]:del expected_nets[change["from_net"]]
assert expected_nets==candidate_nets,"Unexpected electrical topology change"
baseline=read("review/source-base.json")
def git(*args):
    return subprocess.check_output(["git","-C",str(root.parents[1]),*args])
for path,blob in baseline["design_files"].items():
    assert git("rev-parse",baseline["commit"]+":"+path).decode().strip()==blob
project_from_git=json.loads(git("show",baseline["commit"]+":"+baseline["project_path"]))
project=read("UPS.kicad_pro")
# The user changed schematic editor settings. Preserve them; board rules and
# stack constraints must still match the team's baseline.
assert {key:value for key,value in project.items() if key!="schematic"}=={
    key:value for key,value in project_from_git.items() if key!="schematic"}
geometry=read("review/candidate-placement.json")
fps={f["ref"]:f for f in geometry["footprints"]}
assert {r for r,f in fps.items() if f["layer"]=="B.Cu"}=={"B1","B2","B3","B4","J2","J5","J6","J11"}
holes=geometry["pi"]["holes"]
assert sorted({x for x,y in holes})==[83.5,141.5]
assert sorted({y for x,y in holes})==[103,152]
for ref in ["H1","H2","H3","H4"]:
    assert fps[ref]["pads"][0]["drill"]==[2.7,2.7]
for i in range(39,45):assert fps[f"C{i}"]["lib"]=="Capacitor_SMD:C_1210_3225Metric"
for i in range(51,58):assert fps[f"C{i}"]["lib"]=="Capacitor_SMD:C_0805_2012Metric"
power=read("review/main-power-connectivity.json")
assert power["board_sha256"]==hashlib.sha256((root/"UPS.kicad_pcb").read_bytes()).hexdigest(),"Power-path evidence is stale"
assert all(path["connected_without_small_signal_tracks"] for path in power["paths"])
usb=read("review/usb-routing-pass.json")
assert usb["board_sha256"]==power["board_sha256"],"USB evidence is stale"
assert all(net["all_pads_connected"] for net in usb["usb_related_nets"])
assert usb["schematics_unchanged_during_pass"] and usb["project_settings_unchanged_during_pass"]
room=read("review/bq-routing-room.json")
finish_path=root/"review/bq-routing-finish.json"
if finish_path.exists():
    finish=read("review/bq-routing-finish.json")
    if (root/"review/fab-review.json").exists():
        fab=read("review/fab-review.json")
        assert fab["board_sha256"]==power["board_sha256"],"Fabrication review evidence is stale"
        assert fab["all_previous_pad_connections_preserved"] and fab["all_existing_tracks_preserved"]
        assert fab["positions_and_footprints_unchanged"] and fab["project_settings_unchanged"]
        assert fab["only_expected_value_change"]=={"C9":["470pF","470nF"]}
        assert all(n["all_pads_connected"] for n in fab["all_nets"])
        assert fab["unconnected_items"]==0 and not r["unconnected_items"]
    elif (root/"review/controls-cleanup.json").exists():
        controls=read("review/controls-cleanup.json")
        assert controls["board_sha256"]==power["board_sha256"],"Control routing evidence is stale"
        assert controls["all_previous_pad_connections_preserved"] and controls["values_and_footprints_unchanged"]
        assert controls["other_footprint_positions_unchanged"] and controls["project_settings_unchanged"]
        assert all(n["all_pads_connected"] for n in controls["all_nets"])
        assert controls["unconnected_items_after"]==0 and not r["unconnected_items"]
    elif (root/"review/stm-routing-room.json").exists():
        stm=read("review/stm-routing-room.json")
        assert stm["board_sha256"]==power["board_sha256"],"STM movement evidence is stale"
        assert stm["before_board_sha256"]==finish["board_sha256"],"STM baseline does not match BQ checkpoint"
        assert stm["all_previous_pad_connections_preserved"] and stm["values_footprints_pad_nets_unchanged"]
        assert stm["other_footprint_positions_unchanged"] and all(n["all_pads_connected"] for n in stm["u1_nets"])
    else:
        assert finish["board_sha256"]==power["board_sha256"],"BQ routing evidence is stale"
    assert finish["bq_all_nets_fully_connected"] and all(n["all_pads_connected"] for n in finish["u1_nets"])
    assert finish["all_previous_pad_connections_preserved"]
    assert finish["values_footprints_positions_and_pad_nets_unchanged"]
    assert finish["schematics_unchanged"] and finish["project_settings_unchanged"]
else:
    assert room["board_sha256"]==power["board_sha256"],"BQ placement evidence is stale"
assert room["all_previous_pad_connections_preserved"]
assert room["values_footprints_and_pad_nets_unchanged"] and room["schematics_unchanged"] and room["project_unchanged"]
summary={"status":"ALL EXISTING NETS ROUTED; NOT READY FOR FABRICATION: COMPONENT, CURRENT/THERMAL AND MANUFACTURING FINDINGS REMAIN",
         "native_checks":{x:counts[x] for x in native_checks},"schematic_parity_issues":0,
         "electrical_topology_matches_source":source_nets==candidate_nets,
         "approved_net_changes":[change["id"] for change in approved],
         "no_unapproved_topology_changes":True,"source_base_preserved_in_git":baseline["commit"],
         "source_board_and_net_settings_preserved":True,
         "user_schematic_settings_preserved":True,
         "main_power_paths_connected_without_small_signal_tracks":len(power["paths"]),
         "usb_related_nets_fully_connected":len(usb["usb_related_nets"]),
         "bq_support_parts_repositioned":len(room["moved"]),
         "bq_nets_fully_connected":len(finish["u1_nets"]) if finish_path.exists() else None,
         "saved_user_route_items_preserved":room["saved_user_route_items_preserved"],
         "footprints":len(fps),
         "total_tracks_and_vias":len(geometry["tracks"]),
         "unconnected_items":len(r["unconnected_items"]),"remaining_drc":dict(sorted(counts.items())),
         "pi_hole_spacing_mm":[58,49],"pi_mount_holes_mm":holes,"bottom_components":sorted(ref for ref,f in fps.items() if f["layer"]=="B.Cu")}
(root/"review/validation-summary.json").write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
