# Density Controller Test v0.1 (2026-11-17), 125 × 130 mm

16 pots, 1 fader, 20 buttons each with an RGB LED beside it, 1 push encoder, 2 OLED headers, 2 pedal jacks.
Brain is a plug-in Waveshare RP2350-Zero. Everything is on the front; 4 layers (one inner layer is a solid ground plane).
Name, version and date are on the silkscreen: small on the front left margin, large on the back.

## State

- Routed. KiCad's rule check reports 0 unconnected items and 0 errors. The only warnings left are cosmetic: silkscreen text overlapping other text and pads, and the 14 test points differing from their library footprint (their rings were removed to make room for labels).
- No dangling track ends or one-layer vias remain; every track end lands exactly on a pad, a via, another track end or a T-junction.
- Routing was done by Freerouting plus scripts, not by hand. It is electrically complete but not tidy. Signals are 0.15 mm with 0.15 mm clearances; supply tracks are 0.3 mm widened to 0.5 mm (3V3, AVDD) or 0.8 mm (5 V) wherever the neighbours allow.
- Board and schematic agree pad for pad, and since the schematic was redrawn (2026-11-18) the net names are identical too. KiCad's parity check now reports only field differences (descriptions, BOM flags) and the three unused ~Q7 pins of the shift registers.

## Design review, 2026-11-17

An independent pass over the netlist against the datasheets found no blockers: every chip pin, the shift-register chain, the LED chain and the module pin functions are right. Layout was checked by script (supply drop, plane integrity, clearances round mounting holes, how every track end is joined).

Fixed on the board:

| Finding | Fix |
|---|---|
| Metal frames of all pots, the fader and the encoder were floating (ESD path, hum) | All 37 mounting-lug pads are now on GND, in the schematic too |
| The LED level shifter's enable pin (U8 pin 1) was grounded only through a patch of copper pour | Own via to the ground plane |
| All LED current ran through one 0.3 mm track: 207 mV drop at full white | 5 V tracks widened up to 0.8 mm; drop now 124 mV |
| An AVDD track and a button track ran under the screw heads at H5 and H6 | Moved to 3.4 mm or more from the hole centres |
| Seven leftover track tails and five vias that went nowhere | Removed or cut back to their junctions |
| Fader lug hole position disputed between Alps' drawing and Alps' 3D model | Hole at the four-pin end is now a 2.1 × 2.9 mm slot that takes either |

Left as they are, and why:

| Finding | Why it is left |
|---|---|
| Decoupling capacitors are 7–11 mm from the supply pins of the ADC (U3) and the three shift registers (U5–U7) | Each is fed by a track directly over the ground plane 0.2 mm below, which adds only a few nH. Fine at these speeds. Moving them means hand-rerouting in the densest part of the board; do it on the next revision. |
| 44 µF across USB 5 V (C3 + C4) is over the USB limit of 10 µF | Usually harmless; if a hub resets on plug-in, remove C4. |
| No hardware limit on LED current | See firmware notes. |
| Pedal inputs have no clamp | Expression pedals only. Do not feed them CV or audio. |

Checked against the makers' own documents on 2026-11-18: the RP2350-Zero pin order (Waveshare's pinout picture and dimension drawing), the PJ-320D jack's pad-to-contact order (Hanxia datasheet) and the tact switch's internal pairing and lead span (HRO datasheet). All three agree with the board.

## Second review, 2026-11-18

A second independent pass looked only at the layout and the manufacturing files, and every BOM line was compared with JLCPCB's own footprint. No blockers.

Fixed:

