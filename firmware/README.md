# Density firmware

Firmware for the Density USB-MIDI controllers. `DESIGN.md` explains the architecture; this
file says how to build, flash and use it. Board facts (pins, chain order, bit order) are in
`../proto-125x130/NOTES.md`.

Status: the board code builds, but **none of it has run on hardware yet**. The boards were
ordered and have not arrived. Flash the hardware test first (below) and check each item.

## What you need

- PlatformIO Core. It is at `~/.platformio/penv/bin/pio` here (not on PATH). Below, `pio` means
  that path.
- For `tools/density_config.py`: Python with pyserial. PlatformIO's own Python has it:
  `~/.platformio/penv/bin/python`.

The first board build downloads the toolchain and the arduino-pico core (a few minutes).

## Environments

| Env | What |
|---|---|
| `native`, `native_engine`, `native_config`, `native_ui` | Core library unit tests on the Mac |
| `density_proto` | The 125 x 130 prototype: Waveshare RP2350-Zero |
| `density_proto_hwtest` | Same board, always in hardware test mode (`-DDENSITY_HW_TEST`) |

`default_envs` is still `native`, so a bare `pio test` keeps running the unit tests. Always
name the board env: `pio run -e density_proto`.

Board platform: the maxgerhardt fork of `platform-raspberrypi`
(the official PlatformIO platform has no RP2350 support), pinned to a commit, which brings
arduino-pico 6.1.0. Board id `waveshare_rp2350_zero`. 4 MB flash: 3 MB program, 1 MB LittleFS.
USB uses the core's default Pico SDK stack: one composite device with a USB serial port and a
class-compliant MIDI port, both named "Density".

## Build

```sh
pio run -e density_proto            # firmware: .pio/build/density_proto/firmware.uf2
pio test -e native                  # core unit tests (on the Mac)
```

`density_proto` is the full firmware: it builds the portable application in `lib/core` and
`src/app.cpp` forwards to it. `density_proto_hwtest` leaves the core out (`lib_ignore = density-core`)
and runs only the hardware test.

## Flash

**First time (or if the board does not respond): BOOTSEL.**

1. Unplug the board. Hold the **BOOT** button on the RP2350-Zero module and plug in USB. Let go.
2. A drive called `RP2350` appears.
3. Either copy `.pio/build/density_proto/firmware.uf2` onto that drive, or run
   `pio run -e density_proto -t upload`. The board restarts by itself.

**After that:** `pio run -e density_proto -t upload` works without the button. PlatformIO
resets the board through the USB serial port (the 1200 baud trick). If that fails, use BOOTSEL,
or `~/.platformio/penv/bin/python tools/density_config.py bootsel`.

## Watch the log

```sh
pio device monitor -e density_proto
```

The board waits up to 1.5 s at power-up for a terminal so the start-up report is seen. Start-up
report: LittleFS state and whether `/config.json` exists, the ADC clock and chip-select high
time, the raw shift-register word with the chain check (spare bit 1, 25th bit 0), LED driver
state, and which displays answered.

Starting USB MIDI re-enumerates the USB device, which closes a terminal that was already open,
so the live boot report is easy to miss. The board keeps it: `density_config.py info` (or
typing `@INFO` in a terminal) prints it again as `BOOT ...` lines, together with the filesystem
state and the MIDI drop counters.

**Filesystem.** The firmware never formats silently. If LittleFS will not mount, the log says
so, the board formats it once and logs `LittleFS FORMATTED ... ALL FILES WERE LOST`; `info`
then shows `FS formatted at boot (...)` until the next restart. The app just finds no
`/config.json` or calibration (reading them fails).

