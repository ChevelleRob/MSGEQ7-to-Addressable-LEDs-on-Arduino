/*
 * MSGEQ7 -> Addressable LED Strip Music Visualizer
 * Board : Arduino Uno (ATmega328P)
 * Needs : FastLED library (Arduino IDE: Sketch > Include Library > Manage Libraries > "FastLED")
 *
 * The MSGEQ7 splits the audio into 7 frequency bands. Each "step" the strip
 * scrolls by one pixel and a new pixel is fed in whose color/brightness comes
 * from the music, so beats travel down the strip.
 *
 * Three pushbuttons (each wired from the pin to GND, no resistor needed):
 *   PATTERN button -> cycles the scroll pattern
 *   COLOR   button -> cycles the color scheme
 *   SPEED   button -> cycles the scroll speed
 *
 * Patterns:
 *   0  Full strip   : start -> end
 *   1  Full strip   : end -> start
 *   2  Full strip   : middle -> ends
 *   3  Full strip   : ends -> middle
 *   4  Segments     : start -> end         (every SEGMENT_SIZE LEDs does the same thing)
 *   5  Segments     : end -> start
 *   6  Segments     : middle -> ends
 *   7  Segments     : ends -> middle
 *   8  Segments     : alternating start->end / end->start (zig-zag)
 *   9  Segments     : alternating middle->ends / ends->middle
 *
 * Color schemes:
 *   0 Spectrum   - color shows the strongest frequency (bass = red ... treble = purple)
 *   1 RGB mix    - bass = red, mids = green, treble = blue
 *   2 Rainbow    - hue slowly rotates over time
 *   3 Fire       - quiet = dark red, loud = yellow/white
 *   4 Ocean      - blues / aquas
 *   5 Lava       - black / red / orange / white by loudness
 *   6 Forest     - greens
 *   7 Party      - pink / purple / orange / blue
 *   8 Custom 2   - blends CUSTOM_COLOR_1 (quiet) to CUSTOM_COLOR_2 (loud)
 *   9 Custom 1   - CUSTOM_COLOR_1 only
 */

#include <FastLED.h>
#include <EEPROM.h>

// =====================================================================
// ======================  USER SETTINGS  ==============================
// =====================================================================

// ---------- LED strip ----------
// Common types: WS2812B, WS2812, WS2811, WS2813, WS2815, SK6812, TM1803, UCS1903
// 4-wire (clock) types: APA102, SK9822, WS2801, LPD8806  -> also set LED_CLOCK_PIN below
#define LED_TYPE        WS2812B
#define COLOR_ORDER     GRB       // WS2812B is usually GRB. If red/green are swapped try RGB.
#define NUM_LEDS        150       // Total LEDs on the strip
#define LED_DATA_PIN    6         // Strip data (DI) pin
// #define LED_CLOCK_PIN 13       // Uncomment ONLY for 4-wire strips (APA102 etc). On Uno use DATA=11, CLOCK=13

#define LED_BRIGHTNESS  180       // Overall brightness limit 0-255
#define POWER_VOLTS     5         // Strip voltage (5 or 12)
#define POWER_MAX_MA    2000      // Max current your power supply can give (mA). FastLED dims to stay under it.

#define SEGMENT_SIZE    15        // LEDs per segment in the "Segments" patterns

// ---------- MSGEQ7 ----------
#define MSGEQ7_STROBE_PIN  7      // MSGEQ7 pin 4 (STROBE)
#define MSGEQ7_RESET_PIN   8      // MSGEQ7 pin 7 (RESET)
#define MSGEQ7_OUT_PIN     A0     // MSGEQ7 pin 3 (OUT)

// ---------- Pushbuttons (pin -> button -> GND, internal pull-ups are used) ----------
#define BUTTON_PATTERN_PIN 2
#define BUTTON_COLOR_PIN   3
#define BUTTON_SPEED_PIN   4
#define DEBOUNCE_MS        30

// ---------- Scroll speeds ----------
// Milliseconds per one-pixel step. The SPEED button cycles through this list.
// Smaller = faster. With 150 WS2812B LEDs the fastest real rate is ~6 ms/step.
const uint16_t SCROLL_SPEEDS_MS[] = { 6, 12, 20, 30, 45, 70, 100 };

