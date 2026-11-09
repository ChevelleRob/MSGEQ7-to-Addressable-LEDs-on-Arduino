# MSGEQ7 → Addressable LEDs on Arduino Uno

A music visualizer for an Arduino Uno. An MSGEQ7 splits audio into 7 frequency bands. The beats then scroll along a 150-LED addressable strip. Three pushbuttons step through the patterns, color schemes and scroll speeds.

Sketch: [`MSGEQ7_LED_Visualizer/MSGEQ7_LED_Visualizer.ino`](MSGEQ7_LED_Visualizer/MSGEQ7_LED_Visualizer.ino)

## Requirements
- Arduino IDE (or arduino-cli) with the Uno board selected
- **FastLED** library (Library Manager → search "FastLED")

## Settings you'll most likely change
All of these are in the **USER SETTINGS** block at the top of the sketch:

| Setting | Default | Meaning |
|---|---|---|
| `LED_TYPE` / `COLOR_ORDER` | `WS2812B` / `GRB` | Strip chipset and color order |
| `NUM_LEDS` | 150 | Number of LEDs |
| `LED_DATA_PIN` | 6 | Strip data pin (`LED_CLOCK_PIN` for 4-wire strips like APA102) |
| `LED_BRIGHTNESS` | 180 | Global brightness 0–255 |
| `POWER_VOLTS`, `POWER_MAX_MA` | 5 V, 2000 mA | FastLED dims the strip to stay within your supply |
| `SEGMENT_SIZE` | 15 | LEDs per segment for segment patterns |
| `MSGEQ7_STROBE_PIN` / `RESET` / `OUT` | 7 / 8 / A0 | MSGEQ7 connections |
| `USE_BUTTONS` | true | `false` = no buttons; the `DEFAULT_PATTERN` / `DEFAULT_COLOR` / `DEFAULT_SPEED` values set the display |
| `BUTTON_PATTERN_PIN` / `COLOR` / `SPEED` | 2 / 3 / 4 | Pushbuttons |
| `SCROLL_SPEEDS_MS[]` | 6…100 | ms per one-pixel step; the speed button cycles this list |
| `NOISE_FLOOR`, `BEAT_THRESHOLD`, `AUTO_GAIN` | | Audio sensitivity tuning |
| `CUSTOM_COLOR_1/2` | Red / Blue | Colors for the custom color schemes |

## Controls
If you don't want buttons, set `USE_BUTTONS false`. Then pick the pattern, color scheme and speed with `DEFAULT_PATTERN`, `DEFAULT_COLOR` and `DEFAULT_SPEED` (the numbers are listed at the top of the sketch) and re-upload.

- **Pattern button**: full strip start→end, end→start, middle→ends, ends→middle. Then the same four on every 15-LED segment, then two alternating (zig-zag) segment patterns.
- **Color button**: Spectrum, RGB mix (bass/mid/treble), Rainbow, Fire, Ocean, Lava, Forest, Party, Custom 2-color, Custom 1-color.
- **Speed button**: cycles through `SCROLL_SPEEDS_MS`.

After each press, the first N LEDs light briefly to show the new setting number (white = pattern, colored = color scheme, blue = speed). Your settings are saved in EEPROM and restored at power-up.

## Wiring

```
Arduino Uno                MSGEQ7 (DIP-8)
-----------                --------------
5V  ---------------------- pin 1 VDD    (+0.1µF to GND)
GND ---------------------- pin 2 VSS
A0  ---------------------- pin 3 OUT
D7  ---------------------- pin 4 STROBE
                           pin 5 IN     <- 0.01µF <- 22kΩ <- audio
                           pin 6 CG     -> 0.1µF to GND
D8  ---------------------- pin 7 RESET
                           pin 8 CKIN   -> 200kΩ to 5V and 33pF to GND
```

For stereo audio, connect the left and right channels to pin 5 through separate 22 kΩ + 0.01 µF paths so they sum together. Also connect the audio source ground to Arduino GND.

Pushbuttons: one leg to D2/D3/D4, the other to GND. No resistors are needed because internal pull-ups are used.

LED strip:
- Connect strip DIN to D6 through a **~330–470 Ω resistor**.
- Put a **1000 µF capacitor** across the strip's 5V/GND.
- Power the strip from a **separate 5 V supply** sized for the strip. 150 LEDs can draw up to ~9 A at full white. Set `POWER_MAX_MA` to match your supply.
- **Connect the Arduino GND to the strip/supply GND.**
- For 150 LEDs, inject power at both ends of the strip.

## Tuning tips
- Set `SERIAL_DEBUG true` and open the Serial Monitor at 115200 baud to see raw band values.
- If LEDs flicker with no music playing, raise `NOISE_FLOOR`.
- If only big hits light up, lower `BEAT_THRESHOLD`. Raise it for more separation between beats.
- Set `BASS_DRIVES_BEAT false` to react to all frequencies instead of mainly the kick and bass.
- Set `SEGMENT_PER_BAND true` to make each 15-LED segment follow a different frequency band.
