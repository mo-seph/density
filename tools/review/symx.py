import re, sys
def block(text, start):
    d = 0
    for i in range(start, len(text)):
        if text[i] == "(": d += 1
        elif text[i] == ")":
            d -= 1
            if d == 0: return text[start:i+1]
def sym(text, name):
    m = re.search(r'\(symbol "%s"\s' % re.escape(name), text)
    return block(text, m.start()) if m else None
def pins(b):
    return [(m.group(4), m.group(5), float(m.group(1)), float(m.group(2)), int(m.group(3))) for m in re.finditer(r'\(pin \w+ \w+\s*\(at ([-\d.]+) ([-\d.]+) (\d+)\)\s*\(length [-\d.]+\)\s*\(name "([^"]*)".*?\(number "([^"]*)"', b, re.S)]
if __name__ == "__main__":
    lib = open("/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols/Device.kicad_sym").read()
    sch = open(sys.argv[1]).read()
    for stock, emb in (("R_Potentiometer_MountingPin", "Device:R_Potentiometer"), ("RotaryEncoder_Switch_MP", "Device:RotaryEncoder_Switch")):
        b = sym(lib, stock); e = sym(sch, emb)
        print(stock, "found" if b else "MISSING", "| extends:", "extends" in (b or ""), "| len", len(b or ""))
        print("   stock pins:", pins(b)); print("   embedded  :", pins(e))
