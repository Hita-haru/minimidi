#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <RotaryEncoder.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>

// --- Pin Definitions ---
const byte ROW_PINS[] = {0, 1, 2, 3, 4, 5};
const byte COL_PINS[] = {6, 7, 8, 9, 10, 11, 12};
const byte NUM_ROWS = sizeof(ROW_PINS);
const byte NUM_COLS = sizeof(COL_PINS);

#define ROTARY_ENCODER_1A_PIN 21
#define ROTARY_ENCODER_1B_PIN 20
#define ROTARY_ENCODER_2A_PIN 16
#define ROTARY_ENCODER_2B_PIN 17

#define TFT_CS   15
#define TFT_DC   13
#define TFT_RST  14

// --- MIDI & Keypad Configuration ---
#define MIDI_CHANNEL 1
#define ENCODER_1_SWITCH -1
#define ENCODER_2_SWITCH -2
#define OCTAVE_UP_KEY    -3
#define OCTAVE_DOWN_KEY  -4
#define DAW_CC_BASE_VAL 30

int midiNotes[NUM_ROWS][NUM_COLS] = {
  // COL#      1,      2,      3,      4,      5,      6,    7
  /*ROW 0*/{   49,     51,    -1,     54,     56,     58,   -1},
  /*ROW 1*/{   48,     50,    52,     53,     55,     57,   59},
  /*ROW 2*/{   61,     63,    -1,     66,     68,     70,   -1},
  /*ROW 3*/{   60,     62,    64,     65,     67,     69,   71},
  /*ROW 4*/{  -30,    -31,   -32,    -33, OCTAVE_UP_KEY,    -1,  ENCODER_1_SWITCH},
  /*ROW 5*/{  -35,    -36,   -37,    -38, OCTAVE_DOWN_KEY,  -1,  ENCODER_2_SWITCH}
};

// --- Global State Variables ---
bool currentKeyStates[NUM_ROWS][NUM_COLS];
bool lastKeyStates[NUM_ROWS][NUM_COLS];
int octaveShift = 0;
int activeNoteCount = 0;
String lastNoteString = "---";
String lastCCString = "---";

// --- Object Instances ---
Adafruit_USBD_MIDI usb_midi;
RotaryEncoder encoder1(ROTARY_ENCODER_1A_PIN, ROTARY_ENCODER_1B_PIN, RotaryEncoder::LatchMode::FOUR3);
RotaryEncoder encoder2(ROTARY_ENCODER_2A_PIN, ROTARY_ENCODER_2B_PIN, RotaryEncoder::LatchMode::FOUR3);
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// --- Function Prototypes ---
void updateNoteArea();
void updateCCArea();
void updateStatusArea();
String midiNoteToString(int note);
void scanMatrix();
void checkEncoders();
void processKeyPress(byte row, byte col);
void processKeyRelease(byte row, byte col);


// --- Display Functions ---
void updateNoteArea() {
  tft.fillRect(0, 25, tft.width(), 12, ST7735_BLACK);
  tft.setCursor(5, 25);
  tft.setTextColor(ST7735_CYAN);
  tft.print("Note: " + lastNoteString);
  tft.setCursor(110, 25);
  tft.print("(" + String(activeNoteCount) + ")");
}

void updateCCArea() {
  tft.fillRect(0, 55, tft.width(), 12, ST7735_BLACK);
  tft.setCursor(5, 55);
  tft.setTextColor(ST7735_MAGENTA);
  tft.print("CC: " + lastCCString);
}

void updateStatusArea() {
  tft.fillRect(0, 85, tft.width(), 12, ST7735_BLACK);
  tft.setCursor(5, 85);
  tft.setTextColor(ST7735_YELLOW);
  tft.print("Ch:" + String(MIDI_CHANNEL));
  tft.setCursor(80, 85);
  tft.print("Oct:" + String(octaveShift));
}

String midiNoteToString(int note) {
  static const String noteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  int octave = (note / 12) - 1;
  return noteNames[note % 12] + String(octave);
}

// --- Main Setup and Loop ---
void setup() {
  // Init states
  for (byte r = 0; r < NUM_ROWS; r++) for (byte c = 0; c < NUM_COLS; c++) {
    currentKeyStates[r][c] = false; lastKeyStates[r][c] = false;
  }
  
  // Init Pins
  for (byte i = 0; i < NUM_ROWS; i++) {
    pinMode(ROW_PINS[i], OUTPUT); digitalWrite(ROW_PINS[i], HIGH);
  }
  for (byte i = 0; i < NUM_COLS; i++) pinMode(COL_PINS[i], INPUT_PULLUP);

  usb_midi.begin();
  Serial.begin(115200);

  // Init LCD
  tft.initR(INITR_BLACKTAB); // Initialize a ST7735S chip, black tab
  tft.setRotation(1); // Adjust rotation as needed
  tft.fillScreen(ST7735_BLACK);

  // Draw initial layout
  tft.setTextWrap(false);
  tft.setTextColor(ST7735_WHITE);
  tft.setTextSize(2);
  tft.setCursor(5, 5);
  tft.print("MiniMIDI");
  tft.drawFastHLine(0, 22, tft.width(), ST7735_DARKGREY);
  tft.drawFastHLine(0, 52, tft.width(), ST7735_DARKGREY);
  tft.drawFastHLine(0, 82, tft.width(), ST7735_DARKGREY);
  tft.setTextSize(1);
  updateNoteArea();
  updateCCArea();
  updateStatusArea();
  
  Serial.println("MiniMIDI Firmware Initialized");
}