// ---------- Starting settings (used on first power-up / if saving is off) ----------
#define DEFAULT_PATTERN    0
#define DEFAULT_COLOR      0
#define DEFAULT_SPEED      2      // index into SCROLL_SPEEDS_MS
#define SAVE_SETTINGS      true   // Remember pattern/color/speed in EEPROM across power cycles

// ---------- Custom colors (used by color schemes 8 and 9) ----------
const CRGB CUSTOM_COLOR_1 = CRGB::Red;
const CRGB CUSTOM_COLOR_2 = CRGB::Blue;

// ---------- Audio tuning ----------
#define NOISE_FLOOR        80     // MSGEQ7 readings (0-1023) at/below this count as silence
#define AUTO_GAIN          true   // Automatically adapt to the music volume
#define FIXED_FULL_SCALE   900    // If AUTO_GAIN is false: reading that equals full brightness
#define MIN_FULL_SCALE     300    // AUTO_GAIN never amplifies more than this (stops noise blowing up)
#define BEAT_THRESHOLD     40     // Levels (0-255) below this are shown as dark -> beats stand out
#define BASS_DRIVES_BEAT   true   // true: brightness follows bass (beats). false: loudest of all bands
#define SEGMENT_PER_BAND   false  // true: in Segment patterns each segment follows a different band

// ---------- Feedback ----------
#define SHOW_INDICATOR     true   // Briefly light N LEDs at the start of the strip after a button press
#define INDICATOR_MS       500
#define SERIAL_DEBUG       false  // Print band levels to Serial Monitor (115200 baud)

// =====================================================================
// ======================  END OF USER SETTINGS  =======================
// =====================================================================

#if MIN_FULL_SCALE <= NOISE_FLOOR
#error "MIN_FULL_SCALE must be larger than NOISE_FLOOR"
#endif
#if BEAT_THRESHOLD >= 255
#error "BEAT_THRESHOLD must be below 255"
#endif

enum Direction : uint8_t { START_TO_END, END_TO_START, MIDDLE_OUT, ENDS_IN };

struct PatternDef {
  bool segmented;   // whole strip or every SEGMENT_SIZE LEDs
  uint8_t dir;      // Direction
  bool alternate;   // flip direction on every other segment
};

const PatternDef PATTERNS[] = {
  { false, START_TO_END, false },
  { false, END_TO_START, false },
  { false, MIDDLE_OUT,   false },
  { false, ENDS_IN,      false },
  { true,  START_TO_END, false },
  { true,  END_TO_START, false },
  { true,  MIDDLE_OUT,   false },
  { true,  ENDS_IN,      false },
  { true,  START_TO_END, true  },
  { true,  MIDDLE_OUT,   true  },
};

const uint8_t NUM_PATTERNS      = sizeof(PATTERNS) / sizeof(PATTERNS[0]);
const uint8_t NUM_COLOR_SCHEMES = 10;
const uint8_t NUM_SPEEDS        = sizeof(SCROLL_SPEEDS_MS) / sizeof(SCROLL_SPEEDS_MS[0]);

const uint8_t EEPROM_MAGIC = 0xA7;   // marks EEPROM as holding valid settings

CRGB leds[NUM_LEDS];

uint8_t patternIndex = DEFAULT_PATTERN;
uint8_t colorIndex   = DEFAULT_COLOR;
uint8_t speedIndex   = DEFAULT_SPEED;

// Audio state
uint16_t rawBands[7];        // latest raw readings 0-1023
uint8_t  peakLevel[7];       // highest scaled level (0-255) since the last scroll step
uint16_t fullScale = MIN_FULL_SCALE;  // auto-gain ceiling
uint8_t  bassLevel, midLevel, trebleLevel;
uint8_t  rainbowHue = 0;

unsigned long lastStepMs = 0;

struct Button {
  uint8_t pin;
  bool stableState;
  bool lastReading;
  unsigned long lastChangeMs;
};

