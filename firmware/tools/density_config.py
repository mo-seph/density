#!/usr/bin/env python3
"""Load, fetch and manage files on a Density controller over its USB serial port.

The board must be running the Density firmware (it answers '@' commands on the USB serial
port; see src/board/density_proto/console.cpp). Needs pyserial, which PlatformIO already has:

    ~/.platformio/penv/bin/python tools/density_config.py put data/config.json
    ~/.platformio/penv/bin/python tools/density_config.py get            # prints /config.json
    ~/.platformio/penv/bin/python tools/density_config.py ls
    ~/.platformio/penv/bin/python tools/density_config.py info

`put` validates the JSON locally, uploads to /config.json (or --path), checks the CRC on the
board, then reboots the board so the new config is loaded (skip with --no-reboot).
Close any serial monitor first: only one program can hold the port.
"""
import argparse
import json
import sys
import time
import zlib

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is missing: run this with ~/.platformio/penv/bin/python, or pip install pyserial")

RP_VID = 0x2E8A  # Raspberry Pi; arduino-pico's default VID for this board


def find_port():
    ports = [p for p in serial.tools.list_ports.comports() if p.vid == RP_VID]
    named = [p for p in ports if p.product and "Density" in p.product]
    pick = named or ports
    if not pick:
        sys.exit("No Density board found. Plug it in, or pass --port.")
    if len(pick) > 1:
        print("Several candidate ports, using %s (pass --port to choose):" % pick[0].device, file=sys.stderr)
        for p in pick:
            print("  %s  %s" % (p.device, p.product), file=sys.stderr)
    return pick[0].device


class Board:
    def __init__(self, port, timeout=5.0):
        # Never open at 1200 baud: that is the "reboot to bootloader" signal.
        self.s = serial.Serial(port, 115200, timeout=0.2)
        self.s.dtr = True
        self.timeout = timeout
        time.sleep(0.2)
        self.s.reset_input_buffer()

    def send(self, line):
        self.s.write((line + "\n").encode())
        self.s.flush()

    def reply(self):
        """Next line that starts with '@' (log lines from the firmware are skipped)."""
        end = time.time() + self.timeout
        buf = b""
        while time.time() < end:
            c = self.s.read(1)
            if not c:
                continue
            if c == b"\n":
                line = buf.decode(errors="replace").strip()
                buf = b""
                if line.startswith("@"):
                    return line
                continue
            buf += c
        raise TimeoutError("no reply from board")

    def expect_ok(self):
        r = self.reply()
        if r.startswith("@ERR"):
            raise RuntimeError(r[5:])
        if not r.startswith("@OK"):
            raise RuntimeError("unexpected reply: " + r)
        return r

    def read_exact(self, n):
        out = b""
        end = time.time() + self.timeout
        while len(out) < n and time.time() < end:
            out += self.s.read(n - len(out))
        if len(out) != n:
            raise TimeoutError("short read: %d of %d bytes" % (len(out), n))
        return out

    def get(self, path):
        self.send("@GET " + path)
        r = self.reply()
        if r.startswith("@ERR"):
            raise RuntimeError(r[5:])
        _, n, crc = r.split()
        data = self.read_exact(int(n))
        self.expect_ok()
        if zlib.crc32(data) & 0xFFFFFFFF != int(crc, 16):
            raise RuntimeError("CRC mismatch on download")
        return data

    def put(self, path, data):
        if not data:
            raise RuntimeError("refusing to upload an empty file (the board rejects length 0)")
        self.send("@PUT %s %d %08x" % (path, len(data), zlib.crc32(data) & 0xFFFFFFFF))
        r = self.reply()
        if r != "@READY":
            raise RuntimeError(r[5:] if r.startswith("@ERR") else "unexpected reply: " + r)
        for i in range(0, len(data), 256):
            self.s.write(data[i:i + 256])
        self.s.flush()
        r = self.expect_ok()
        parts = r.split()
        if len(parts) != 2 or not parts[1].isdigit():
            raise RuntimeError("board replied %r to the upload, expected '@OK %d': the file may not "
                               "have been written correctly" % (r, len(data)))
        if int(parts[1]) != len(data):
            raise RuntimeError("length mismatch: sent %d bytes but the board says it wrote %s. "
                               "Check the file with 'get' and upload it again." % (len(data), parts[1]))
        return r

    def lines_until_ok(self, cmd):
        self.send(cmd)
        out = []
        while True:
            r = self.reply()
            if r.startswith("@OK"):
                return out
            if r.startswith("@ERR"):
                raise RuntimeError(r[5:])
            out.append(r)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: find the board by USB id)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("put", help="upload a file (default target /config.json) and reboot")
    p.add_argument("file")
    p.add_argument("--path", default="/config.json")
    p.add_argument("--no-reboot", action="store_true")
    p.add_argument("--no-check", action="store_true", help="skip the local JSON syntax check")
    g = sub.add_parser("get", help="download a file (default /config.json)")
    g.add_argument("--path", default="/config.json")
    g.add_argument("-o", "--out", help="write to this file instead of stdout")
    r = sub.add_parser("rm", help="delete a file on the board")
    r.add_argument("path")
    sub.add_parser("ls", help="list files on the board")
    sub.add_parser("info", help="show board name, build, filesystem state, MIDI drops and the boot report")
    sub.add_parser("reboot", help="restart the board")
    sub.add_parser("bootsel", help="restart the board into the USB bootloader (for flashing)")
    a = ap.parse_args()

    if a.cmd == "put":
        data = open(a.file, "rb").read()
        if a.path.endswith(".json") and not a.no_check:
            try:
                json.loads(data.decode("utf-8"))
            except ValueError as e:
                sys.exit("%s is not valid JSON: %s (use --no-check to send it anyway)" % (a.file, e))

    b = Board(a.port or find_port())
    try:
        if a.cmd == "put":
            b.put(a.path, data)
            print("Wrote %s (%d bytes)" % (a.path, len(data)))
            if not a.no_reboot:
                b.send("@REBOOT")
                print("Board rebooting to load it")
        elif a.cmd == "get":
            data = b.get(a.path)
            if a.out:
                open(a.out, "wb").write(data)
            else:
                sys.stdout.write(data.decode(errors="replace"))
        elif a.cmd == "rm":
            b.lines_until_ok("@RM " + a.path)
        elif a.cmd == "ls":
            for line in b.lines_until_ok("@LS"):
                print(line[6:] if line.startswith("@FILE ") else line)
        elif a.cmd == "info":
            for line in b.lines_until_ok("@INFO"):
                print(line[1:])
        elif a.cmd == "reboot":
            b.send("@REBOOT")
        elif a.cmd == "bootsel":
            b.send("@BOOTSEL")
    except (RuntimeError, TimeoutError) as e:
        sys.exit("Error: %s" % e)


if __name__ == "__main__":
    main()