| Finding | Fix |
|---|---|
| The level shifter U8 was a quarter turn out in the placement file (wrong rotation offset for SOT-23-5) | Corrected; resistor packs also now match JLCPCB's pin numbering |
| An inner-layer mux address track (MUX_S1) ran 0.28 mm from the wall of mounting hole H5 | Moved to 1.07 mm |
| Ground pads of the jacks, bulk capacitors, OLED jumpers and U8's capacitor were 13–26 mm from the nearest tie to the ground plane | 11 ground vias added beside them |
| A1, J1 and J2 labels were printed outside the board edge | Taken off the silkscreen |
| 65 duplicate or fully overlapped track segments | Removed |
| Silkscreen outlines 0.12 mm wide and test point labels 0.9 mm high, under JLCPCB's minimums | 0.15 mm and 1.0 mm |
| Drill-map and job files in the Gerber zip | Left out |

For the case and knobs:

- Pot knobs: the switch body below each pot starts 6.95 mm from the shaft centre, so a knob wider than about 13 mm overhangs the button.
- Fader cap: with the lever at the top of its travel, a cap longer than about 16 mm overhangs the bottom button and LED of the column.
- Mounting hardware: see the third pass below; the holes now have copper keepouts.
- JP1–JP4 are under the OLED modules; set them before fitting the OLEDs.

For the next revision:

- Thermal reliefs on the ground pads of small parts (39 of the 67 two-pad passives have one pad flooded and one on a thin track).
- The 5 V feed still narrows to 0.3 mm in two places on the back.
- JLCPCB asks for slots at least twice as long as wide; the pot, encoder and fader slots are shorter and may draw a query or come out slightly off shape.
- In JLCPCB's Gerber viewer, confirm the solid plane (`In1_Cu.g1`) is taken as layer 2.

## Third pass, 2026-11-18

Changes asked for after the second review, plus what a public PCB release checklist (github.com/SPREsxm/claude-pcb-designer, read as a reference, not installed) added that had not been done.

| Change | Detail |
|---|---|
| Copper keepout round all six mounting holes | No copper within 3.5 mm of the hole centre on front and back (clear of an M3 screw head or standoff), nor within 2.6 mm (1 mm from the hole wall) on the inner layers. One AVDD track at H5 moved to make room. |
| Copper pulled back from the board edge | 0.5 mm, was 0.28 mm |
| Ground stitching | 494 vias on a 5 mm grid tie the front and back pours to the plane; 561 ground vias in all |
| Spare header J5 | Pins labelled on the silkscreen: 3V, G, 0, 1, 26, 27, 28, 29 |

Tried and not done: moving the 16 switch-and-LED groups under the pots down. Stretching the existing routing leaves 25 clearance violations at 1.0 mm (four of them touching) and 50 with 8 shorts at 1.5 mm, nearly all in copper that is already packed, worst under the mux U1. It needs the pot block re-routed. For the next revision it is one number in `tools/gen3.py` (switch centre 10 mm below the shaft) followed by a re-route.

Also from that checklist, not changed: no fiducials (JLCPCB does not need them at this size), and the RP2350-Zero's SWD pads are on its underside, so fit the module on sockets if you want a debugger; BOOT and RUN buttons are on top and need reaching through the case.

Order settings assumed throughout: 4 layers, 1.6 mm, 1 oz outer and 0.5 oz inner copper, JLCPCB's standard 4-layer stackup, Economic assembly of the top side.

## Check before ordering

Print the board 1:1 and offer the real parts up to it.