Button btnPattern = { BUTTON_PATTERN_PIN, HIGH, HIGH, 0 };
Button btnColor   = { BUTTON_COLOR_PIN,   HIGH, HIGH, 0 };
Button btnSpeed   = { BUTTON_SPEED_PIN,   HIGH, HIGH, 0 };

// ---------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------
// Returns true once each time the button is pressed (debounced).
bool buttonPressed(Button &b) {
  bool reading = digitalRead(b.pin);
  unsigned long now = millis();
  if (reading != b.lastReading) {
    b.lastReading = reading;
    b.lastChangeMs = now;
  }
  if ((now - b.lastChangeMs) >= DEBOUNCE_MS && reading != b.stableState) {
    b.stableState = reading;
    return (reading == LOW);
  }
  return false;
}

// ---------------------------------------------------------------------
// Settings storage
// ---------------------------------------------------------------------
void loadSettings() {
  if (!SAVE_SETTINGS || EEPROM.read(0) != EEPROM_MAGIC) return;
  uint8_t p = EEPROM.read(1), c = EEPROM.read(2), s = EEPROM.read(3);
  if (p < NUM_PATTERNS)      patternIndex = p;
  if (c < NUM_COLOR_SCHEMES) colorIndex   = c;
  if (s < NUM_SPEEDS)        speedIndex   = s;
}

void saveSettings() {
  if (!SAVE_SETTINGS) return;
  EEPROM.update(0, EEPROM_MAGIC);   // update() only writes if the value changed
  EEPROM.update(1, patternIndex);
  EEPROM.update(2, colorIndex);
  EEPROM.update(3, speedIndex);
}

// ---------------------------------------------------------------------
// MSGEQ7
// ---------------------------------------------------------------------
uint8_t scaleReading(uint16_t r) {
  uint16_t top = AUTO_GAIN ? fullScale : FIXED_FULL_SCALE;
  if (r <= NOISE_FLOOR) return 0;
  if (top <= NOISE_FLOOR) return 255;
  uint32_t v = (uint32_t)(r - NOISE_FLOOR) * 255UL / (top - NOISE_FLOOR);
  return v > 255 ? 255 : (uint8_t)v;
}

void readMSGEQ7() {
  // Reset the multiplexer to band 0 (63 Hz)
  digitalWrite(MSGEQ7_RESET_PIN, HIGH);
  delayMicroseconds(1);
  digitalWrite(MSGEQ7_RESET_PIN, LOW);
  delayMicroseconds(75);

  uint16_t loudest = 0;
  for (uint8_t i = 0; i < 7; i++) {
    digitalWrite(MSGEQ7_STROBE_PIN, LOW);
    delayMicroseconds(40);                      // let the output settle
    rawBands[i] = analogRead(MSGEQ7_OUT_PIN);
    digitalWrite(MSGEQ7_STROBE_PIN, HIGH);
    delayMicroseconds(40);
    if (rawBands[i] > loudest) loudest = rawBands[i];
  }

  if (AUTO_GAIN && loudest > fullScale) fullScale = loudest;

  // Keep the peak of each band until the next scroll step so short beats aren't missed
  for (uint8_t i = 0; i < 7; i++) {
    uint8_t lv = scaleReading(rawBands[i]);
    if (lv > peakLevel[i]) peakLevel[i] = lv;
  }
}

// Slowly lower the auto-gain ceiling so quieter music still fills the range
void decayGain() {
  if (!AUTO_GAIN) return;
  if (fullScale > MIN_FULL_SCALE) {
    fullScale -= (fullScale >> 7) + 1;
    if (fullScale < MIN_FULL_SCALE) fullScale = MIN_FULL_SCALE;
  }
}

