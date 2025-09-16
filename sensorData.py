# Collecting the Data from the Sensors to Create CSV File

#!/usr/bin/env python3
"""
capture_serial_split_dt.py
Usage: python capture_serial_split_dt.py <serial-port> <baud> <output.csv>

Listens to serial port, expects CSV lines from ESP like:
ESP_ms,Temperature_C,Pressure_hPa,AngleX,AngleY,AngleZ,AccX_g,AccY_g,AccZ_g,Altitude_m

The script looks for a header line starting with "ESP_CSV_HEADER:" to learn the ESP columns.
It prepends Date (YYYY-MM-DD) and Time (HH:MM:SS.mmm) to each incoming line and writes to output CSV.
"""

import sys
import serial
import time
import datetime
import csv
import os

if len(sys.argv) < 4:
    print("Usage: python capture_serial_split_dt.py <serial-port> <baud> <output.csv>")
    sys.exit(1)

port = sys.argv[1]
baud = int(sys.argv[2])
outfile = sys.argv[3]

SER_TIMEOUT = 1.0  # serial read timeout in seconds

def now_date_time():
    """Return tuple (date_str, time_str) where time includes milliseconds."""
    now = datetime.datetime.now()
    date_str = now.strftime("%Y-%m-%d")
    time_str = now.strftime("%H:%M:%S.%f")[:-3]  # keep milliseconds
    return date_str, time_str

def main():
    print(f"Opening serial port {port} @ {baud} ...")
    try:
        ser = serial.Serial(port, baud, timeout=SER_TIMEOUT)
    except Exception as e:
        print(f"Failed to open serial port: {e}")
        sys.exit(1)

    esp_columns = None
    header_written = False
    file_exists = os.path.isfile(outfile)

    try:
        with open(outfile, "a", newline='') as csvfile:
            writer = csv.writer(csvfile)
            if file_exists:
                print(f"Appending to existing file: {outfile}")
            else:
                print(f"Creating new file: {outfile}")

            print("Waiting for data... (press Ctrl+C to stop)")

            while True:
                try:
                    raw = ser.readline()
                except KeyboardInterrupt:
                    print("\nKeyboardInterrupt received, stopping.")
                    break

                if not raw:
                    continue
                try:
                    line = raw.decode('utf-8', errors='replace').strip()
                except Exception:
                    continue

                if len(line) == 0:
                    continue

                # Only keep header or numeric data lines; skip ESP32 boot/debug messages
                if not (line.startswith("ESP_CSV_HEADER:") or line[0].isdigit()):
                    print("[DEBUG skipped]:", line)  # still show in console
                    continue

                # Detect ESP header line
                if line.startswith("ESP_CSV_HEADER:"):
                    esp_header = line[len("ESP_CSV_HEADER:"):].strip()
                    esp_columns = [c.strip() for c in esp_header.split(',') if c.strip()]
                    final_header = ["Date", "Time"] + esp_columns
                    # If file is empty, write header
                    if csvfile.tell() == 0:
                        writer.writerow(final_header)
                        csvfile.flush()
                        header_written = True
                        print("Wrote header:", ", ".join(final_header))
                    else:
                        # File exists: assume header already present; still ensure header_written flag set
                        header_written = True
                    continue

                # If header not yet seen, assume default columns (same order as ESP sketch)
                if esp_columns is None:
                    default_cols = ["ESP_ms","Temperature_C","Pressure_hPa","AngleX","AngleY","AngleZ","AccX_g","AccY_g","AccZ_g","Altitude_m"]
                    esp_columns = default_cols
                    final_header = ["Date", "Time"] + esp_columns
                    if csvfile.tell() == 0:
                        writer.writerow(final_header)
                        csvfile.flush()
                        header_written = True
                        print("No header from ESP detected; wrote default header.")
                    else:
                        header_written = True

                # Normalize separators: accept comma-separated or tab-separated
                if '\t' in line and ',' not in line:
                    parts = [p.strip() for p in line.split('\t')]
                else:
                    parts = [p.strip() for p in line.split(',')]

                # Adjust parts length to esp_columns length
                if len(parts) < len(esp_columns):
                    parts += [''] * (len(esp_columns) - len(parts))
                elif len(parts) > len(esp_columns):
                    parts = parts[:len(esp_columns)]

                date_str, time_str = now_date_time()
                row = [date_str, time_str] + parts

                writer.writerow(row)
                csvfile.flush()

                # Print to console for live monitoring (tabular)
                print(','.join(row))

    except KeyboardInterrupt:
        print("Interrupted by user.")
    finally:
        try:
            ser.close()
        except:
            pass
        print("Serial port closed. CSV saved to:", outfile)

if __name__ == "__main__":
    main()