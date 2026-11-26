# Configuring the controller: `config.json`

The controller reads `/config.json` from its flash when it starts. The file says what each knob, fader, pedal and pad sends, how the pad lights behave, and how many banks there are. If the file is missing or has a mistake, the controller starts with a built-in setup (below) and shows the first problem on the display.

## The basics

- **Numbers are as printed on the board**: `knob1` is the first knob, `pad17` the seventeenth pad, bank `1` the first bank.
- **Controls** are knobs, faders and pedals. **Pads** are the buttons, each with its own light.
- You describe **groups**: a set of controls or pads numbered in a row, such as "knobs 1 to 16 send CC 16 upwards". The controller expands each group into one setting per control.
- **Groups are applied in order, and later groups win** for the settings they mention. Anything a group does not mention is left as it was. This is how you write exceptions: a general group first, then a smaller group limited to one bank (see the last group in the example).
- Anything you do not set keeps its built-in value, so a short file is fine.
- The file is plain JSON saved as UTF-8 (a byte order mark at the start, as some Windows editors add, is fine).
- Comments are allowed: `// to the end of the line` and `/* ... */`. Keys starting with `_`, and the key `comment`, are ignored, so you can leave notes such as `"_why": "Ableton wants ch 2"`.
- Spelling mistakes are reported, not ignored: an unknown key, mode or action stops the load with a message naming the group.

## Selectors

A selector names one control or pad, or a run of them:

| Selector | Means |
|---|---|
| `"knob3"` | knob 3 |
| `"knob1-16"` | knobs 1 to 16 |
| `"fader1"` | fader 1 |
| `"pedal1-2"` | both pedals |
| `"pad17-20"` | pads 17 to 20 |
| `["knob1-4", "knob9-12"]` | a list: knobs 1-4, then 9-12, numbered in that order |

Case and spaces do not matter (`"Knob 1 - 4"` works). Knobs, faders and pedals go under `controls`; pads go under `pads`.

## Example

This is `data/config.json`, the example for the 125 x 130 prototype (16 knobs with a pad under each, 1 fader, 2 pedals, 4 more pads):

```json
{
  "name": "Density proto",
  "midi_channel": 1,
  "led_brightness": 0.5,
  "pickup_tolerance": 0.03,

  "banks": [
    { "name": "Mix",   "hue": 120 },
    { "name": "Sends", "hue": 180 },
    { "name": "FX",    "hue": 280 },
    { "name": "Perf",  "hue": 30 }
  ],

  "filter": {
    "knob":  { "smoothing": 0.2, "deadband": 0.006, "settle_ms": 300 },
    "fader": { "smoothing": 0.2, "deadband": 0.006, "settle_ms": 300 },
    "pedal": { "smoothing": 0.1, "deadband": 0.01,  "settle_ms": 300 }
  },

  "groups": [
    {
      "name": "knobs",
      "controls": "knob1-16",
      "send": { "type": "cc", "first": 16, "bank_offset": 16 },
      "pickup": "pickup",
      "led": { "mode": "level", "hue": "bank" }
    },
    {
      "name": "knob buttons",
      "pads": "pad1-16",
      "action": "toggle",
      "send": { "type": "note", "first": 36, "bank_offset": 16 }
    },
    {
      "name": "fader",
      "controls": "fader1",
      "shared": true,
      "send": { "type": "cc", "first": 7 }
    },
    {
      "name": "pedals",
      "controls": "pedal1-2",
      "shared": true,
      "pickup": "jump",
      "send": { "type": "cc", "first": 11 }
    },
    {
      "name": "bank buttons",
      "pads": "pad17-20",
      "action": "bank_select",
      "first_bank": 1,
      "led": { "mode": "state", "hue": "bank" }
    },
    {
      "name": "perf bank: first row shows fixed magenta, slow fade",
      "banks": [4],
      "pads": "pad1-4",
      "led": { "mode": "static", "hue": 300, "sat": 1.0, "val": 0.8, "effect": "fade", "period_ms": 1500 }
    }
  ]
}
```

What it does:

