// density::Hal for the 125 x 130 mm prototype (Waveshare RP2350-Zero).
//
// Who runs where:
//   core 0, main loop : poll() (analog scan, latch inputs, MIDI in, serial console), LEDs,
//                       display drawing, files, everything the core does
//   core 0, timer IRQ : shift registers at 4 kHz (buttons debounced, encoder decoded)
//   core 0, USB IRQ   : TinyUSB task (arduino-pico Pico SDK USB stack)
//   core 1            : I2C1, pushes display framebuffers (see oled.h)
//   PIO + DMA         : SK6812 waveform (see leds.h)
#pragma once
#include <string>
#include "density/hal.h"
#include "board_table.h"
#include "inputs.h"
#include "leds.h"
#include "midi_queue.h"
#include "oled.h"

namespace density {
namespace proto {

// Analog scan settings. Each knob is read through the mux: change the address, throw the
// first conversion away (settling), then average kKnobSamples conversions.
constexpr uint8_t kKnobSamples = 1;     // 1 keeps a full scan under ~0.9 ms
constexpr uint8_t kDirectSamples = 1;   // fader and pedals (no mux); 2 costs ~80 us more per poll

// MCP3208 minimum chip-select disable time (tCSH, datasheet) between two frames.
constexpr uint32_t kAdcCsHighNs = 500;

// Outgoing MIDI messages waiting for room in the USB TX FIFO (which holds 16 packets).
constexpr uint16_t kMidiQueueLen = 128;
// midiReceive handles at most this many USB packets per call (it holds the USB mutex).
constexpr uint8_t kMidiRxPacketsPerCall = 32;

// Boot report kept for the @INFO console command (USB MIDI start-up re-enumerates USB, which
// closes a terminal that was already open, so the live boot log is often missed).
constexpr uint8_t kBootLogLines = 20;
constexpr uint8_t kBootLogLineLen = 112;

enum class FsState : uint8_t {
    NotMounted,   // mount failed and formatting failed too: no files
    Mounted,      // mounted as found
    Formatted,    // mount failed, so it was formatted: every file was lost
};

struct ProtoStats {
    uint32_t pollUs = 0;        // duration of the last poll()
    uint32_t pollUsMax = 0;     // worst poll() since the last resetStats()
    uint32_t scanUs = 0;        // analog part of the last poll()
    uint32_t polls = 0;
    uint32_t midiDropped = 0;   // messages lost: USB not mounted, or the send queue full
    uint32_t midiSuperseded = 0;   // queued CCs replaced by a newer value for the same control
    uint32_t logDropped = 0;    // log lines not sent (no terminal, or it is not reading)
};

class ProtoHal : public Hal {
public:
    ProtoHal();

    // Bring up everything: USB names, MIDI, LittleFS, ADC, inputs, LEDs, displays.
    // Logs what it found. Call once from setup().
    void begin();

    // --- density::Hal
    const HardwareInfo& info() const override { return info_; }
    uint32_t micros() override;
    uint32_t millis() override;
    void poll() override;
    uint16_t analogRaw(AnalogId a) override { return a < kNumAnalog ? raw_[a] : 0; }
    bool padPressed(PadId p) override { return p < kNumPads && ((inputs_.padsPressed >> p) & 1u); }
    int8_t encoderSteps(uint8_t enc) override;
    bool encoderPressed(uint8_t enc) override { return enc == 0 && inputs_.encPressed; }
    void setLed(PadId p, Rgb c) override { leds_.set(p, c); }
    void showLeds() override { leds_.show(); }
    Display& display(uint8_t i) override { return i < kNumDisplays ? displays_[i] : displays_[0]; }
    void flushDisplay(uint8_t i) override { if (i < kNumDisplays) displays_[i].flush(); }
    void midiSend(const MidiMsg& m) override;
    bool midiReceive(MidiMsg& out) override;
    bool readFile(const char* path, std::string& out) override;
    bool writeFile(const char* path, const std::string& data) override;
    void log(const char* msg) override;

    // --- Board extras (hardware test, diagnostics). Not part of the core contract.
    void logf(const char* fmt, ...) __attribute__((format(printf, 2, 3)));
    bool displayPresent(uint8_t i) const { return i < kNumDisplays && displays_[i].present(); }
    bool fsMounted() const { return fsMounted_; }
    FsState fsState() const { return fsState_; }
    static const char* fsStateName(FsState s);
    bool midiMounted();
    const InputSnapshot& inputs() const { return inputs_; }
    const LedChain& leds() const { return leds_; }
    const ProtoStats& stats() const { return stats_; }
    uint16_t midiQueued() const { return midiQueue_.size(); }
    void resetStats() { stats_.pollUsMax = 0; }
    int32_t encoderCount() const { return inputs_.encCount; }   // cumulative, for display

private:
    void scanAnalog();
    uint16_t adcRead(uint8_t ch);
    void drainMidi();
    void serviceConsole();
    void handleCommand(char* line);

    HardwareInfo info_;
    InputSampler sampler_;
    InputSnapshot inputs_;
    LedChain leds_;
    OledDisplay displays_[kNumDisplays] = {OledDisplay(0), OledDisplay(1)};
    uint16_t raw_[kNumAnalog] = {};
    int32_t encLatched_ = 0;    // encCount at the last encoderSteps() read
    bool fsMounted_ = false;
    FsState fsState_ = FsState::NotMounted;
    uint32_t adcCsHighCycles_ = 0;   // set in begin() from the system clock
    MidiQueue<kMidiQueueLen> midiQueue_;
    char bootLog_[kBootLogLines][kBootLogLineLen] = {};
    uint8_t bootLogNext_ = 0;        // next slot to write (ring)
    uint8_t bootLogCount_ = 0;
    bool bootLogging_ = false;       // log() also records into bootLog_ while true
    uint32_t logWaitUs_ = 100000;   // generous during begin(), 2 ms afterwards
    ProtoStats stats_;
    char line_[160] = {};
    uint8_t lineLen_ = 0;
    bool lineOverflow_ = false;   // discarding the rest of an over-long line
};

}  // namespace proto
}  // namespace density
