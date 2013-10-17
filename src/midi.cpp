// Auduino Midi, a simple client implementation
//
// by Ilja Everilä <saarni@gmail.com>
//
// ChangeLog:
// 12 Oct 2012: System Common and Real time made optional

#include <Arduino.h>
#include <avr/pgmspace.h>
#include "midi.h"
#include "debug.h"

// The Instance
_Midi Midi;

//---------------------------------------------------------//
// Choose serial here (Serial, Serial1, Serial2 or Serial3)
static auto &serial = Serial;

#if MIDI_HOOK_SERIAL_EVENT
// Choose serial event callback here
void serialEvent() {
//void serialEvent1() {
//void serialEvent2() {
//void serialEvent3() {
	int c;
	// HardwareSerial::available has a modulo in it. Can't use
	// it in a loop then...
	while ((c = serial.read()) != -1) {
		Midi.eventHandler(c);
	}
}
#endif
//---------------------------------------------------------//

inline constexpr uint8_t denseIndexFromStatus(uint8_t status) {
	return status < 0xF0
		// Channel Voice: 0x00 - 0x06
		? (status >> 4) - 0x08
		// Common, Real time: 0x07 - 0x16
		: (status & 0x0F) + 0x07;
}

inline constexpr uint8_t channelFromStatus(uint8_t status) {
	return (status & 0x0F) + 1;
}

// Work around buggy avr-libc PSTR
#ifdef DEBUG
static const char beginFmtStr[] PROGMEM = "_Midi::begin(channel = %d)\n";
#endif

void _Midi::begin(int8_t channel_) {
	DEBUG_WRITE_P(beginFmtStr, channel_);
	serial.begin(baudRate);
	channel = channel_;
	dataBufferPosition = 0;
	currentMessage = 0;
}

_Midi::Handlers::CallbackPtr _Midi::getControlChangeHandler() {
	Handlers::CallbackPtr handler = nullptr;
	// first data byte has controller number
#if MIDI_CHANNEL_MODE
	switch (dataBuffer[0]) {
		case Messages::AllSoundOff:         handler = handlers.allSoundOff;         break;
		case Messages::ResetAllControllers: handler = handlers.resetAllControllers; break;
		case Messages::LocalControl:        handler = handlers.localControl;        break;
		case Messages::AllNotesOff:         handler = handlers.allNotesOff;         break;
		case Messages::OmniModeOff:         handler = handlers.omniModeOff;         break;
		case Messages::OmniModeOn:          handler = handlers.omniModeOn;          break;
		case Messages::MonoModeOn:          handler = handlers.monoModeOn;          break;
		case Messages::PolyModeOn:          handler = handlers.polyModeOn;          break;
		default:
#else
	if (databuffer[0] < 120) {
#endif
			handler = handlers.controlChange;
	}

	return handler;
}

void _Midi::messageHandler(uint8_t status) {
	// Remember that channel is only valid for Channel Voice Messages
	uint8_t statusChannel = channelFromStatus(status);
	// ...so check if this is a Channel Voice etc message and this
	// is our channel, or return
	if (channel && status < 0xF0 && channel != statusChannel) {
		return;
	}

	Handlers::CallbackPtr handler = nullptr;

	switch (denseIndexFromStatus(status)) {
		case denseIndexFromStatus(Messages::NoteOff):               handler = handlers.noteOff;               break;
		case denseIndexFromStatus(Messages::NoteOn):                handler = handlers.noteOn;                break;
		case denseIndexFromStatus(Messages::PolyphonicKeyPressure): handler = handlers.polyphonicKeyPressure; break;
		case denseIndexFromStatus(Messages::ControlChange):         handler = getControlChangeHandler();      break;
		case denseIndexFromStatus(Messages::ProgramChange):         handler = handlers.programChange;         break;
		case denseIndexFromStatus(Messages::ChannelPressure):       handler = handlers.channelPressure;       break;
		case denseIndexFromStatus(Messages::PitchWheelChange):      handler = handlers.pitchWheelChange;      break;
#if MIDI_SYSTEM_COMMON
// NOTE: disabling System Common, but enabling SystemRealTime will
// make this switch/case sparse, could cause crappier compiler results?
		case denseIndexFromStatus(Messages::SystemExclusive):       handler = handlers.systemExclusive;       break;
		case denseIndexFromStatus(Messages::TimeCodeQuarterFrame):  handler = handlers.timeCodeQuarterFrame;  break;
		case denseIndexFromStatus(Messages::SongPositionPointer):   handler = handlers.songPositionPointer;   break;
		case denseIndexFromStatus(Messages::SongSelect):            handler = handlers.songSelect;            break;
		case denseIndexFromStatus(0xF4):                            /* Undefined */                           break;
		case denseIndexFromStatus(0xF5):                            /* Undefined */                           break;
		case denseIndexFromStatus(Messages::TuneRequest):           handler = handlers.tuneRequest;           break;
		case denseIndexFromStatus(Messages::EndOfExclusive):        handler = handlers.endOfExclusive;        break;
#endif
#if MIDI_SYSTEM_REAL_TIME
		case denseIndexFromStatus(Messages::TimingClock):           handler = handlers.timingClock;           break;
		case denseIndexFromStatus(0xF9):                            /* Undefined */                           break;
		case denseIndexFromStatus(Messages::Start):                 handler = handlers.start;                 break;
		case denseIndexFromStatus(Messages::Continue):              handler = handlers.continue_;             break;
		case denseIndexFromStatus(Messages::Stop):                  handler = handlers.stop;                  break;
		case denseIndexFromStatus(0xFD):                            /* Undefined */                           break;
		case denseIndexFromStatus(Messages::ActiveSensing):         handler = handlers.activeSensing;         break;
		case denseIndexFromStatus(Messages::Reset):                 handler = handlers.reset;                 break;
#endif
	}

	if (handler) {
		Message message = {
			status,
			statusChannel,
			{ dataBuffer[0], dataBuffer[1], dataBuffer[2] },
		};

		handler(message);
	}
}

static const uint8_t bytes_to_read_lookup[23] PROGMEM = {
	// Channel Voice Messages
	2, 2, 2, 2, 1, 1, 2,
	// System Common Messages
	1, 1, 2, 1, 0, 0, 0, 0,
	// System Real-Time Messages
	// (Not really needed, but for completeness)
	0, 0, 0, 0, 0, 0, 0, 0,
};

// Can not condition system real time handling here, since
// handling running status requires at least filtering them
inline constexpr bool isSystemRealTime(uint8_t status) {
	// System real time = 0xF8 - 0xFF
	return status >= 0xF8;
}

void _Midi::eventHandler(uint8_t data) {
	if (data & 0x80) {
		if (isSystemRealTime(data)) {
			// System real time is always without data and
			// must be handled right away even between data
			// streams
			messageHandler(data);
			// No data, ever, and must not disrupt other messages
			return;
		}
		// Begin new channel voice or system common message
		currentMessage = data;
		bytesLeft = bytesToRead = pgm_read_byte(&bytes_to_read_lookup[denseIndexFromStatus(data)]);
	} else if (dataBufferPosition < dataBufferSize) {
		// store data
		dataBuffer[dataBufferPosition++] = data;
		bytesLeft--;
	}

	// ... until
	if (bytesLeft == 0) {
		// ... message buffer full, handle message
		messageHandler(currentMessage);
		// Reset data buffer
		dataBufferPosition = 0;
		// Reset bytes to read (handle running status).
		// If next byte is a status byte, this is overwritten anyway.
		bytesLeft = bytesToRead;
	}
}
