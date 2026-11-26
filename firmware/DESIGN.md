# Density firmware: design

One firmware for a family of USB-MIDI controllers. What a board *has* is fixed at compile time; what the controls *do* comes from a JSON file read at boot.

## Layers

```
src/main.cpp                glue: build Hal, load config, run loop
src/board/                  one folder per board: pin table + drivers implementing Hal   (Arduino code lives only here and in main)
lib/core/                   portable C++17, no Arduino headers. Builds and unit-tests on the Mac (`pio test -e native`)
  include/density/types.h     small shared types (Hsv, Rgb, ids, MidiMsg)
  include/density/hal.h       the hardware interface core code talks to
  include/density/model.h     the in-memory configuration (what the JSON becomes)
  include/density/engine_view.h  what LEDs and UI may ask of, and tell, the mapping engine
  src/...                     filter, engine, led engine, config loader, ui
test/                       native tests, with test/support/fake_hal.h
data/config.json            example configuration for the 125 x 130 prototype
```

Rule: nothing in `lib/core` includes `<Arduino.h>` or touches a pin. It sees hardware only through `density::Hal`.

## Hardware as the core sees it

- **Analog controls**: knobs, faders, pedals. One flat list, `AnalogId` 0..N-1, ordered knobs, then faders, then pedals. Hal delivers raw 12-bit readings; the core does scaling, smoothing and change detection.
- **Pads**: a button with an RGB LED beside it is one thing, a *pad*. `PadId` i is both button i and LED i. The driver hides the LED chain order.
- **Encoders**: detent steps and a push switch.
- **Displays**: small monochrome framebuffers.
- **Links**: the board table says which pad sits under which analog control (prototype: knob i has pad i, for i = 0..15).

Prototype (`density_proto`): 16 knobs, 1 fader, 2 pedals (AnalogId 0-15, 16, 17-18), 20 pads, 1 encoder, 2 displays (128 x 64). Pin facts are in `../proto-125x130/NOTES.md`.

## Behaviour

- **Banks.** N banks, each with a name and a hue. A binding is either *banked* (separate stored value and MIDI address per bank) or *global*.
- **Stored values.** Every (binding, bank) has a stored 0..1 value. It changes when the control sends, or when matching MIDI arrives from the host (so software changes are known).
- **Pickup.** After a bank change, or after the host changes a stored value, a control whose physical position differs from the stored value is *not picked up*: it sends nothing until it reaches or crosses the stored value (within a tolerance). Mode per binding: `pickup` or `jump`.
- **Smoothing.** Per analog control: scale raw to 0..1 using calibrated end stops, low-pass, then a hysteresis dead band so an untouched control sends nothing. The filter opens quickly on real movement (adaptive: more smoothing when still, less when moving). Output resolution is 7-bit by default; no message is sent unless the 7-bit value changes.
- **LEDs.** Colours are HSV everywhere in config and core. Each pad has an LED spec: `off`, `static`, `level` (brightness follows the linked control's stored value in the current bank; hue from the spec or the bank), `state` (toggle/bank-selected on or off), `midi` (set by incoming note/CC). Effects `blink` and `fade` (pulse) run locally with no MIDI. A not-picked-up control overrides its pad's LED with the pickup indicator: brightness shows the stored value, blinking, with the blink colour shifted to tell "turn up" from "turn down". Total LED current is capped in the driver.
- **Pads as buttons.** Actions: `bank_select n`, `bank_next/prev`, `note`, `cc` (momentary or toggle), `none`.
- **Groups.** JSON describes groups of controls with sequential numbering; the loader expands them to one binding per control.
- **UI.** The encoder and displays run a small page system: turn = next/previous page or adjust, press = enter/confirm. Pages for now: bank names, knob levels, pedal calibration. Display 0 shows the page, display 1 is free for a second view.
- **MIDI in.** CC/note matching a binding's address updates the stored value / pad state.

## Config delivery

`/config.json` on the board's flash filesystem. If it is missing or invalid the firmware uses a built-in default and shows the error on the display. How the file gets there (USB drive, SysEx, serial) is decided by the platform agent after reading the core's documentation; the core only needs `Hal::readFile`.

## Timing

Main loop target 1 kHz or better for analog scan and LED/engine update; buttons and the encoder are sampled at 2 kHz or faster (driver's job, e.g. timer or second core). Display refresh is slow (I2C) and must not stall the scan: draw at most one display per loop pass, at about 20-30 Hz.

## Ownership (who edits what)

| Area | Files |
|---|---|
| Platform | `platformio.ini`, `src/**`, `README.md` |
| Engine | `lib/core/src/filter.*`, `engine.*`, `test/test_engine/**`, `test/support/**` |
| Config + LEDs | `lib/core/src/config_loader.*`, `led_engine.*`, `color.*`, `test/test_config/**`, `test/test_leds/**` |
| UI | `lib/core/src/ui.*`, `test/test_ui/**` |
| Contract (ask before changing) | `lib/core/include/density/{types,hal,model,engine_view}.h`, `data/config.json` (as the JSON format example), this file |

If the contract is wrong or missing something, do not silently change it: add what you need in your own header, and say so in your report with the exact change you want.
