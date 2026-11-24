"""Build the schematic (single A1 sheet, connectivity by net labels)."""
import os, json, sys, shutil
from kmcp import run

PROJ_DIR = "/Users/dmurrayrust/Dropbox/Projects/Fabrication/Density Midi/proto-125x130"
NAME = "density_proto"
SCH = f"{PROJ_DIR}/{NAME}.kicad_sch"

# start from the empty schematic every time
EMPTY = '''(kicad_sch (version 20260101) (generator "eeschema") (generator_version "10.0")

  (uuid 26998239-2857-441e-8ec2-0b70f1294177)

  (paper "A1")

  (lib_symbols
  )

  (sheet_instances
    (path "/" (page "1"))
  )
)
'''
open(SCH, "w").write(EMPTY)

FP = {
    "pot": "Potentiometer_THT:Potentiometer_Alps_RK09K_Single_Vertical",
    "fader": "DensityMidi:Fader_Alps_RS6011SP_Single",
    "c0402": "Capacitor_SMD:C_0402_1005Metric",
    "c0805": "Capacitor_SMD:C_0805_2012Metric",
    "r0402": "Resistor_SMD:R_0402_1005Metric",
    "rn": "Resistor_SMD:R_Array_Convex_4x0603",
    "fb": "Inductor_SMD:L_0805_2012Metric",
    "sw": "Button_Switch_SMD:SW_SPST_PTS645Sx43SMTR92",
    "led": "DensityMidi:LED_SK6812MINI-RV_3.7x3.5mm",
    "ssop24": "Package_SO:SSOP-24_5.3x8.2mm_P0.65mm",
    "soic16": "Package_SO:SOIC-16_3.9x9.9mm_P1.27mm",
    "soic8": "Package_SO:SOIC-8_3.9x4.9mm_P1.27mm",
    "sot235": "Package_TO_SOT_SMD:SOT-23-5",
    "zero": "DensityMidi:Waveshare_RP2350_Zero_THT",
    "oled": "DensityMidi:OLED_096in_I2C_4Pin",
    "jp": "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm",
    "jack": "Connector_Audio:Jack_3.5mm_PJ320D_Horizontal",
    "hdr8": "Connector_PinHeader_2.54mm:PinHeader_1x08_P2.54mm_Vertical",
    "tp": "TestPoint:TestPoint_Pad_D1.5mm",
    "hole": "DensityMidi:MountingHole_M3_tight",
    "enc": "Rotary_Encoder:RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm",
}

comps = []
cnt = {}
def ref(prefix):
    cnt[prefix] = cnt.get(prefix, 0) + 1
    return f"{prefix}{cnt[prefix]}"
def add(sym, r, val, fp, x, y, nets=None, rot=0):
    comps.append({"symbol": sym, "reference": r, "value": val, "footprint": FP[fp], "position": {"x": x, "y": y}, "rotation": rot, "nets": nets or {}})
    return r
def cap(val, a, b, x, y, fp="c0402"):
    return add("Device:C", ref("C"), val, fp, x, y, {"1": a, "2": b})
def res(val, a, b, x, y):
    return add("Device:R", ref("R"), val, "r0402", x, y, {"1": a, "2": b})
def tp(net, x, y, name):
    return add("Connector:TestPoint", ref("TP"), name, "tp", x, y, {"1": net})

# ---- brain ---------------------------------------------------------------
modnets = {"5V": "+5V", "GND": "GND", "3V3": "+3V3", "GP29": "GP29", "GP28": "GP28", "GP27": "GP27", "GP26": "GP26",
           "GP15": "SCL", "GP14": "SDA", "GP13": "LED_DATA", "GP12": "SR_DATA", "GP11": "SR_LOAD", "GP10": "SR_CLK",
           "GP9": "MUX_S3", "GP8": "MUX_S2", "GP7": "MUX_S1", "GP6": "MUX_S0", "GP5": "ADC_CS", "GP4": "ADC_MISO",
           "GP3": "ADC_MOSI", "GP2": "ADC_SCK", "GP1": "GP1", "GP0": "GP0"}
add("DensityMidi:RP2350_Zero", "A1", "RP2350-Zero", "zero", 80, 60, modnets)
add("Connector_Generic:Conn_01x08", "J5", "SPARE_IO", "hdr8", 160, 60,
    {"1": "+3V3", "2": "GND", "3": "GP0", "4": "GP1", "5": "GP26", "6": "GP27", "7": "GP28", "8": "GP29"})

# ---- power ----------------------------------------------------------------
add("Device:FerriteBead", "FB1", "600R@100MHz", "fb", 40, 125, {"1": "+3V3", "2": "AVDD"})
cap("10uF", "AVDD", "GND", 60, 125, "c0805")
cap("10uF", "+3V3", "GND", 80, 125, "c0805")
cap("22uF", "+5V", "GND", 100, 125, "c0805")
cap("22uF", "+5V", "GND", 120, 125, "c0805")
add("power:PWR_FLAG", "#FLG1", "PWR_FLAG", "tp", 145, 125, {"1": "GND"})
add("power:PWR_FLAG", "#FLG2", "PWR_FLAG", "tp", 165, 125, {"1": "AVDD"})