- Knobs 1-16 send CC 16-31 in bank 1, CC 32-47 in bank 2, 48-63 in bank 3, 64-79 in bank 4. Each waits to be picked up after a bank change. The pad under each knob lights in the bank's colour, brighter as the knob's value rises.
- Pads 1-16 toggle notes 36-51 in bank 1, 52-67 in bank 2, and so on.
- The fader sends CC 7 and the pedals CC 11 and 12, the same in every bank, with one shared value.
- Pads 17-20 choose banks 1-4. Each shows its bank's colour, bright for the current bank and dim for the others.
- In bank 4 only, pads 1-4 glow magenta with a slow pulse instead of showing the knob level.

## Top-level settings

| Field | Default | What it does |
|---|---|---|
| `name` | `"Default"` | Shown on the display. |
| `midi_channel` | `1` | Channel (1-16) for everything that does not set its own. |
| `led_brightness` | `0.5` | Overall light level, 0 to 1. Scales every pad. |
| `pickup_tolerance` | `0.03` | How close (as a fraction of full travel) a control must come to its stored value to be picked up. 0.012 to 0.5 (smaller values are refused: a pot might never get close enough). Near the ends of travel there is extra room: a control within twice this distance of an end counts as having reached any stored value at or beyond that end, so a pot that cannot quite reach its end stop still picks up 0 or 127. |
| `banks` | one bank, "Bank 1" | List of banks, at most 8. Each `{ "name": "Mix", "hue": 120 }`. `hue` is a colour angle 0-360 (0 red, 60 yellow, 120 green, 180 cyan, 240 blue, 300 magenta); default 200. |
| `filter` | see below | Smoothing per kind of control: keys `knob`, `fader`, `pedal`. |
| `calibration` | 40 to 4055 | End stops per control: `{ "pedal1": [120, 3900] }`. The raw readings (0-4095) at the two ends of travel. A range such as `"pedal1-2"` sets both. Usually set from the calibration page rather than by hand. |
| `groups` | none | The list of groups, applied in order. |

### `filter` (per kind)

| Field | Default | What it does |
|---|---|---|
| `smoothing` | `0.2` | 0 to 1. How much of each new reading is taken while the control is still. Lower is smoother but slower. |
| `deadband` | `0.006` | Fraction of travel a control must move before it counts as touched. Stops an untouched knob chattering. 0.001 to 0.5. |
| `settle_ms` | `300` | After this long without movement, the control is treated as still again. 20 to 60000. |

## Group settings

| Field | Default | What it does |
|---|---|---|
| `name` | none | Your label; used in error messages. |
| `controls` | none | Selector(s) for knobs, faders, pedals. |
| `pads` | none | Selector(s) for pads. A group needs `controls`, `pads`, or both. |
| `banks` | all banks | List of bank numbers this group applies to, e.g. `[2, 4]`. |
| `send` | (unchanged) | What MIDI to send; see below. |
| `shared` | `false` | `true`: one value for all banks, and the same MIDI number in each (`bank_offset` is ignored). For pads, the toggle state is shared. |
| `pickup` | `"jump"` | Controls only. `"pickup"`: after a bank change (or when the computer changes the value) the control sends nothing until you move it to the stored value; its pad shows the pickup indicator meanwhile. `"jump"`: sends straight away. |
| `invert` | `false` | Controls only. Reverse the direction. For an inverted control the pickup indicator's "up" and "down" refer to the value, so the physical direction is reversed. |
| `action` | see defaults | Pads only: `none`, `momentary` (on while held), `toggle` (press on, press off), `bank_select`, `bank_next`, `bank_prev`. |
| `first_bank` | `1` | With `bank_select`: the bank the first pad selects; the next pad selects the next bank, and so on. |
| `on_value` | `127` | Pads: value (velocity for notes) sent for on. |
| `off_value` | `0` | Pads: value sent for off. |
| `led` | (unchanged) | The pad light; see below. |

### `send`

