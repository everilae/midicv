// Auduino, the Lo-Fi granular synthesiser
//
// by Peter Knight, Tinker.it http://tinker.it
//
// Help:      http://code.google.com/p/tinkerit/wiki/Auduino
// More help: http://groups.google.com/group/auduino
//
// Analog in 0: Grain 1 pitch
// Analog in 1: Grain 2 decay
// Analog in 2: Grain 1 decay
// Analog in 3: Grain 2 pitch
// Analog in 4: Grain repetition frequency
//
// Digital 3: Audio out (Digital 11 on ATmega8)
//
// Changelog:
// 19 Nov 2008: Added support for ATmega8 boards
// 21 Mar 2009: Added support for ATmega328 boards
// 7  Apr 2009: Fixed interrupt vector for ATmega328 boards
// 8  Apr 2009: Added support for ATmega1280 boards (Arduino Mega)
// 12 Oct 2012: Made source more C++11 friendly, added initial Midi

#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include "midi.h"
#include "debug.h"

struct Note {
  enum Gate {
    CLOSED,
    OPEN,
  } gate;

  uint8_t number;
  uint8_t velocity;
  uint8_t pwm;
};

static struct Voice {
  Note note;
} voice;

// Changing these will also requires rewriting audioOn()
//
// For modern ATmega168 and ATmega328 boards
//    Output is on pin 3
//
#define PWM_PIN       3
#define ENV_PWM_PIN   11
#define PWM_VALUE     OCR2B
#define ENV_PWM_VALUE OCR2A
#define LED_PIN       13
#define LED_PORT      PORTB
#define LED_BIT       5
#define PWM_INTERRUPT TIMER2_OVF_vect
/*
#include <math.h>

constexpr double noteToCV(double n) {
  return 0.62 * exp(0.057762 * n);
}

constexpr uint8_t cvToPWM(double v) {
  return v * 255 / 5;
}
*/
static void audioOn() {
  // Set up PWM to 31.25kHz, phase accurate
  TCCR2A = _BV(COM2A1) | _BV(COM2B1) | _BV(WGM20);
  TCCR2B = _BV(CS20);
  TIMSK2 = _BV(TOIE2);
}

// MIDI note to CV PWM table
static const uint8_t midiCVTable[128] PROGMEM = {2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 5, 5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 24, 25, 27, 28, 30, 32, 34, 35, 38, 40, 42, 45, 47, 50, 53, 56, 60, 63, 67, 71, 75, 80, 84, 89, 95, 100, 106, 113, 119, 126, 134, 142, 150, 159, 169, 179, 190, 201, 213, 225, 239, 253, 12, 28, 45, 63, 82, 102, 123, 146, 169, 195, 222, 250, 24, 56, 90, 125, 163, 203, 246, 35, 83, 133, 187, 244, 48, 112, 179, 251, 71, 151, 236, 70, 166, 11, 118, 232, 96, 223, 103, 246, 141, 46, 216};

void setup() {
  SETUP_DEBUG();
  pinMode(PWM_PIN, OUTPUT);
  pinMode(ENV_PWM_PIN, OUTPUT);
  audioOn();
  pinMode(LED_PIN, OUTPUT);
  // setup midi
  Midi.begin();
  // set handlers
  Midi.handlers.noteOn = [] (MidiMessage &message) {
    // no bounds checking, midi should not produce
    // note values higher than 127
    uint8_t number = message.data[0];
    uint8_t velocity = message.data[1];

    if (velocity) {
      voice.note.number = number;
      voice.note.pwm = pgm_read_word(&midiCVTable[number]);
      voice.note.velocity = velocity;
      voice.note.gate = Note::OPEN;
      LED_PORT ^= 1 << LED_BIT;
    } else if (voice.note.number == number) {
      voice.note.gate = Note::CLOSED;
      LED_PORT ^= 1 << LED_BIT;
    }
  };
  Midi.handlers.noteOff = [] (MidiMessage &message) {
    if (voice.note.number == message.data[0]) {
      voice.note.gate = Note::CLOSED;
      LED_PORT ^= 1 << LED_BIT;
    }
  };
  voice.note.number = 48;
  voice.note.pwm = pgm_read_word(&midiCVTable[48]);
  voice.note.gate = Note::CLOSED;
  voice.note.velocity = 0;
}

void loop() {}

ISR(PWM_INTERRUPT)
{
  // Gate
  if (voice.note.gate == Note::OPEN) {
    // Pull-down triggering
    ENV_PWM_VALUE = 0;
  } else {
    ENV_PWM_VALUE = 255;
  }
  // Always output CV
  PWM_VALUE = voice.note.pwm;
}
