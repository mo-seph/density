#!/bin/zsh
# usage: drc.sh <out.json> [extra kicad-cli args]
S=/private/tmp/claude-373960657/-Users-dmurrayrust-Dropbox-Projects-Fabrication-Density-Midi/d6715d97-f485-4e61-9dec-b6ee8ab73ff6/scratchpad/dm
export KICAD_DOCUMENTS_HOME="/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/kicad-user"
cd "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130"
out=$1; shift
perl -e 'alarm 240; exec @ARGV' /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb drc --format json --severity-all "$@" -o $out density_proto.kicad_pcb < /dev/null 2>&1 | tail -2
python3 - $out <<'PY'
import json,collections,sys
d=json.load(open(sys.argv[1]))
v=d.get("violations",[])
c=collections.Counter((x["severity"],x["type"]) for x in v)
for k,n in c.most_common(): print(n,k)
skip=("lib_footprint_issues","lib_footprint_mismatch","silk_overlap","silk_over_copper","silk_edge_clearance","text_height","text_thickness")
seen=collections.Counter()
for x in v:
    if x["type"] in skip: continue
    seen[x["type"]]+=1
    if seen[x["type"]]<=6: print(" ",x["type"],"|",x["description"][:70],"|",[i["description"][:55] for i in x["items"]])
u=d.get("unconnected_items",[])
print("unconnected:",len(u))
nets=collections.Counter()
import re
for x in u:
    for i in x["items"]:
        m=re.search(r"\[([^\]]+)\]",i["description"])
        if m: nets[m.group(1)]+=1; break
print(nets.most_common(12))
PY