| Item | What to check |
|---|---|
| Fader (Alps RS6011SP6003) | Checked against Alps' hole drawing, Alps' 3D model and JLCPCB's own footprint: all six pins match and it is not mirrored. The drawing and JLCPCB's footprint put the lug hole 25 mm from centre; Alps' 3D model has that lug 0.7 mm further out. The slot covers both. |
| Brain (RP2350-Zero) | Pin order matches Waveshare's pinout picture: USB at the top, viewed from above, left column 5V, GND, 3V3, GP29–26, GP15, GP14; bottom row GP13–GP9; right column GP0–GP8 from the top. Positions are from Waveshare's dimension drawing, except that the drawing does not dimension how far the bottom row sits from the bottom edge; 1.38 mm is assumed, the same as the side columns. |
| LED (OPSCO SK6812MINI-RV) | Footprint matches the datasheet and JLCPCB's own footprint pad for pad, same pin numbers, so rotation 0 is right. |
| OLED header | Pin 1 is the left pin with the header at the top. JP1–JP4 swap pins 1 and 2 between GND and 3V3; default is pin 1 = GND, pin 2 = 3V3. To change one, cut its factory bridge before soldering the other side, or it shorts 3V3 to GND. Two modules need different addresses (move the address resistor on one). |
| Tact switch (K2-1102SP-C4SC-04) | On the generic 6 × 6 mm gull-wing footprint (C&K PTS645). The datasheet gives this variant a 9.0 mm lead span, so the pads (out to 9.5 mm) leave 0.25 mm beyond each lead tip. The maker's own pattern is longer (7.2 mm inside, 11 mm outside); use that next revision. Leads opposite each other across the body are joined inside, as the footprint assumes. |
| Pedal jack (PJ-320D) | Datasheet pads A, B, C, D are sleeve, ring 2, ring 1, tip, matching the footprint. The 2 mm collar overhangs the top edge by about 1.6 mm. |
| JLCPCB placement preview | Rotations and positions are now calculated by matching JLCPCB's own footprints against the KiCad ones. Pots moved 3.5 mm (JLCPCB's origin is between the pins and the lugs, not the shaft), jacks 0.83 mm, encoder 0.25 mm. Every BOM line has since been compared pad number by pad number with JLCPCB's footprint; that found the level shifter U8 a quarter turn out in the placement file (fixed) and confirmed the rest. Look at the preview once more after uploading, U8 in particular. |

## Firmware notes from the review

- **LED current:** 20 LEDs at full white draw 0.72–0.81 A, more than a USB 2.0 port gives, and the module's 5V pin is USB power with no fuse. Cap total LED current at about 350–400 mA in firmware.
- **Pot scan:** after changing the mux address wait 100–150 µs (or throw away the first conversion) before reading. Write GP6–GP9 in one go.
- **ADC:** keep the clock at or below 1 MHz at 3.3 V. Chip-select must stay low for the whole 24-clock frame, so drive GP5 as a plain GPIO.
- **End stops:** the buffer cannot reach the rails, so pots and fader read roughly 40–4055, not 0–4095. Scale in firmware.
- **Buttons and encoder switch** have no hardware debounce. The encoder is 30 detents, 15 pulses: two transitions per click.
- **Pedals:** tip = wiper, ring = 3.3 V. Nothing plugged in reads 0. Calibrate min and max per pedal.

## Parts chosen for JLCPCB Economic assembly

| Part | JLCPCB # | Note |
|---|---|---|
| Pot, Alps RK09K1130AAU | C202373 | 10k linear, flat shaft, 20 mm. The 15 mm RK09K1130A6S (C209770) fits the same footprint but is pre-order only. |
| Fader, Alps RS6011SP6003 | C370993 | 10k linear, 60 mm travel, single element |
| Encoder, Alps EC11E15244B2 | C470754 | 30 detents, push switch, 20 mm flat shaft |
| LED, OPSCO SK6812MINI-RV | C7279958 | One of very few addressable LEDs JLCPCB allows on Economic assembly |
| ADC, MCP3208T-CI/SL | C626764 | 12-bit, 8 inputs, SPI |
| Mux, CD74HC4067SM96 | C98457 | 16:1 analogue |

## Brain pins

| Pin | Net | Use |
|---|---|---|
| GP2 / GP3 / GP4 / GP5 | ADC_SCK / ADC_MOSI / ADC_MISO / ADC_CS | MCP3208 on SPI0 |
| GP6 / GP7 / GP8 / GP9 | MUX_S0–S3 | Pot multiplexer address |
| GP10 / GP11 / GP12 | SR_CLK / SR_LOAD / SR_DATA | Button shift registers (GP10 and GP12 are SPI1 SCK and RX; keep GP11 a plain GPIO) |
| GP13 | LED_DATA | LED chain, via 5 V level shifter |
| GP14 / GP15 | SDA / SCL | OLEDs on I2C1 |
| GP0, GP1, GP26–GP29 | spare | On header J5 with 3V3 and GND |