// ---------------------------------------------------------------------
// Color generation
// ---------------------------------------------------------------------
// level: loudness 0-255.  hue: 0-255 position that represents the frequency band.
CRGB makeColor(uint8_t level, uint8_t hue) {
  if (level < BEAT_THRESHOLD) return CRGB::Black;
  // Re-stretch the remaining range to 0-255 brightness
  uint8_t v = (uint16_t)(level - BEAT_THRESHOLD) * 255U / (255U - BEAT_THRESHOLD);

  switch (colorIndex) {
    case 0:  return CHSV(hue, 255, v);                                             // Spectrum
    case 1: {                                                                       // RGB mix
      CRGB c(bassLevel, midLevel, trebleLevel);
      c.nscale8_video(v);
      return c;
    }
    case 2:  return CHSV(rainbowHue, 255, v);                                      // Rainbow
    case 3:  return ColorFromPalette(HeatColors_p,   scale8(level, 240), v);       // Fire
    case 4:  return ColorFromPalette(OceanColors_p,  hue + rainbowHue, v);         // Ocean
    case 5:  return ColorFromPalette(LavaColors_p,   scale8(level, 240), v);       // Lava
    case 6:  return ColorFromPalette(ForestColors_p, hue + rainbowHue, v);         // Forest
    case 7:  return ColorFromPalette(PartyColors_p,  hue + rainbowHue, v);         // Party
    case 8: {                                                                       // Custom 2-color
      CRGB c = blend(CUSTOM_COLOR_1, CUSTOM_COLOR_2, level);
      c.nscale8_video(v);
      return c;
    }
    default: {                                                                      // Custom 1-color
      CRGB c = CUSTOM_COLOR_1;
      c.nscale8_video(v);
      return c;
    }
  }
}

inline uint8_t bandHue(uint8_t band) { return band * 36; }   // 0=red ... 6=purple

// ---------------------------------------------------------------------
// Scrolling
// ---------------------------------------------------------------------
// Pixels in [a..b] move toward b; the new pixel enters at a.
void shiftUp(int a, int b, const CRGB &c) {
  for (int i = b; i > a; i--) leds[i] = leds[i - 1];
  leds[a] = c;
}

// Pixels in [a..b] move toward a; the new pixel enters at b.
void shiftDown(int a, int b, const CRGB &c) {
  for (int i = a; i < b; i++) leds[i] = leds[i + 1];
  leds[b] = c;
}

// Scroll one zone (the whole strip or one segment) by one pixel.
void scrollZone(int zs, int ze, uint8_t dir, const CRGB &c) {
  int len  = ze - zs + 1;
  int half = len / 2;
  bool odd = len & 1;

  switch (dir) {
    case START_TO_END:
      shiftUp(zs, ze, c);
      break;

    case END_TO_START:
      shiftDown(zs, ze, c);
      break;

    case MIDDLE_OUT:
      if (half > 0) {
        shiftDown(zs, zs + half - 1, c);     // left half flows toward the start
        shiftUp(ze - half + 1, ze, c);       // right half flows toward the end
      }
      if (odd) leds[zs + half] = c;          // center pixel
      break;

    case ENDS_IN:
      if (odd && half > 0) leds[zs + half] = leds[zs + half - 1];   // center gets arriving pixel
      if (half > 0) {
        shiftUp(zs, zs + half - 1, c);       // left half flows toward the middle
        shiftDown(ze - half + 1, ze, c);     // right half flows toward the middle
      } else {
        leds[zs] = c;
      }
      break;
  }
}

void scrollStep() {
  // Summarize the audio collected since the last step
  bassLevel   = max(peakLevel[0], peakLevel[1]);
  midLevel    = max(peakLevel[2], max(peakLevel[3], peakLevel[4]));
  trebleLevel = max(peakLevel[5], peakLevel[6]);

  uint8_t loudestBand = 0;
  for (uint8_t i = 1; i < 7; i++) {
    if (peakLevel[i] > peakLevel[loudestBand]) loudestBand = i;
  }

  uint8_t beatLevel = BASS_DRIVES_BEAT ? bassLevel : peakLevel[loudestBand];
  CRGB newPixel = makeColor(beatLevel, bandHue(loudestBand));

  const PatternDef &p = PATTERNS[patternIndex];

  if (!p.segmented) {
    scrollZone(0, NUM_LEDS - 1, p.dir, newPixel);
  } else {
    uint8_t seg = 0;
    for (int zs = 0; zs < NUM_LEDS; zs += SEGMENT_SIZE, seg++) {
      int ze = zs + SEGMENT_SIZE - 1;
      if (ze > NUM_LEDS - 1) ze = NUM_LEDS - 1;

      uint8_t dir = p.dir;
      if (p.alternate && (seg & 1)) dir ^= 1;   // START<->END or MIDDLE<->ENDS

      CRGB c = newPixel;
      if (SEGMENT_PER_BAND) {
        uint8_t band = seg % 7;
        c = makeColor(peakLevel[band], bandHue(band));
      }
      scrollZone(zs, ze, dir, c);
    }
  }

  FastLED.show();

  for (uint8_t i = 0; i < 7; i++) peakLevel[i] = 0;
}