| Field | Default | What it does |
|---|---|---|
| `type` | required | `"cc"` or `"note"` (`"none"` silences the members). |
| `first` | required | MIDI number (0-127) of the first member. The others count up from it. In a group with both `controls` and `pads`, the controls are numbered first and the pads carry on after them. |
| `bank_offset` | `0` | Added once per bank: bank 1 gets `first`, bank 2 `first + bank_offset`, and so on. |
| `channel` | `midi_channel` | Channel 1-16 for this group. |

Pads with `bank_select`, `bank_next` or `bank_prev` send nothing unless their group also has a `send`. With a `send`, such a pad sends `on_value` when pressed, after changing the bank, and nothing when released, so the computer can follow bank changes. A `bank_select` pad sends on the number set for it in the bank where it was pressed (a `bank_select` to a bank that does not exist does nothing). Pads with `none` never send.

### `led`

Every field is optional; only the ones you give are changed.

On a group with `controls`, the `led` applies to the pad **under** each control, and that pad follows that control. On a group with `pads`, it applies to those pads.

| Field | Default | What it does |
|---|---|---|
| `mode` | see defaults | `off`; `static` (always lit); `level` (brightness follows a control's value in the current bank); `state` (bright when the toggle is on, when it is the current bank's button, or while held; dim otherwise); `midi` (brightness follows the last value the computer sent to this pad's note/CC). |
| `hue` | `200` | Colour angle 0-360, or `"bank"` for the current bank's colour. On a `bank_select` pad, `"bank"` means the colour of the bank that pad selects. |
| `sat` | `1` | Saturation 0-1: 0 is white, 1 full colour. |
| `val` | `1` | Maximum brightness 0-1 for this pad (before `led_brightness`). |
| `floor` | `0.04` | `level` and `state`: brightness at zero / off, so the pad stays faintly visible. |
| `effect` | `"none"` | `"blink"` (on/off) or `"fade"` (smooth pulse), on top of the mode. All pads with the same period stay in step. |
| `period_ms` | `500` | Length of one blink or pulse, 20 to 60000 ms. |
| `source` | the control above | `level`: follow this control instead, e.g. `"fader1"`. One control only. Required for `level` on a pad with no control above it. |

## What the lights mean

Each pad shows, in order of priority:

1. **Off** pads stay dark, always.
2. **Pressed**: while you hold a pad that does something, it lights at full brightness and paler (towards white).
3. **Pickup indicator**: if the pad's control is waiting to be picked up, the pad blinks quickly (about three times a second). **Red means turn the control down**, **blue means turn it up**; the brightness shows the value it is waiting for. The colours are the same in every bank, so a row of 16 tells you at a glance which way each knob needs to go. It stops as soon as the control is picked up.
4. Otherwise the pad shows its `mode` and `effect`.

## Built-in setup (no file, or a broken file)

- One bank, everything on MIDI channel 1.
- Knobs, then faders, then pedals send CC counting up from 16 (on the prototype: knobs CC 16-31, fader CC 32, pedals CC 33-34), in `jump` mode.
- Pads under knobs show the knob's level in blue (hue 200) and do nothing when pressed.
- The other pads are momentary notes counting up from 36 (prototype: pads 17-20 play notes 36-39), lit while held.

Your file is applied on top of this, so anything you leave out behaves as above. The top-level `midi_channel` also moves these built-in settings to that channel.

## Errors and warnings

The controller stops at the first mistake and reports it in one line, on the About page of the display and on the USB serial log (where it starts with `config.json: `), for example:

```
group 1 ("knobs"): selector 'knob1-17': this board has 16 knobs
group 2 ("knob buttons"): send numbers run past 127 (pad16 in bank 4 would be 147)
group 6 ("perf bank"): bank 5 does not exist (4 banks declared)
config is not valid JSON (InvalidInput): line 14: comma before ']' (remove the trailing comma)
```

For a JSON syntax mistake the controller names the line when it can find it (a trailing comma, a missing comma, unclosed quotes, brackets that do not match); otherwise it suggests checking for a trailing comma or a missing quote. The display shows messages of up to about 160 characters; if one is longer, it shows the end.

Two controls sending the same MIDI in the same bank is allowed (sometimes you want it), but the controller can list such clashes as warnings, e.g. `bank 2 (Sends): knob1 and fader1 both send CC 32 on channel 1`.
