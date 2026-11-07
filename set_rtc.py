#!/usr/bin/env python3
"""
Sets the DS3231 to UTC over serial, and reports the offset.

Usage:
    python3 set_rtc.py          set the clock
    python3 set_rtc.py --check  read the offset without setting

Upload ds3231_settime_auto.cpp first, and close the PlatformIO
serial monitor — it holds the port.
"""
import sys, time, datetime, serial

PORT = "/dev/cu.SLAB_USBtoUART"
BAUD = 115200

def open_port():
    try:
        return serial.Serial(PORT, BAUD, timeout=2)
    except Exception as e:
        sys.exit(f"Could not open {PORT}: {e}\nIs the serial monitor still open?")

def read_until(ser, marker, timeout=10):
    lines, t0 = [], time.time()
    while time.time() - t0 < timeout:
        raw = ser.readline().decode(errors="replace").strip()
        if raw:
            lines.append(raw)
            print(f"  < {raw}")
            if marker in raw:
                return lines
    return lines

def parse_rtc(line):
    for tag in ("BEFORE", "TIME_SET", "NOW", "TICK"):
        if line.startswith(tag):
            iso = line.split()[1].rstrip("Z")
            return datetime.datetime.strptime(iso, "%Y-%m-%dT%H:%M:%S")
    return None

def show_offset(ser):
    ser.reset_input_buffer()
    ser.write(b"NOW\n")
    host = datetime.datetime.utcnow()
    for line in read_until(ser, "NOW", 5):
        rtc = parse_rtc(line)
        if rtc:
            delta = (rtc - host).total_seconds()
            print(f"\n  Host (UTC): {host.strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"  RTC  (UTC): {rtc.strftime('%Y-%m-%d %H:%M:%S')}")
            print(f"  Offset:     {delta:+.1f} s  "
                  f"({'RTC ahead' if delta > 0 else 'RTC behind'})")
            return delta
    print("  No reading returned.")
    return None

def main():
    check_only = "--check" in sys.argv
    ser = open_port()
    print(f"Opened {PORT}\n")

    if check_only:
        time.sleep(0.3)
        show_offset(ser)
        ser.close()
        return

    print("Resetting board and waiting for TIME_REQUEST...")
    ser.setDTR(False); ser.setRTS(True)
    time.sleep(0.1)
    ser.setRTS(False)
    time.sleep(0.1)
    ser.reset_input_buffer()

    if not any("TIME_REQUEST" in l for l in read_until(ser, "TIME_REQUEST", 15)):
        print("\nNo TIME_REQUEST seen. Is ds3231_settime_auto.cpp uploaded?")
        ser.close()
        return

    # Wait for the next whole second, then send it immediately.
    now = datetime.datetime.utcnow()
    target = (now + datetime.timedelta(seconds=1)).replace(microsecond=0)
    time.sleep((target - datetime.datetime.utcnow()).total_seconds())

    msg = f"TIME:{target.strftime('%Y-%m-%dT%H:%M:%S')}\n"
    ser.write(msg.encode())
    print(f"\n  > {msg.strip()}")

    read_until(ser, "TIME_SET", 5)
    print("\nVerifying...")
    time.sleep(2)
    show_offset(ser)
    ser.close()

if __name__ == "__main__":
    main()
