#!/bin/zsh
# usage: gerbers.sh <board.kicad_pcb> <zip name>
export KICAD_DOCUMENTS_HOME="/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/kicad-user"
P="/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130"
K=/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli
G="$P/jlcpcb/_gerbers"; rm -rf "$G"; mkdir -p "$G"
perl -e 'alarm 200; exec @ARGV' $K pcb export gerbers --layers "F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.SilkS,B.SilkS,F.Mask,B.Mask,Edge.Cuts" --subtract-soldermask --no-x2 -o "$G/" "$1" < /dev/null 2>&1 | grep -v -i debug | tail -1
perl -e 'alarm 200; exec @ARGV' $K pcb export drill --format excellon --excellon-separate-th --excellon-units mm --excellon-zeros-format decimal -o "$G/" "$1" < /dev/null 2>&1 | grep -v -i debug | tail -1
(cd "$G" && rm -f *.gbrjob && rm -f "$P/jlcpcb/$2" && zip -q -j "$P/jlcpcb/$2" * && ls | tr '\n' ' ')
rm -rf "$G"; echo; ls -la "$P/jlcpcb"
