// Serial console on the USB CDC port: file transfer for /config.json (and any other small
// file) plus a few maintenance commands. Used by tools/density_config.py.
//
// Every command line starts with '@' and ends with '\n'. Every reply line starts with '@',
// so a host tool can skip interleaved log lines. Commands:
//   @INFO                         -> @INFO <board> <build date> fs=<0|1>
//                                    @FS <state>    mounted / formatted at boot (files lost) / NOT mounted
//                                    @MIDI dropped=<n> superseded=<n> queued=<n>
//                                    @BOOT <line>   the boot report, one line each
//                                    then @OK
//   @LS                           -> @FILE <path> <size> ...                then @OK
//   @GET <path>                   -> @DATA <len> <crc32 hex>, <len> raw bytes, "\n", @OK
//   @PUT <path> <len> <crc32 hex> -> @READY, host sends <len> raw bytes,    then @OK <len>
//                                    (len must be 1..65536; the host checks the echoed len)
//   @RM <path>                    -> @OK
//   @REBOOT                       -> @OK, then the board restarts (config is read at boot)
//   @BOOTSEL                      -> @OK, then the board restarts into the USB bootloader
// Errors reply "@ERR <reason>". A line longer than the 159-character buffer is discarded
// up to its newline (never run as a command).
#include "proto_hal.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace density {
namespace proto {

namespace {
constexpr size_t kMaxFileBytes = 64 * 1024;
constexpr uint32_t kByteTimeoutMs = 2000;

uint32_t crc32(const uint8_t* d, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; ++i) {
        c ^= d[i];
        for (int k = 0; k < 8; ++k) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void reply(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void reply(const char* fmt, ...) {
    char buf[200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > int(sizeof buf) - 1) n = sizeof buf - 1;
    Serial.write(buf, n);   // replies may block briefly: the host asked for them
    Serial.write("\r\n", 2);
    Serial.flush();
}

bool validPath(const char* p) {
    return p && p[0] == '/' && strlen(p) < 64 && !strstr(p, "..");
}

void listDir(const char* dir) {
    Dir d = LittleFS.openDir(dir);
    while (d.next()) {
        std::string path = std::string(dir) + (dir[strlen(dir) - 1] == '/' ? "" : "/") + d.fileName().c_str();
        if (d.isDirectory()) listDir(path.c_str());
        else reply("@FILE %s %u", path.c_str(), unsigned(d.fileSize()));
    }
}
}  // namespace

void ProtoHal::serviceConsole() {
    // Read whatever has arrived, without waiting. A line longer than the buffer is dropped
    // whole: everything up to the next newline is discarded, not parsed as a new command.
    int budget = 256;
    while (budget-- > 0 && Serial.available() > 0) {
        const int c = Serial.read();
        if (c < 0) break;
        if (c == '\r') continue;
        if (c == '\n') {
            line_[lineLen_] = 0;
            const bool run = !lineOverflow_ && lineLen_ > 0 && line_[0] == '@';
            lineLen_ = 0;
            if (lineOverflow_) {
                lineOverflow_ = false;
                continue;   // end of the discarded line; nothing to run
            }
            if (run) handleCommand(line_);
            // A PUT consumes raw bytes after its line: stop here so they are not taken as text.
            return;
        }
        if (lineOverflow_) continue;
        if (lineLen_ < sizeof(line_) - 1) {
            line_[lineLen_++] = char(c);
        } else {
            lineLen_ = 0;
            lineOverflow_ = true;
        }
    }
}

void ProtoHal::handleCommand(char* line) {
    char* save = nullptr;
    const char* cmd = strtok_r(line, " ", &save);
    const char* a1 = strtok_r(nullptr, " ", &save);
    const char* a2 = strtok_r(nullptr, " ", &save);
    const char* a3 = strtok_r(nullptr, " ", &save);
    if (!cmd) return;

    if (!strcmp(cmd, "@INFO")) {
        reply("@INFO %s %s fs=%d", info_.name, __DATE__, fsMounted_ ? 1 : 0);
        reply("@FS %s", fsStateName(fsState_));
        reply("@MIDI dropped=%lu superseded=%lu queued=%u", (unsigned long)stats_.midiDropped,
              (unsigned long)stats_.midiSuperseded, unsigned(midiQueue_.size()));
        const uint8_t first = uint8_t((bootLogNext_ + kBootLogLines - bootLogCount_) % kBootLogLines);
        for (uint8_t i = 0; i < bootLogCount_; ++i) reply("@BOOT %s", bootLog_[(first + i) % kBootLogLines]);
        reply("@OK");
    } else if (!strcmp(cmd, "@LS")) {
        if (!fsMounted_) return reply("@ERR filesystem not mounted");
        listDir("/");
        reply("@OK");
    } else if (!strcmp(cmd, "@GET")) {
        if (!validPath(a1)) return reply("@ERR bad path");
        std::string data;
        if (!readFile(a1, data)) return reply("@ERR cannot read %s", a1);
        reply("@DATA %u %08lx", unsigned(data.size()),
              (unsigned long)crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size()));
        if (!data.empty()) Serial.write(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        Serial.write("\r\n", 2);
        reply("@OK");
    } else if (!strcmp(cmd, "@PUT")) {
        if (!validPath(a1) || !a2 || !a3) return reply("@ERR usage: @PUT <path> <len> <crc32>");
        const unsigned long len = strtoul(a2, nullptr, 10);
        const unsigned long want = strtoul(a3, nullptr, 16);
        if (len == 0) return reply("@ERR length 0 (or not a number): refusing to write an empty file");
        if (len > kMaxFileBytes) return reply("@ERR too big (max %u)", unsigned(kMaxFileBytes));
        std::string data;
        data.resize(len);
        reply("@READY");
        size_t got = 0;
        uint32_t last = ::millis();
        while (got < len) {
            const int avail = Serial.available();
            if (avail > 0) {
                got += Serial.readBytes(&data[got], std::min<size_t>(size_t(avail), len - got));
                last = ::millis();
            } else if (::millis() - last > kByteTimeoutMs) {
                return reply("@ERR timeout after %u of %lu bytes", unsigned(got), len);
            }
        }
        const uint32_t crc = crc32(reinterpret_cast<const uint8_t*>(data.data()), data.size());
        if (crc != want) return reply("@ERR crc mismatch (got %08lx)", (unsigned long)crc);
        if (!writeFile(a1, data)) return reply("@ERR write failed");
        logf("Wrote %s, %lu bytes", a1, len);
        reply("@OK %lu", len);
    } else if (!strcmp(cmd, "@RM")) {
        if (!validPath(a1) || !fsMounted_) return reply("@ERR bad path");
        if (!LittleFS.remove(a1)) return reply("@ERR cannot remove %s", a1);
        reply("@OK");
    } else if (!strcmp(cmd, "@REBOOT")) {
        reply("@OK");
        delay(50);
        rp2040.reboot();
    } else if (!strcmp(cmd, "@BOOTSEL")) {
        reply("@OK");
        delay(50);
        rp2040.rebootToBootloader();
    } else {
        reply("@ERR unknown command %s", cmd);
    }
}

}  // namespace proto
}  // namespace density
