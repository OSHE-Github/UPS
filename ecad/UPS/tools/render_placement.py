"""Render native PCB inventory as an annotated placement review, not fabrication art."""
import json, sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

data=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
scale=9
origin=(40,28)
size=(1390,1530)
im=Image.new("RGB",size,"#101923")
d=ImageDraw.Draw(im)
font=ImageFont.truetype("C:/Windows/Fonts/arial.ttf",12)
big=ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf",22)
small=ImageFont.truetype("C:/Windows/Fonts/arial.ttf",11)
def xy(p):return (int((p[0]-origin[0])*scale+28),int((p[1]-origin[1])*scale+64))
def rect(b):
    x,y,w,h=b
    return [xy((x,y)),xy((x+w,y+h))]
colors={"/Charger/":"#f9ad60","/BoostConvertor/":"#66d6c1","/Output/":"#cf9cff","/controller/":"#89baff","":"#e0e4ec"}
d.text((28,16),"UPS — "+("BASELINE" if "original" in sys.argv[1] else "PLACEMENT CANDIDATE"),font=big,fill="#ffffff")
d.text((28,44),"Top view • bottom holder outlines • simplified KiCad geometry • routing incomplete",font=font,fill="#acbac8")
d.rectangle(rect(data["outline"]),fill="#172d30",outline="#f0eee5",width=2)
for f in data["footprints"]:
    if f["ref"].startswith("B") and f["ref"][1:].isdigit():
        b=f["box"]; d.rectangle(rect(b),outline="#577578",width=2)
        d.text(xy((b[0]+1,b[1]+b[3]-3)),f["ref"]+" • bottom battery",font=font,fill="#84a9ac")
for t in data["tracks"]:
    col={"F.Cu":"#634343","In1.Cu":"#374853","In2.Cu":"#3f4358","B.Cu":"#394c53"}.get(t["layer"],"#404040")
    d.line([xy(t["start"]),xy(t["end"])],fill=col,width=max(1,int(t["width"]*scale)))
for f in data["footprints"]:
    if f["ref"].startswith("B") and f["ref"][1:].isdigit():continue
    col=colors.get(f["sheet"],"#e0e4ec")
    d.rectangle(rect(f["box"]),outline=col,width=1)
    for p in f["pads"]:
        x,y=p["at"]; w,h=p["size"]
        if round(p["angle"])%180==90:w,h=h,w
        if p["drill"][0]:
            radius=max(2,int(p["drill"][0]*scale/2)); a,b=xy((x,y))
            d.ellipse((a-radius,b-radius,a+radius,b+radius),fill="#101923",outline=col)
        else:
            d.rectangle([xy((x-w/2,y-h/2)),xy((x+w/2,y+h/2))],fill=col)
    x,y=f["at"]
    d.text(xy((x+1,y-1.8)),f["ref"],font=small,fill=col,stroke_width=1,stroke_fill="#101923")
if "pi" in data:
    pi=data["pi"]
    d.rectangle(rect(pi["body"]),outline="#ffdf73",width=2)
    for x,y in pi["holes"]:
        a,b=xy((x,y));r=round(3.5*scale)
        d.ellipse((a-r,b-r,a+r,b+r),outline="#ffdf73",width=2)
    d.text(xy((pi["body"][0]+2,pi["body"][1]+2)),"Pi 5 envelope • 58 × 49 mm centres",font=font,fill="#ffdf73")
legend_y=1450
for i,(name,col) in enumerate([( "Charger",colors["/Charger/"]),("Boost",colors["/BoostConvertor/"]),("USB-C output",colors["/Output/"]),("MCU / interfaces",colors["/controller/"]) ]):
    d.text((28+i*310,legend_y),name,font=big,fill=col)
d.text((28,1490),"Review candidate only. Copper sizing awaits team current/thermal calculations; mechanical stack needs physical fit check.",font=font,fill="#d0d7df")
im.save(sys.argv[2])
print(sys.argv[2])
