import sys, json, logging, os
sys.path.insert(0, "/Users/dmurrayrust/Applications/KiCAD-MCP-Server/python")
logging.disable(logging.CRITICAL)
_real_stdout = sys.stdout
sys.stdout = open(os.devnull, "w")
from kicad_interface import KiCADInterface
_iface = KiCADInterface()
sys.stdout = _real_stdout
def run(cmd, params=None, quiet=False):
    so = sys.stdout; sys.stdout = open(os.devnull, "w")
    try:
        r = _iface.handle_command(cmd, params or {})
    finally:
        sys.stdout = so
    if not quiet and not (isinstance(r, dict) and r.get("success", True)):
        print("FAIL", cmd, json.dumps(r)[:600])
    return r