## Analogue inputs (MCP3208)

| Channel | Source |
|---|---|
| CH0 | Pot multiplexer U1 (address 0–15 = RV1–RV16), buffered |
| CH1 | Fader RV17, buffered |
| CH2 / CH3 | Pedal 1 / Pedal 2 (tip = wiper, ring = 3.3 V through 470 Ω, sleeve = GND) |
| CH4–CH7 | Tied to ground |

Pots RV1–RV16 run left to right, top to bottom.

## Shift register bits

Pulse SR_LOAD low, then clock out 24 bits. Bit order is U5 first: D7 down to D0 of each chip. Inputs are pulled up, so pressed = 0.

| Chip | D0–D7 |
|---|---|
| U5 | BTN1–BTN8 |
| U6 | BTN9–BTN16 |
| U7 | BTN17–BTN20, ENC_A, ENC_B, ENC_SW, spare (reads 1) |

BTN1–BTN16 sit under RV1–RV16. BTN17–BTN20 are the column of four under the encoder, top to bottom. Poll at 2 kHz or faster so the encoder is not missed.

## LED chain order

20 LEDs, each to the right of its button. The chain starts at the bottom right of the pot block and snakes upward, so chain position is not button number:

| Chain position | Beside button |
|---|---|
| 1–4 | BTN16, BTN15, BTN14, BTN13 |
| 5–8 | BTN9, BTN10, BTN11, BTN12 |
| 9–12 | BTN8, BTN7, BTN6, BTN5 |
| 13–16 | BTN1, BTN2, BTN3, BTN4 |
| 17–20 | BTN17, BTN18, BTN19, BTN20 |

LED centre is 7.5 mm to the right of its switch centre, at the same height, everywhere on the board.

## Test points

Each has its net name printed beside it on the front (TP numbers are only in the design files).

| Label | TP | Where |
|---|---|---|
| MUXA, ADC0, ADC1, SCK, MISO, AVDD | TP5, TP6, TP7, TP13, TP14, TP3 | Block of six between RV6 and RV7. MUXA is the mux output, ADC0/ADC1 the buffered ADC inputs, SCK/MISO the ADC's SPI lines. |
| 5V, 3V3, GND, SR_DATA | TP1, TP2, TP4, TP10 | Between RV7 and RV8 |
| SCL, SDA | TP11, TP12 | Left of the brain module |
| LED_DIN | TP8 | Below U8: data into the first LED |
| LED_END | TP9 | Right edge by the bottom button of the column: data out of the last LED |

## Not fitted by JLCPCB

Brain module (A1), OLED modules (J1, J2), spare header (J5). Solder jumpers, test points and mounting holes have no parts.

## Files

| File | What it is |
|---|---|
| `jlcpcb/gerbers_density_proto.zip` | Gerbers and drill files for the reviewed board |
| `jlcpcb/bom_jlcpcb.csv`, `jlcpcb/cpl_jlcpcb.csv` | Parts list (unchanged by the review) and placement file |
| `density_proto_assembled.step` | Board with Alps' own model of the fader and block models of the other tall parts, for case design |
| `density_proto_top.png`, `density_proto_bottom.png`, `density_proto_3d.png` | Renders |
| `density_proto_schematic.pdf` | Schematic, one A1 sheet in four wired blocks: brain/power/displays, analogue inputs, buttons/encoder, LEDs |
| `schematic_blocks/*.png` | The same four blocks as separate images, easier to read on screen or print |
| `../tools/gen_schematic.py` | Draws the schematic from the netlist; keeps every symbol's identity so the board links survive |
| `../tools/` | Scripts that generated the schematic, placement, routing clean-up and output files; `../tools/review/` holds the review checks and fixes |
| `../datasheets/` | Alps' 3D model of the fader |