**Saving pauses inputs and MIDI.** arduino-pico's LittleFS turns interrupts off and stops
core 1 for every flash erase and program. So while the firmware saves a file (pedal
calibration, or a config uploaded with `density_config.py put`) the buttons, encoder, USB MIDI
and serial, and the displays all pause, for some tens of milliseconds per file. A press or an
encoder click made entirely inside that pause is missed; MIDI messages are held, not lost.
Afterwards the input sampler ignores the timer's burst of catch-up ticks and starts debouncing
and decoding afresh (counted as `input resyncs` in the hardware test's status line).

## Load a configuration

The configuration is `/config.json` in the board's flash (LittleFS). The firmware reads it at
start-up. Two ways to put it there:

**1. Over USB serial (normal way; keeps calibration and other files):**

```sh
~/.platformio/penv/bin/python tools/density_config.py put data/config.json
```

This checks that the JSON parses, uploads it, checks the CRC on the board, writes it safely
(temp file then rename), checks that the length the board reports writing matches what was
sent, and restarts the board so it loads the new file. Empty files are refused (by the tool
and by the board). Close any serial
monitor first: only one program can hold the port. Other commands:

```sh
... density_config.py get                 # print the config on the board
... density_config.py get -o backup.json
... density_config.py ls                  # list files
... density_config.py rm /some/file
... density_config.py info                # board, build, filesystem state, MIDI drops, boot report
... density_config.py reboot | bootsel
```

**2. Whole filesystem image from `data/`:**

```sh
pio run -e density_proto -t uploadfs
```

This replaces the **entire** filesystem with the contents of `data/`, so pedal calibration or
anything else the firmware saved is lost. Good for a fresh board.

Why not a USB drive? arduino-pico has two drive features. `SingleFileDrive` shows one LittleFS
file to the computer, but the computer can only read or delete it, not write it. `FatFSUSB`
shows a writeable drive, but only for a FAT filesystem. While the computer has a FAT drive
mounted, the firmware must not touch it, and a desktop keeps the drive mounted the whole time
the board is plugged in. So the firmware could not save calibration while connected. Both
features work alongside MIDI on the Pico SDK USB stack. Neither lets the user write the file
while the firmware keeps a filesystem it can write. Sources:
https://arduino-pico.readthedocs.io/en/latest/singlefile.html and
https://arduino-pico.readthedocs.io/en/latest/fatfsusb.html.

The serial protocol is documented at the top of `src/board/density_proto/console.cpp`. Every
command and reply line starts with `@`, so log lines can be mixed in.

## Hardware test mode

This is what runs until the core is integrated (the weak stubs in `src/app_stub.cpp`). Build
`density_proto_hwtest` to get it later. Flash it first on a new board.

**LEDs**
- Knob *i* sets the brightness of pad *i*'s LED. Each pad has its own hue around the colour
  wheel. Pad 1 (BTN1) is red, then the colour changes a bit per pad. If the colours are in the
  wrong places, the LED chain map is wrong.
- Pads 17, 18 and 19 (BTN17-19) follow the fader, pedal 1 and pedal 2. Pad 20 stays dim.
- A pressed pad lights solid white.
- The encoder moves a blinking white highlight across the pads, in pad order.
- The encoder **switch** cycles the LED mode:
  - **0, normal**: as above.
  - **1, pad walk**: one pad lit at a time, in pad order BTN1, BTN2, ..., every 0.4 s. Display 0
    says in large text which pad should be lit (`BTN 7`) and its chain position. This is the
    clearest test of the chain map.
  - **2, colour order**: every LED pure red, then pure green, then pure blue, a second each,
    repeating. Display 0 says in large text which colour you should see (`RED`, `GREEN`,
    `BLUE`) and the log says it too. If what you see differs, the LEDs are not GRB (see below).
    The level is low enough that the current cap never scales it.
  - **3, current-cap test**: all 20 at full white. The driver must scale this down to the cap
    (350 mA estimated). Display 0 shows the estimate before and after the cap, for example
    `LED 740>348mA`.

**Display 0 (address 0x3C)** (modes 0 and 3; modes 1 and 2 show the large text above)
- Top bar: encoder position, the direction of the last step (`last+` or `last-`), the
  reminder `CW=+` (clockwise should count up), and `S` while the switch is held.
- Four rows of four: raw values (0-4095) of knobs RV1-RV16, laid out like the knobs.
  Expect about 40-4055 end to end (the buffer cannot reach the rails).
- `F`, `P1`, `P2`: fader and pedals. An empty pedal socket reads about 0.
- Main loop rate in Hz, duration of the last `poll()` in µs, and the LED mode.
- LED current estimate before and after the cap, and the measured shift-register sample rate
  (`SR`, expect 4000).

**Display 1 (address 0x3D)**
- Top bar: `chain OK` or `chain FAULT`, and `D0ok` when display 0 was found. The chain check
  clocks one bit more than the 24 data bits: the last shift register's serial input is tied
  to GND, so that 25th bit must read 0, and the unused input U7 D7 must read 1. Both must hold
  for `chain OK`.
- Pad grid in board layout: a 4 x 4 block (BTN1-16, left to right, top to bottom) and the
  column of four (BTN17-20). A filled box means pressed.
- Raw shift-register word in hex. Nothing pressed should read `FFFFFF`, except the encoder
  A/B bits.
- `sp1 x0`: the spare bit and the 25th bit (right is 1 and 0; a `!` follows if not).
- Live encoder A, B and switch levels.
- `MIDI ok` when the computer has opened the MIDI device.
- `drop N`: MIDI messages lost so far (see "MIDI out" below).
- Bottom line: the last MIDI message received.

Either display can be missing. The other still works, and the log says which was found.

**MIDI out, channel 1**
- Pad press/release: note 36 + pad, velocity 127. Release sends note off.
- Knob, fader or pedal moving more than 48 raw counts: CC 20 + control. Knobs are CC 20-35,
  the fader CC 36, the pedals CC 37-38. The value is raw / 32.
- Encoder detent: CC 50, value 65 clockwise and 63 anticlockwise (relative).

**Sending.** Messages go straight to the USB TX FIFO (16 packets) when nothing is waiting,
otherwise into a 128-message queue that every `poll()` drains. If that queue fills, a new CC
replaces the oldest queued CC for the same channel and number (it is out of date anyway;
counted as superseded, not dropped). A note off never loses out to a CC: if needed the oldest
CC is dropped to make room. Otherwise the new message is dropped and counted. Messages sent
while no computer has the MIDI device open are dropped and counted too. The count is `drop N`
on display 1, in the status line, and in `density_config.py info`.

**MIDI in**: every note or CC is logged and shown on display 1. Other messages (clock, sysex,
...) are skipped; each `midiReceive()` call looks at no more than 32 USB packets.

**Serial log**: one line per pad press/release, analog change, encoder step, switch and MIDI
message in. Every 5 s it logs a status line with the loop rate, `poll()` time (last and
worst), scan time, shift-register rate, LED current, MIDI state, dropped and superseded
messages, display frames pushed and input resyncs.

### What to check first, and what might need changing

- **Shift-register chain.** Display 1 must say `chain OK`.
- **Encoder direction.** If turning clockwise shows a negative count in the hardware test,
  set `kEncoderReverse = true` (in `src/board/density_proto/board_table.h`). Display 0's top
  bar shows `last-` and the log says `counts down` when that happens; `CW = +` is the
  expectation.
- **Encoder steps per click.** The decoder expects detents at A/B = 00 and 11 (two edges per
  click). If one click gives 0 or 2 steps, look at the live A/B levels on display 1.
- **LED colours.** Use LED mode 2. If the colours do not match the words on display 0 (for
  example red and green swapped), the LEDs are not GRB. The byte order is set in
  `LedChain::startFrame()`.
- **LED timing.** 0 bit high 375 ns, 1 bit high 625 ns, period 1.25 µs
  (`src/board/density_proto/sk6812.pio`).
- **Display orientation.** If the picture is upside down, set a rotation for the display.
- The shift-register CLK INH pin is assumed to be tied low.

## Timing (estimated, not measured)

| Work | Where | Cost |
|---|---|---|
| Analog scan: 16 knobs (address change, one thrown-away conversion, one kept), fader, 2 pedals | `poll()`, core 0 | 34 MCP3208 frames at 1 MHz: 24 µs of clocks, then CS held high at least 500 ns (datasheet tCSH), plus call overhead, about 25-26 µs each: **about 0.86-0.9 ms** |
| Shift registers (24 bits bit-banged), debounce, quadrature | timer IRQ, core 0, 4 kHz | about 6-8 µs per sample, about 3 % of core 0. Stops while a file is written (see "Saving pauses inputs and MIDI"). |
| MIDI out queue | `poll()`, core 0 | a few µs when messages are waiting |
| LEDs | PIO + DMA | a few µs of CPU per `showLeds()`. A new frame starts at most every 700 µs. A newer frame waiting in between is sent next. |
| Display push | core 1 | `flushDisplay()` copies 1 KB, about 1 µs. Core 1 pushes it over I2C in about 25 ms. |
| USB | USB IRQ every 1 ms | small |

So `poll()` costs about 0.9 ms and the loop runs at roughly 1 kHz. The engine and LED work
come on top. The hardware test shows the real figures. The knob scan is the limit: the
MCP3208 clock may not go above 1 MHz at 3.3 V, chip select must stay high at least 500 ns
between frames (the wait is computed from the system clock at start-up and logged), and each
knob needs a settling conversion after its mux address changes. The constants `kKnobSamples` and `kDirectSamples` in `proto_hal.h`
set the averaging. Each extra knob sample adds about 0.4 ms. Each extra direct sample adds
about 80 µs.

At 400 kHz the I2C bus moves about 40 full frames per second in total. Two displays at
20 Hz each fill it. The newest frame always wins: if a display is flushed again before core 1
has sent the last frame, the older one is dropped.

## Source layout (board side)

```
src/main.cpp                      Hal instance, setup/loop, core 1 entry points
src/app.h, src/app_stub.cpp       integration hook (weak stubs run the hardware test)
src/hw_test/                      hardware test mode
src/board/board.h                 board selection (BoardHal = ProtoHal)
src/board/density_proto/
  board_table.h                   counts, pins, bit map, LED chain map, padUnder
  proto_hal.*                     density::Hal: analog scan, MIDI, files, log
  inputs.*, input_logic.h         shift-register sampler, debounce, quadrature decoder,
                                  catch-up rule after a pause (SampleGate)
  midi_queue.h                    outgoing MIDI queue and its drop policy
  leds.*, sk6812.pio(.h)          SK6812 PIO + DMA driver, gamma, current cap
  oled.*                          density::Display over GFXcanvas1 + Adafruit_SSD1306 on core 1
  console.cpp                     serial file-transfer commands
tools/density_config.py           host side of the serial file transfer
```
