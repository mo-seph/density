#include "proto_hal.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <MIDIUSB.h>
#include <USB.h>
#include <CoreMutex.h>
#include <tusb.h>
#include <hardware/clocks.h>
#include <hardware/gpio.h>
#include <hardware/spi.h>
#include <hardware/timer.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace density {
namespace proto {

namespace {
const char* const kUsbName = "Density";
}

ProtoHal::ProtoHal() : info_(makeHardwareInfo()) {}

const char* ProtoHal::fsStateName(FsState s) {
    switch (s) {
        case FsState::Mounted: return "mounted";
        case FsState::Formatted: return "formatted at boot (mount failed; all files were lost)";
        default: return "NOT mounted (mount and format both failed)";
    }
}

uint32_t ProtoHal::micros() { return time_us_32(); }
uint32_t ProtoHal::millis() { return ::millis(); }

// ------------------------------------------------------------------------------ begin
void ProtoHal::begin() {
    bootLogging_ = true;   // keep the boot report for @INFO
    // USB: the core has already started CDC (Serial). Name the device and add MIDI;
    // MidiUSB.begin() re-enumerates, so the new product string is picked up.
    USB.setProduct(kUsbName);
    MidiUSB.setName(kUsbName);
    MidiUSB.begin();

    // Give a terminal a moment to attach so the boot report is seen (does not block if absent).
    const uint32_t t0 = ::millis();
    while (!Serial && ::millis() - t0 < 1500) delay(10);
    logf("Density firmware, board %s, built %s %s", info_.name, __DATE__, __TIME__);

    // Files. Never format silently: auto-format off, and if the mount fails, say so, format
    // once and say that the files are gone. The app sees that as readFile() failing.
    LittleFSConfig fsCfg;
    fsCfg.setAutoFormat(false);
    LittleFS.setConfig(fsCfg);
    fsMounted_ = LittleFS.begin();
    if (fsMounted_) {
        fsState_ = FsState::Mounted;
    } else {
        log("LittleFS mount FAILED (is board_build.filesystem_size set?). Formatting it now...");
        if (LittleFS.format() && LittleFS.begin()) {
            fsMounted_ = true;
            fsState_ = FsState::Formatted;
            log("LittleFS FORMATTED: the filesystem was unreadable, ALL FILES WERE LOST "
                "(config, calibration). Load a config again.");
        } else {
            fsState_ = FsState::NotMounted;
            log("LittleFS format FAILED too: no files this session");
        }
    }
    if (fsMounted_) {
        FSInfo fi;
        LittleFS.info(fi);
        logf("LittleFS mounted: %u of %u bytes used", unsigned(fi.usedBytes), unsigned(fi.totalBytes));
        if (LittleFS.exists("/config.json")) {
            File f = LittleFS.open("/config.json", "r");
            logf("/config.json present, %u bytes", unsigned(f.size()));
            f.close();
        } else {
            log("/config.json missing (load one with tools/density_config.py put, or pio run -t uploadfs)");
        }
    }

    // ADC: SPI0 at <= 1 MHz, mode 0, CS as a plain GPIO.
    spi_init(spi0, kAdcSpiHz);
    spi_set_format(spi0, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(kPinAdcSck, GPIO_FUNC_SPI);
    gpio_set_function(kPinAdcMosi, GPIO_FUNC_SPI);
    gpio_set_function(kPinAdcMiso, GPIO_FUNC_SPI);
    gpio_init(kPinAdcCs);
    gpio_set_dir(kPinAdcCs, GPIO_OUT);
    gpio_put(kPinAdcCs, 1);
    gpio_init_mask(kMuxMask);
    gpio_set_dir_out_masked(kMuxMask);
    gpio_put_masked(kMuxMask, 0);
    // CS high time in CPU cycles, rounded up, plus one for the loop around the wait.
    adcCsHighCycles_ = uint32_t((uint64_t(clock_get_hz(clk_sys)) * kAdcCsHighNs + 999999999u) / 1000000000u) + 1;
    logf("ADC SPI0 at %u Hz, CS high >= %u ns (%lu cycles)", unsigned(spi_get_baudrate(spi0)),
         unsigned(kAdcCsHighNs), (unsigned long)adcCsHighCycles_);

    // Buttons and encoder.
    if (sampler_.begin()) logf("Shift registers sampled at %u Hz", unsigned(kInputSampleHz));
    else log("Shift register timer FAILED to start");
    inputs_ = sampler_.snapshot();
    encLatched_ = inputs_.encCount;
    {
        const ChainCheck cc = InputSampler::chainCheck();
        logf("Shift register raw 0x%06lX, spare bit %d (want 1), 25th bit %d (want 0): chain %s",
             (unsigned long)cc.raw, cc.spareBit ? 1 : 0, cc.extraBit ? 1 : 0, cc.ok() ? "OK" : "FAULT");
    }
    if (inputs_.padsPressed)
        logf("Note: pads held at boot: 0x%05lX", (unsigned long)inputs_.padsPressed);

    // LEDs.
    if (leds_.begin()) logf("LEDs: PIO + DMA on GP%u, cap %u mA", kPinLedData, unsigned(kLedCurrentCapMa));
    else log("LED driver FAILED (no free PIO state machine or DMA channel)");

    // Displays: core 1 owns I2C1, probes and initialises them.
    for (auto& d : displays_) d.begin();
    oled_core1::start();
    if (!oled_core1::waitReady(1000)) log("Display core did not report back within 1 s");
    for (uint8_t i = 0; i < kNumDisplays; ++i) {
        displays_[i].setPresent(oled_core1::present(i));
        logf("Display %u at 0x%02X: %s", i, kDisplayAddr[i], displays_[i].present() ? "found" : "absent");
    }
    log("Serial commands: @INFO @LS @GET <path> @PUT <path> <len> <crc32> @RM <path> @REBOOT @BOOTSEL");
    log("@INFO repeats this boot report");
    bootLogging_ = false;
    logWaitUs_ = 2000;
}

// ------------------------------------------------------------------------------ poll
void ProtoHal::poll() {
    const uint32_t t0 = time_us_32();
    inputs_ = sampler_.snapshot();
    scanAnalog();
    const uint32_t t1 = time_us_32();
    leds_.service();
    drainMidi();
    serviceConsole();
    const uint32_t t2 = time_us_32();
    stats_.scanUs = t1 - t0;
    stats_.pollUs = t2 - t0;
    if (stats_.pollUs > stats_.pollUsMax) stats_.pollUsMax = stats_.pollUs;
    stats_.polls++;
}

int8_t ProtoHal::encoderSteps(uint8_t enc) {
    if (enc != 0) return 0;
    int32_t d = inputs_.encCount - encLatched_;
    if (d > 127) d = 127;
    if (d < -127) d = -127;
    encLatched_ += d;
    return int8_t(d);
}

// ------------------------------------------------------------------------------ analog
// One MCP3208 single-ended conversion: one 24-clock frame with CS held low throughout.
// Byte 0: 0000 0 1 SGL D2 -> start bit, single-ended, channel bit 2
// Byte 1: D1 D0 xxxxxx   -> result arrives as 0000 B11..B8 in rx[1], B7..B0 in rx[2]
uint16_t ProtoHal::adcRead(uint8_t ch) {
    const uint8_t tx[3] = {uint8_t(0x06 | ((ch >> 2) & 1)), uint8_t((ch & 3) << 6), 0};
    uint8_t rx[3];
    gpio_put(kPinAdcCs, 0);
    spi_write_read_blocking(spi0, tx, rx, 3);
    gpio_put(kPinAdcCs, 1);
    // MCP3208 tCSH: CS must stay high >= 500 ns before the next frame can lower it. Without
    // this wait the next call could lower it again within ~100-270 ns.
    busy_wait_at_least_cycles(adcCsHighCycles_);
    return uint16_t(((rx[1] & 0x0F) << 8) | rx[2]);
}

void ProtoHal::scanAnalog() {
    // Knobs through the mux. The address was left at 0 by the previous scan, so knob 0 has
    // had a whole loop pass to settle; every other knob gets a thrown-away conversion
    // after its address changes (NOTES.md: wait 100-150 us or discard one). One conversion
    // = 24 SPI clocks at 1 MHz + >= 500 ns CS high + call overhead, about 25-26 us, so the
    // 34 conversions of a scan take about 0.86-0.9 ms.
    for (uint8_t k = 0; k < kNumKnobs; ++k) {
        if (k != 0) {
            gpio_put_masked(kMuxMask, uint32_t(k) << kPinMuxS0);   // all four address lines at once
            (void)adcRead(kAdcChMux);
        }
        uint32_t sum = 0;
        for (uint8_t s = 0; s < kKnobSamples; ++s) sum += adcRead(kAdcChMux);
        raw_[k] = uint16_t((sum + kKnobSamples / 2) / kKnobSamples);
    }
    gpio_put_masked(kMuxMask, 0);   // start knob 0 settling for the next pass

    // Fader and pedals: direct channels, no settling needed.
    for (uint8_t i = 0; i < kNumFaders + kNumPedals; ++i) {
        uint32_t sum = 0;
        for (uint8_t s = 0; s < kDirectSamples; ++s) sum += adcRead(kAdcChDirect[i]);
        raw_[kNumKnobs + i] = uint16_t((sum + kDirectSamples / 2) / kDirectSamples);
    }
}

// ------------------------------------------------------------------------------ MIDI
bool ProtoHal::midiMounted() {
    CoreMutex m(&USB.mutex, false);
    return m && tud_midi_mounted();
}

namespace {
// MidiMsg -> USB-MIDI event packet, cable 0.
void toPacket(const MidiMsg& msg, uint8_t p[4]) {
    const uint8_t ch = uint8_t((msg.channel ? msg.channel - 1 : 0) & 0x0F);
    if (msg.type == MidiType::CC) {
        p[0] = 0x0B;   // CIN = control change
        p[1] = uint8_t(0xB0 | ch);
    } else if (msg.value == 0) {
        p[0] = 0x08;   // note off
        p[1] = uint8_t(0x80 | ch);
    } else {
        p[0] = 0x09;   // note on
        p[1] = uint8_t(0x90 | ch);
    }
    p[2] = msg.number & 0x7F;
    p[3] = msg.value & 0x7F;
}
}  // namespace

// Messages go to the USB TX FIFO (16 packets) directly when nothing is queued ahead of them,
// otherwise into midiQueue_, which poll() drains. Order is kept either way.
void ProtoHal::midiSend(const MidiMsg& msg) {
    if (msg.type == MidiType::None) return;
    {
        // Same locking as the core's SerialUSB: holding USB.mutex keeps the USB IRQ's tud_task out.
        CoreMutex m(&USB.mutex, false);
        if (m) {
            if (!tud_midi_mounted()) {
                stats_.midiDropped++;   // nobody listening: do not queue stale messages
                return;
            }
            if (midiQueue_.empty()) {
                uint8_t p[4];
                toPacket(msg, p);
                if (tud_midi_packet_write(p)) return;
            }
        }
    }
    const uint32_t before = midiQueue_.dropped();
    const uint32_t supBefore = midiQueue_.superseded();
    midiQueue_.push(msg);
    stats_.midiDropped += midiQueue_.dropped() - before;
    stats_.midiSuperseded += midiQueue_.superseded() - supBefore;
}

void ProtoHal::drainMidi() {
    if (midiQueue_.empty()) return;
    CoreMutex m(&USB.mutex, false);
    if (!m) return;   // USB busy: try next pass
    if (!tud_midi_mounted()) {
        stats_.midiDropped += midiQueue_.size();
        midiQueue_.clear();
        return;
    }
    uint8_t p[4];
    while (!midiQueue_.empty()) {
        toPacket(midiQueue_.front(), p);
        if (!tud_midi_packet_write(p)) break;   // FIFO full: rest next pass
        midiQueue_.pop();
    }
}

bool ProtoHal::midiReceive(MidiMsg& out) {
    CoreMutex m(&USB.mutex, false);
    if (!m || !tud_midi_mounted()) return false;
    uint8_t p[4];
    // Bounded: a flood of packets we skip (clock, sysex, ...) must not keep the USB mutex.
    for (uint8_t n = 0; n < kMidiRxPacketsPerCall && tud_midi_available() && tud_midi_packet_read(p); ++n) {
        const uint8_t cin = p[0] & 0x0F;
        const uint8_t status = p[1] & 0xF0;
        out.channel = uint8_t((p[1] & 0x0F) + 1);
        out.number = p[2] & 0x7F;
        if (cin == 0x0B && status == 0xB0) {
            out.type = MidiType::CC;
            out.value = p[3] & 0x7F;
            return true;
        }
        if (cin == 0x09 && status == 0x90) {   // note on, velocity 0 = note off
            out.type = MidiType::Note;
            out.value = p[3] & 0x7F;
            return true;
        }
        if (cin == 0x08 && status == 0x80) {
            out.type = MidiType::Note;
            out.value = 0;
            return true;
        }
        // Anything else (clock, sysex, pitch bend, ...) is skipped.
    }
    return false;
}

// ------------------------------------------------------------------------------ files
bool ProtoHal::readFile(const char* path, std::string& out) {
    out.clear();
    if (!fsMounted_ || !path) return false;
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    const size_t n = f.size();
    out.resize(n);
    const size_t got = n ? f.read(reinterpret_cast<uint8_t*>(&out[0]), n) : 0;
    f.close();
    if (got != n) {
        out.clear();
        return false;
    }
    return true;
}

// Note: arduino-pico's LittleFS turns interrupts off and idles core 1 for every flash erase
// and program, so while this runs the input timer, USB (MIDI, serial) and the display core
// all pause, typically for some tens of milliseconds per file.
bool ProtoHal::writeFile(const char* path, const std::string& data) {
    if (!fsMounted_ || !path || path[0] != '/') return false;
    // Write a temporary file, then rename over the target (lfs_rename replaces it), so a
    // power cut mid-write leaves the old file intact.
    std::string tmp = std::string(path) + ".tmp";
    File f = LittleFS.open(tmp.c_str(), "w");
    if (!f) return false;
    const size_t put = data.empty() ? 0 : f.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    f.close();
    if (put != data.size()) {
        LittleFS.remove(tmp.c_str());
        return false;
    }
    return LittleFS.rename(tmp.c_str(), path);
}

// ------------------------------------------------------------------------------ log
void ProtoHal::log(const char* msg) {
    if (!msg) return;
    char head[16];
    const int hn = snprintf(head, sizeof head, "[%7lu] ", (unsigned long)::millis());
    const size_t n = strlen(msg);
    if (bootLogging_) {
        char* slot = bootLog_[bootLogNext_];
        snprintf(slot, kBootLogLineLen, "%s%s", head, msg);
        bootLogNext_ = uint8_t((bootLogNext_ + 1) % kBootLogLines);
        if (bootLogCount_ < kBootLogLines) bootLogCount_++;
    }
    // Never block the loop for long: if no terminal is open, or it is not draining the
    // 256-byte CDC buffer within logWaitUs_, drop the line.
    if (!Serial) {
        stats_.logDropped++;
        return;
    }
    const int need = int(hn + n + 2);
    const uint32_t t0 = time_us_32();
    while (Serial.availableForWrite() < need) {
        if (need > 256 || time_us_32() - t0 > logWaitUs_) {
            stats_.logDropped++;
            return;
        }
        delayMicroseconds(50);
    }
    Serial.write(head, hn);
    Serial.write(msg, n);
    Serial.write("\r\n", 2);
}

void ProtoHal::logf(const char* fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    log(buf);
}

}  // namespace proto
}  // namespace density
