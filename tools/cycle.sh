#!/bin/zsh
# place -> route -> pour -> check.  usage: cycle.sh <tag>
S=/private/tmp/claude-373960657/-Users-dmurrayrust-Dropbox-Projects-Fabrication-Density-Midi/d6715d97-f485-4e61-9dec-b6ee8ab73ff6/scratchpad/dm
PY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/Resources/Python.app/Contents/MacOS/Python
export KICAD_DOCUMENTS_HOME="/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/kicad-user"
PCB="/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/density_proto.kicad_pcb"
cd $S
python3 -c "
import json
p='/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/density_proto.kicad_pro'; d=json.load(open(p))
for c in d['net_settings']['classes']: c['clearance']=0.15
json.dump(d,open(p,'w'),indent=2)"
cp $S/base_v2.kicad_pcb "$PCB"
$PY -u gen3.py > gen3.log 2>&1 || { echo "place failed"; tail -3 gen3.log; exit 1; }
$PY -u ledstub.py 2>&1 | grep -E "stubs|Error|assert"
cp "$PCB" $S/placed_$1.kicad_pcb
rm -f gen4.log fr_live.log
STRIP_GND=1 $PY -u gen4.py ${2:-25} 900 > gen4.log 2>&1
grep -o '"message": "[^"]*"' gen4.log | tail -1; grep -o 'pass #[0-9]* .*unrouted)' fr_live.log | tail -1 | sed -E "s/on board '[0-9a-f]*' was completed in//" | cut -c1-90
cp "$PCB" $S/routed_$1.kicad_pcb
$PY -u ledstub.py 2>&1 | grep -E "stubs|Error|assert"
python3 -c "
import json
p='/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/density_proto.kicad_pro'; d=json.load(open(p))
for c in d['net_settings']['classes']: c['clearance']=0.14
json.dump(d,open(p,'w'),indent=2)"
$PY -u gen6.py > gen6.log 2>&1; grep -v -E "Debug|memory leak|wxApp" gen6.log | tail -2 | head -1
for n in 1 2 3; do
  $S/drc.sh $S/drc_$1.json > /dev/null
  $PY -u finish2.py repair $S/drc_$1.json 2>&1 | grep -E "removed"
  $PY -u finish2.py all 2>&1 | grep -v -E "Debug|wxApp|memory leak|^  joined" | cut -c1-400
  $S/drc.sh $S/drc_$1.json | grep -E "^unconnected|^[0-9]+ \\('error'"
  left=$(python3 -c "import json; d=json.load(open('$S/drc_$1.json')); print(len(d['unconnected_items'])+sum(1 for v in d['violations'] if v['type'] in ('shorting_items','clearance','tracks_crossing','hole_clearance')))")
  echo "finishing pass $n: $left problem(s) left"
  [ "$left" = "0" ] && break
done
$S/drc.sh $S/drc_$1.json | grep -v -E "silk|^Saved"
echo CYCLE_DONE