// ---------------------------------------------------------------------
// Button feedback: light (index + 1) LEDs at the start of the strip
// ---------------------------------------------------------------------
void showIndicator(uint8_t index, CRGB color) {
  if (!SHOW_INDICATOR) return;
  FastLED.clear();
  for (uint8_t i = 0; i <= index && i < NUM_LEDS; i++) leds[i] = color;
  FastLED.show();
  delay(INDICATOR_MS);
  FastLED.clear();
  FastLED.show();
}

void checkButtons() {
  if (buttonPressed(btnPattern)) {
    patternIndex = (patternIndex + 1) % NUM_PATTERNS;
    showIndicator(patternIndex, CRGB::White);
    FastLED.clear();             // start the new pattern from a blank strip
    saveSettings();
  }
  if (buttonPressed(btnColor)) {
    colorIndex = (colorIndex + 1) % NUM_COLOR_SCHEMES;
    showIndicator(colorIndex, CHSV(colorIndex * 25, 255, 255));
    saveSettings();
  }
  if (buttonPressed(btnSpeed)) {
    speedIndex = (speedIndex + 1) % NUM_SPEEDS;
    showIndicator(speedIndex, CRGB::Blue);
    saveSettings();
  }
}

void printDebug() {
  Serial.print(F("P")); Serial.print(patternIndex);
  Serial.print(F(" C")); Serial.print(colorIndex);
  Serial.print(F(" S")); Serial.print(SCROLL_SPEEDS_MS[speedIndex]);
  Serial.print(F("ms  gain=")); Serial.print(fullScale);
  Serial.print(F("  bands:"));
  for (uint8_t i = 0; i < 7; i++) { Serial.print(' '); Serial.print(rawBands[i]); }
  Serial.println();
}

// ---------------------------------------------------------------------
void setup() {
  pinMode(MSGEQ7_STROBE_PIN, OUTPUT);
  pinMode(MSGEQ7_RESET_PIN,  OUTPUT);
  pinMode(MSGEQ7_OUT_PIN,    INPUT);
  digitalWrite(MSGEQ7_STROBE_PIN, HIGH);
  digitalWrite(MSGEQ7_RESET_PIN,  LOW);

  pinMode(BUTTON_PATTERN_PIN, INPUT_PULLUP);
  pinMode(BUTTON_COLOR_PIN,   INPUT_PULLUP);
  pinMode(BUTTON_SPEED_PIN,   INPUT_PULLUP);

  if (SERIAL_DEBUG) Serial.begin(115200);

  delay(300);   // let the strip's power settle

#ifdef LED_CLOCK_PIN
  FastLED.addLeds<LED_TYPE, LED_DATA_PIN, LED_CLOCK_PIN, COLOR_ORDER>(leds, NUM_LEDS);
#else
  FastLED.addLeds<LED_TYPE, LED_DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
#endif
  FastLED.setMaxPowerInVoltsAndMilliamps(POWER_VOLTS, POWER_MAX_MA);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.clear();
  FastLED.show();

  loadSettings();
}

void loop() {
  checkButtons();
  readMSGEQ7();

  EVERY_N_MILLISECONDS(40) { rainbowHue++; }
  EVERY_N_MILLISECONDS(50) { decayGain(); }
  if (SERIAL_DEBUG) { EVERY_N_MILLISECONDS(250) { printDebug(); } }

  unsigned long now = millis();
  if (now - lastStepMs >= SCROLL_SPEEDS_MS[speedIndex]) {
    lastStepMs = now;
    scrollStep();
  }
}