void loop() {
  scanMatrix();
  checkEncoders();
  delay(1); 
}

// --- Input Processing Functions ---
void scanMatrix() {
  for (byte r = 0; r < NUM_ROWS; r++) {
    digitalWrite(ROW_PINS[r], LOW);
    for (byte c = 0; c < NUM_COLS; c++) {
      currentKeyStates[r][c] = (digitalRead(COL_PINS[c]) == LOW);
      if (currentKeyStates[r][c] != lastKeyStates[r][c]) {
        if (currentKeyStates[r][c]) processKeyPress(r, c);
        else processKeyRelease(r, c);
      }
      lastKeyStates[r][c] = currentKeyStates[r][c];
    }
    digitalWrite(ROW_PINS[r], HIGH);
  }
}

void processKeyPress(byte row, byte col) {
  int note = midiNotes[row][col];
  int shiftedNote = note + (octaveShift * 12);

  if (note >= 0) {
    activeNoteCount++;
    lastNoteString = midiNoteToString(shiftedNote);
    updateNoteArea();
    usb_midi.sendNoteOn(shiftedNote, 127, MIDI_CHANNEL);
  } else if (note <= -DAW_CC_BASE_VAL) {
    byte cc_num = -note;
    lastCCString = String(cc_num) + " | Val: 127";
    updateCCArea();
    usb_midi.sendControlChange(cc_num, 127, MIDI_CHANNEL);
  } else if (note == ENCODER_1_SWITCH) {
    lastCCString = "20 | Val: 127";
    updateCCArea();
    usb_midi.sendControlChange(20, 127, MIDI_CHANNEL);
  } else if (note == ENCODER_2_SWITCH) {
    lastCCString = "21 | Val: 127";
    updateCCArea();
    usb_midi.sendControlChange(21, 127, MIDI_CHANNEL);
  } else if (note == OCTAVE_UP_KEY) {
    if (octaveShift < 2) octaveShift++;
    updateStatusArea();
  } else if (note == OCTAVE_DOWN_KEY) {
    if (octaveShift > -2) octaveShift--;
    updateStatusArea();
  }
}

void processKeyRelease(byte row, byte col) {
  int note = midiNotes[row][col];
  int shiftedNote = note + (octaveShift * 12);

  if (note >= 0) {
    if(activeNoteCount > 0) activeNoteCount--;
    updateNoteArea(); // Update count
    usb_midi.sendNoteOff(shiftedNote, 0, MIDI_CHANNEL);
  } else if (note <= -DAW_CC_BASE_VAL) {
    byte cc_num = -note;
    lastCCString = String(cc_num) + " | Val: 0";
    updateCCArea();
    usb_midi.sendControlChange(cc_num, 0, MIDI_CHANNEL);
  } else if (note == ENCODER_1_SWITCH) {
    lastCCString = "20 | Val: 0";
    updateCCArea();
    usb_midi.sendControlChange(20, 0, MIDI_CHANNEL);
  } else if (note == ENCODER_2_SWITCH) {
    lastCCString = "21 | Val: 0";
    updateCCArea();
    usb_midi.sendControlChange(21, 0, MIDI_CHANNEL);
  }
}

void checkEncoders() {
  static int lastPos1 = 0;
  static int lastPos2 = 0;

  encoder1.tick();
  int newPos1 = encoder1.getPosition();
  if (newPos1 != lastPos1) {
    byte cc_val = (newPos1 > lastPos1) ? 1 : 127;
    lastCCString = "22 | Val: " + String(cc_val);
    updateCCArea();
    usb_midi.sendControlChange(22, cc_val, MIDI_CHANNEL);
    lastPos1 = newPos1;
  }

  encoder2.tick();
  int newPos2 = encoder2.getPosition();
  if (newPos2 != lastPos2) {
    byte cc_val = (newPos2 > lastPos2) ? 1 : 127;
    lastCCString = "23 | Val: " + String(cc_val);
    updateCCArea();
    usb_midi.sendControlChange(23, cc_val, MIDI_CHANNEL);
    lastPos2 = newPos2;
  }
}
