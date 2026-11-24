"""Attach the project's block 3D models to the footprints on a board file. usage: models.py <board>"""
import sys, pcbnew
MAP = {"Potentiometer_Alps_RK09K_Single_Vertical": "pot_alps_rk09k_20mm", "RotaryEncoder_Alps_EC11E-Switch_Vertical_H20mm": "encoder_alps_ec11e_20mm",
       "Fader_Alps_RS6011SP_Single": "fader_alps_rs6011sp", "Waveshare_RP2350_Zero_THT": "waveshare_rp2350_zero",
       "OLED_096in_I2C_4Pin": "oled_096_i2c_module", "Jack_3.5mm_PJ320D_Horizontal": "jack_pj320d", "LED_SK6812MINI-RV_3.7x3.5mm": "led_sk6812mini_rv"}
b = pcbnew.LoadBoard(sys.argv[1]); n = 0
for fp in b.GetFootprints():
    name = str(fp.GetFPID().GetLibItemName())
    if name in MAP:
        ms = fp.Models(); ms.clear()
        m = pcbnew.FP_3DMODEL(); m.m_Filename = "${KIPRJMOD}/DensityMidi.3dshapes/%s.step" % MAP[name]
        ms.push_back(m); n += 1
pcbnew.SaveBoard(sys.argv[1], b); print("models attached:", n)