# ---- pots -----------------------------------------------------------------
for i in range(16):
    x, y = 45 + 50 * (i % 4), 180 + 32 * (i // 4)
    add("Device:R_Potentiometer", f"RV{i+1}", "10k lin RK09K1130AAU", "pot", x, y, {"1": "GND", "2": f"POT{i+1}", "3": "AVDD"})
    cap("10nF", f"POT{i+1}", "GND", x + 22, y)
add("Device:R_Potentiometer", "RV17", "10k lin RS6011SP6003", "fader", 45, 320, {"1": "GND", "2": "FADER1", "3": "AVDD"})
cap("10nF", "FADER1", "GND", 67, 320)

# ---- pedal jacks ------------------------------------------------------------
for j in range(2):
    y = 320 + 45 * j
    n = j + 1
    add("Connector_Audio:AudioJack4", f"J{3+j}", f"PEDAL{n}", "jack", 120, y, {"T": f"PED{n}_TIP", "R1": f"PED{n}_RING", "R2": "GND", "S": "GND"})
    res("470R", "AVDD", f"PED{n}_RING", 160, y)
    res("1k", f"PED{n}_TIP", f"PEDAL{n}", 180, y)
    res("100k", f"PEDAL{n}", "GND", 200, y)
    cap("10nF", f"PEDAL{n}", "GND", 220, y)

# ---- analogue front end -------------------------------------------------------
mux = {"COM": "MUXA", "S0": "MUX_S0", "S1": "MUX_S1", "S2": "MUX_S2", "S3": "MUX_S3", "~{E}": "GND", "GND": "GND", "VCC": "AVDD"}
for i in range(16):
    mux[f"I{i}"] = f"POT{i+1}"
add("74xx:CD74HC4067SM", "U1", "CD74HC4067SM96", "ssop24", 300, 230, mux)
cap("100nF", "AVDD", "GND", 330, 190)
res("100R", "BUFA", "ADC0", 400, 205)
cap("1nF", "ADC0", "GND", 420, 205)
res("100R", "BUFB", "ADC1", 400, 250)
cap("1nF", "ADC1", "GND", 420, 250)
cap("100nF", "AVDD", "GND", 400, 290)
adc = {"CH0": "ADC0", "CH1": "ADC1", "CH2": "PEDAL1", "CH3": "PEDAL2", "CH4": "GND", "CH5": "GND", "CH6": "GND", "CH7": "GND",
       "DGND": "GND", "AGND": "GND", "Vref": "AVDD", "Vdd": "AVDD", "CLK": "ADC_SCK", "Din": "ADC_MOSI", "Dout": "ADC_MISO", "~{CS}/SHDN": "ADC_CS"}
add("Analog_ADC:MCP3208", "U3", "MCP3208T-CI/SL", "soic16", 470, 230, adc)
cap("100nF", "AVDD", "GND", 455, 190)
cap("1uF", "AVDD", "GND", 475, 190)

# ---- buttons, encoder, shift registers ---------------------------------------
for i in range(20):
    x, y = 540 + 32 * (i % 5), 50 + 14 * (i // 5)
    add("Switch:SW_Push", f"SW{i+1}", "6x6 tact", "sw", x, y, {"1": f"BTN{i+1}", "2": "GND"})
add("Device:RotaryEncoder_Switch", "ENC1", "EC11E15244B2", "enc", 730, 60, {"A": "ENC_A", "B": "ENC_B", "C": "GND", "S1": "ENC_SW", "S2": "GND"})
cap("10nF", "ENC_A", "GND", 765, 60)
cap("10nF", "ENC_B", "GND", 785, 60)
srin = [f"BTN{i+1}" for i in range(20)] + ["ENC_A", "ENC_B", "ENC_SW", "SR_SPARE"]
chain_out = ["SR_DATA", "SR_CH1", "SR_CH2"]
chain_in = ["SR_CH1", "SR_CH2", "GND"]
for u in range(3):
    x = 550 + 75 * u
    nets = {"~{PL}": "SR_LOAD", "CP": "SR_CLK", "~{CE}": "GND", "GND": "GND", "VCC": "+3V3", "Q7": chain_out[u], "DS": chain_in[u]}
    for b in range(8):
        nets[f"D{b}"] = srin[u * 8 + b]
    add("74xx:74HC165", f"U{5+u}", "74HC165D", "soic16", x, 165, nets)
    cap("100nF", "+3V3", "GND", x + 25, 130)
for k in range(6):
    n = srin[k * 4:k * 4 + 4]
    add("Device:R_Pack04", f"RN{k+1}", "10k x4", "rn", 540 + 30 * k, 235,
        {"1": n[0], "2": n[1], "3": n[2], "4": n[3], "5": "+3V3", "6": "+3V3", "7": "+3V3", "8": "+3V3"})

# ---- LEDs ------------------------------------------------------------------------
add("74xGxx:74AHCT1G125", "U8", "74AHCT1G125GV", "sot235", 470, 330, {"1": "GND", "2": "LED_DATA", "3": "GND", "4": "LED_5V", "5": "+5V"})
cap("100nF", "+5V", "GND", 470, 365)
res("33R", "LED_5V", "LED_DIN", 505, 330)
for i in range(20):
    x, y = 550 + 50 * (i % 5), 300 + 45 * (i // 5)
    din = "LED_DIN" if i == 0 else f"LEDC{i}"
    dout = "LED_END" if i == 19 else f"LEDC{i+1}"
    add("DensityMidi:SK6812MINI_RV", f"D{i+1}", "SK6812MINI-RV", "led", x, y, {"DIN": din, "DOUT": dout, "VDD": "+5V", "GND": "GND"})
    cap("100nF", "+5V", "GND", x + 22, y)

# ---- OLED headers ------------------------------------------------------------------
for j in range(2):
    y = 60 + 50 * j + 430 - 430
    yy = 520 + 40 * j
    n = j + 1
    add("Connector_Generic:Conn_01x04", f"J{n}", f"OLED{n}", "oled", 560, yy, {"1": f"OLED{n}_P1", "2": f"OLED{n}_P2", "3": "SCL", "4": "SDA"})
    add("Jumper:SolderJumper_3_Bridged12", f"JP{2*j+1}", "P1=GND", "jp", 600, yy - 8, {"1": "GND", "2": f"OLED{n}_P1", "3": "+3V3"})
    add("Jumper:SolderJumper_3_Bridged12", f"JP{2*j+2}", "P2=3V3", "jp", 600, yy + 12, {"1": "+3V3", "2": f"OLED{n}_P2", "3": "GND"})
res("4.7k", "+3V3", "SCL", 650, 520)
res("4.7k", "+3V3", "SDA", 670, 520)

# ---- test points and mounting holes ---------------------------------------------------
for k, (net, nm) in enumerate([("+5V", "5V"), ("+3V3", "3V3"), ("AVDD", "AVDD"), ("GND", "GND"), ("MUXA", "MUXA"), ("ADC0", "ADC0"),
                               ("ADC1", "ADC1"), ("LED_DIN", "LED_DIN"), ("LED_END", "LED_END"), ("SR_DATA", "SR_DATA"),
                               ("SCL", "SCL"), ("SDA", "SDA"), ("ADC_SCK", "ADC_SCK"), ("ADC_MISO", "ADC_MISO")]):
    tp(net, 45 + 18 * k, 440, nm)
for k in range(6):
    add("Mechanical:MountingHole", f"H{k+1}", "M3", "hole", 45 + 18 * k, 480)

# ---------------------------------------------------------------- send
def send(batch):
    r = run("batch_add_and_connect", {"schematicPath": SCH, "components": batch})
    ok = r.get("success")
    msg = json.dumps(r)
    print(len(batch), "comps ->", ok, msg[:300] if not ok else "", flush=True)
    for key in ("errors", "failed", "warnings"):
        if r.get(key):
            print("  ", key, json.dumps(r[key])[:1500])
    return r

for i in range(0, len(comps), 30):
    send(comps[i:i + 30])

# op-amp: three units sharing one reference
r = run("batch_add_components", {"schematicPath": SCH, "components": [
    {"symbol": "Amplifier_Operational:MCP6002-xSN", "reference": "U4", "value": "MCP6002T-I/SN", "footprint": FP["soic8"], "position": {"x": 360, "y": 205}, "unit": 1},
    {"symbol": "Amplifier_Operational:MCP6002-xSN", "reference": "U4", "value": "MCP6002T-I/SN", "footprint": FP["soic8"], "position": {"x": 360, "y": 250}, "unit": 2},
    {"symbol": "Amplifier_Operational:MCP6002-xSN", "reference": "U4", "value": "MCP6002T-I/SN", "footprint": FP["soic8"], "position": {"x": 360, "y": 295}, "unit": 3}]})
print("opamp add:", r.get("success"), json.dumps(r)[:200] if not r.get("success") else "")
r = run("batch_connect", {"schematicPath": SCH, "connections": {"U4": {"1": "BUFA", "2": "BUFA", "3": "MUXA", "5": "FADER1", "6": "BUFB", "7": "BUFB", "4": "GND", "8": "AVDD"}}})
print("opamp connect:", r.get("success"), json.dumps(r)[:600])
r = run("batch_add_no_connects", {"schematicPath": SCH, "pins": [{"componentRef": f"U{u}", "pinName": "~{Q7}"} for u in (5, 6, 7)]})
print("no connects:", r.get("success"), json.dumps(r)[:300] if not r.get("success") else "")
print("total components:", len(comps) + 1)
json.dump(comps, open(os.path.join(os.path.dirname(__file__), "comps.json"), "w"))
