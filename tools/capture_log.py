#!/usr/bin/env python3
"""
Roast Meter Log Capture Tool
Connects to device, sends LOG DUMP, saves CSV output.

Usage: python capture_log.py [port] [output.csv]
"""

import sys
import serial
import time

def capture_log(port='/dev/ttyUSB0', output='roast_log.csv', baud=115200):
    print(f"Connecting to {port}...")
    ser = serial.Serial(port, baud, timeout=1)
    time.sleep(2)  # Wait for device

    # Clear any pending data
    ser.flushInput()

    print("Sending LOG DUMP command...")
    ser.write(b'LOG DUMP\n')

    lines = []
    in_csv = False

    print("Receiving data...")
    timeout_start = time.time()
    while time.time() - timeout_start < 300:  # 5 min max
        line = ser.readline().decode('utf-8', errors='ignore').strip()

        if '--- BEGIN CSV ---' in line:
            in_csv = True
            continue
        elif '--- END CSV ---' in line:
            break
        elif in_csv and line:
            lines.append(line)
            if len(lines) % 1000 == 0:
                print(f"  {len(lines)} entries...")

    ser.close()

    if lines:
        with open(output, 'w') as f:
            f.write('\n'.join(lines) + '\n')
        print(f"Saved {len(lines)-1} entries to {output}")
    else:
        print("No data received")

if __name__ == '__main__':
    port = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyUSB0'
    output = sys.argv[2] if len(sys.argv) > 2 else 'roast_log.csv'
    capture_log(port, output)
