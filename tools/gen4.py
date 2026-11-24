import sys, json, subprocess, os
from kmcp import run
import commands.freerouting as fr
HERE = os.path.dirname(os.path.abspath(__file__))
LIVE = HERE + "/fr_live.log"
_orig = subprocess.run
def _patched(cmd, *a, **k):
    if isinstance(cmd, (list, tuple)) and "-de" in cmd:
        if os.environ.get("STRIP_GND"):
            dsn = cmd[list(cmd).index("-de") + 1]
            txt = open(dsn).read()
            def drop(txt, start_token):
                while True:
                    i = txt.find(start_token)
                    if i < 0: return txt
                    depth = 0; j = i
                    while True:
                        if txt[j] == "(": depth += 1
                        elif txt[j] == ")":
                            depth -= 1
                            if depth == 0: break
                        j += 1
                    txt = txt[:i] + txt[j + 1:]
            keep = set(json.load(open(HERE + "/gnd_keep.json")))
            txt = drop(txt, "(plane GND (polygon In2.Cu")
            i = txt.find("(net GND")
            j = txt.find("(pins", i); kk = txt.find(")", j)
            pins = [p for p in txt[j + 5:kk].split() if p in keep]
            txt = txt[:j] + "(pins " + " ".join(pins) + txt[kk:]
            print("router gets", len(pins), "GND pins", flush=True)
            open(dsn, "w").write(txt)
            
        cmd = [c for c in cmd if c != "--gui.enabled=false"]      # the pass limit only works with the GUI enabled
        with open(LIVE, "w") as log:
            log.write(" ".join(map(str, cmd)) + "\n"); log.flush()
            p = subprocess.Popen(list(cmd), stdout=log, stderr=subprocess.STDOUT)
            try:
                p.wait(timeout=k.get("timeout"))
            except subprocess.TimeoutExpired:
                p.kill(); raise
        return subprocess.CompletedProcess(cmd, p.returncode, open(LIVE).read(), "")
    return _orig(cmd, *a, **k)
fr.subprocess.run = _patched
PCB="/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130/density_proto.kicad_pcb"
r = run("open_project", {"filename": PCB})
print("open:", r.get("success"), flush=True)
r = run("autoroute", {"boardPath": PCB, "maxPasses": int(sys.argv[1]), "timeout": int(sys.argv[2]), "keepArtifacts": False})
print(json.dumps({k:v for k,v in r.items() if k not in ("stdout","log","freerouting_stdout")})[:1500])
