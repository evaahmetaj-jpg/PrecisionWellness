"""
sleep_logger.py
───────────────
Reads the MR60BHA2 CSV stream from the XIAO ESP32C6 over USB serial.
Handles the tracking lifecycle (TRACKING_STARTED / TRACKING_STOPPED),
ignores all '#' prefixed debug lines, flags spike windows, and on exit
prints a session summary aligned to Whoop export fields.

Spike handling
──────────────
Windows where breath_spikes > 0 or heart_spikes > 0 are included in the
CSV and shown with ⚡ in the terminal, but excluded from the clean session
averages used for Whoop comparison.

Requirements:
    pip install pyserial

Usage:
    python sleep_logger.py                              # auto-detects port
    python sleep_logger.py --port COM3                  # Windows
    python sleep_logger.py --port /dev/ttyUSB0          # Linux
    python sleep_logger.py --port /dev/cu.usbmodem1101  # macOS
"""

import serial
import serial.tools.list_ports
import csv
import argparse
import sys
import signal
from datetime import datetime
from pathlib import Path

# ── Config ───────────────────────────────────────────────────────────
BAUD_RATE  = 115200
OUTPUT_DIR = Path("sleep_sessions")

# Minimum clean samples (total - spikes) per window to trust a reading
# for the Whoop comparison average.
MIN_CLEAN_SAMPLES = 20

# ── Graceful shutdown ────────────────────────────────────────────────
_running = True

def _handle_signal(sig, frame):
    global _running
    print("\n[logger] Shutting down — generating session summary …")
    _running = False

signal.signal(signal.SIGINT,  _handle_signal)
signal.signal(signal.SIGTERM, _handle_signal)

# ── Port detection ───────────────────────────────────────────────────
def auto_detect_port() -> str:
    candidates = list(serial.tools.list_ports.comports())
    keywords = ("esp", "arduino", "ch340", "cp210", "ftdi", "usb serial", "usbmodem")
    for p in candidates:
        if any(k in (p.description or "").lower() for k in keywords):
            print(f"[logger] Auto-detected: {p.device}  ({p.description})")
            return p.device
    if candidates:
        print(f"[logger] Falling back to: {candidates[0].device}")
        return candidates[0].device
    print("[logger] ERROR: No serial ports found.")
    sys.exit(1)

# ── Helpers ──────────────────────────────────────────────────────────
def safe_int(val, default=0) -> int:
    try: return int(val)
    except: return default

def safe_float(val, default=0.0) -> float:
    try: return float(val)
    except: return default

def clean_samples(row: dict, metric: str) -> int:
    return max(0, safe_int(row.get(f"{metric}_samples")) -
                  safe_int(row.get(f"{metric}_spikes")))

def has_spikes(row: dict) -> bool:
    return safe_int(row.get("breath_spikes")) > 0 or \
           safe_int(row.get("heart_spikes"))  > 0

def is_high_confidence(row: dict, metric: str) -> bool:
    return clean_samples(row, metric) >= MIN_CLEAN_SAMPLES

def fmt_duration(seconds: int) -> str:
    h, rem = divmod(int(seconds), 3600)
    m, _   = divmod(rem, 60)
    return f"{h}h {m}m"

def fmt_bpm(val) -> str:
    return f"{val:.1f} bpm" if val is not None else "N/A"

# ── Live terminal line ────────────────────────────────────────────────
def fmt_live(wall_time: str, row: dict) -> str:
    b_spikes = safe_int(row.get("breath_spikes"))
    h_spikes = safe_int(row.get("heart_spikes"))
    b_icon = "⚡" if b_spikes > 0 else ("✓" if is_high_confidence(row, "breath") else "⚠")
    h_icon = "⚡" if h_spikes > 0 else ("✓" if is_high_confidence(row, "heart")  else "⚠")
    return (
        f"  {wall_time}  |  "
        f"Present={row.get('person_present','?')}  "
        f"State={row.get('state','?'):<14}  "
        f"Breath={row.get('breath_bpm','?'):>5} bpm{b_icon}(spk={b_spikes})  "
        f"Heart={row.get('heart_bpm','?'):>5} bpm{h_icon}(spk={h_spikes})"
    )

# ── Session summary ───────────────────────────────────────────────────
def print_summary(rows: list, csv_path: Path, session_start: datetime):
    if not rows:
        print("[logger] No tracking data recorded.")
        return

    session_end   = datetime.now()
    duration_secs = (session_end - session_start).total_seconds()
    total_windows = len(rows)

    # Spike stats
    total_b_spikes  = sum(safe_int(r.get("breath_spikes")) for r in rows)
    total_h_spikes  = sum(safe_int(r.get("heart_spikes"))  for r in rows)
    total_b_samples = sum(safe_int(r.get("breath_samples")) for r in rows)
    total_h_samples = sum(safe_int(r.get("heart_samples"))  for r in rows)
    spike_windows   = sum(1 for r in rows if has_spikes(r))
    b_spike_pct = round(total_b_spikes / total_b_samples * 100, 1) if total_b_samples else 0
    h_spike_pct = round(total_h_spikes / total_h_samples * 100, 1) if total_h_samples else 0

    # Raw averages — all windows with data
    all_b = [r for r in rows if safe_float(r.get("breath_bpm")) > 0]
    all_h = [r for r in rows if safe_float(r.get("heart_bpm"))  > 0]
    raw_breath = sum(safe_float(r["breath_bpm"]) for r in all_b) / len(all_b) if all_b else None
    raw_heart  = sum(safe_float(r["heart_bpm"])  for r in all_h) / len(all_h) if all_h else None

    # Clean averages — high-confidence, spike-free windows only
    hc_b = [r for r in rows if is_high_confidence(r, "breath") and not has_spikes(r)
                             and safe_float(r.get("breath_bpm")) > 0]
    hc_h = [r for r in rows if is_high_confidence(r, "heart")  and not has_spikes(r)
                             and safe_float(r.get("heart_bpm"))  > 0]
    clean_breath = sum(safe_float(r["breath_bpm"]) for r in hc_b) / len(hc_b) if hc_b else None
    clean_heart  = sum(safe_float(r["heart_bpm"])  for r in hc_h) / len(hc_h) if hc_h else None

    # Presence & sleep efficiency
    present_windows = sum(1 for r in rows if r.get("person_present") == "1")
    asleep_windows  = sum(1 for r in rows if r.get("state") == "LIKELY_ASLEEP")
    efficiency_pct  = round(asleep_windows  / total_windows * 100, 1) if total_windows else 0
    presence_pct    = round(present_windows / total_windows * 100, 1) if total_windows else 0

    lines = [
        "",
        "╔════════════════════════════════════════════════════════════════╗",
        "║                   SLEEP SESSION SUMMARY                       ║",
        "╠════════════════════════════════════════════════════════════════╣",
        f"║  Session start   : {session_start.strftime('%Y-%m-%d  %H:%M:%S')}                     ║",
        f"║  Session end     : {session_end.strftime('%Y-%m-%d  %H:%M:%S')}                     ║",
        f"║  Duration        : {fmt_duration(duration_secs):<12}                           ║",
        f"║  Total windows   : {total_windows} × 30s                                    ║",
        "╠════════════════════════════════════════════════════════════════╣",
        "║  SPIKE REPORT                                                  ║",
        "╠════════════════════════════════════════════════════════════════╣",
        f"║  Breath spikes   : {total_b_spikes} readings  ({b_spike_pct}% of samples)             ║",
        f"║  Heart spikes    : {total_h_spikes} readings  ({h_spike_pct}% of samples)             ║",
        f"║  Windows w/ spikes : {spike_windows}/{total_windows}                                  ║",
        "╠════════════════════════════════════════════════════════════════╣",
        "║  WHOOP COMPARISON  (use CLEAN averages)                        ║",
        "╠════════════════════════════════════════════════════════════════╣",
        f"║  Breath — raw avg   : {fmt_bpm(raw_breath):<12} (all windows)              ║",
        f"║  Breath — clean avg : {fmt_bpm(clean_breath):<12} ← Whoop: respiratory_rate  ║",
        f"║  Heart  — raw avg   : {fmt_bpm(raw_heart):<12} (all windows)              ║",
        f"║  Heart  — clean avg : {fmt_bpm(clean_heart):<12} ← Whoop: heart_rate_avg     ║",
        f"║  Sleep efficiency   : {efficiency_pct}%            ← Whoop: sleep_efficiency_%  ║",
        f"║  Time present       : {presence_pct}%            ← Whoop: total_in_bed_time    ║",
        "╠════════════════════════════════════════════════════════════════╣",
        f"║  CSV → {str(csv_path):<56} ║",
        "╚════════════════════════════════════════════════════════════════╝",
        "",
        "  Legend: ✓ high-confidence  ⚠ low samples  ⚡ spike detected",
        "",
    ]

    for l in lines:
        print(l)

    with open(csv_path, "a", newline="") as f:
        f.write("\n")
        for l in lines:
            f.write(f"# {l}\n")

# ── Main ─────────────────────────────────────────────────────────────
def main():
    parser = argparse.ArgumentParser(description="MR60BHA2 Sleep Session Logger")
    parser.add_argument("--port", default=None)
    parser.add_argument("--baud", default=BAUD_RATE, type=int)
    args = parser.parse_args()

    port = args.port or auto_detect_port()

    OUTPUT_DIR.mkdir(exist_ok=True)
    session_start = datetime.now()
    session_label = session_start.strftime("%Y-%m-%d_%H-%M-%S")
    csv_path      = OUTPUT_DIR / f"sleep_{session_label}.csv"

    print(f"[logger] Connecting to {port} @ {args.baud} baud …")
    print(f"[logger] Writing to    {csv_path}")
    print(f"[logger] Press Ctrl-C to stop and generate session summary.")
    print(f"[logger] Legend: ✓ high-confidence  ⚠ low samples  ⚡ spike\n")

    rows      = []
    col_names = None
    tracking  = False          # mirrors Arduino trackingActive state

    with serial.Serial(port, args.baud, timeout=2) as ser, \
         open(csv_path, "w", newline="") as csvfile:

        writer = None

        while _running:
            try:
                raw = ser.readline()
            except serial.SerialException as e:
                print(f"[logger] Serial error: {e}")
                break

            if not raw:
                continue

            line = raw.decode("utf-8", errors="replace").strip()
            if not line:
                continue

            # ── Ignore all debug / comment lines ─────────────────
            if line.startswith("#"):
                # Surface key lifecycle events to the terminal
                if "TRACKING_STARTED" in line:
                    tracking = True
                    print(f"\n[logger] ▶ Tracking started  {datetime.now().strftime('%H:%M:%S')}\n")
                elif "TRACKING_STOPPED" in line:
                    tracking = False
                    print(f"\n[logger] ■ Tracking stopped  {datetime.now().strftime('%H:%M:%S')}\n")
                else:
                    print(f"[logger] {line}")
                continue

            # ── CSV header ────────────────────────────────────────
            if line.startswith("time_ms"):
                col_names = line.split(",")
                all_cols  = ["wall_time"] + col_names
                writer    = csv.DictWriter(csvfile, fieldnames=all_cols)
                writer.writeheader()
                csvfile.flush()
                print(f"[logger] Header confirmed — {len(col_names)} columns\n")
                continue

            if writer is None:
                print(f"[logger] Waiting for header … ({line})")
                continue

            # ── Data row ─────────────────────────────────────────
            parts = line.split(",")
            expected = len(col_names)

            # Graceful backward compat: pad missing spike columns
            if len(parts) == 7 and expected == 11:
                parts += ["0", "0", "0", "0"]
            elif len(parts) == 9 and expected == 11:
                parts += ["0", "0"]
            elif len(parts) != expected:
                print(f"[logger] Skipping malformed line ({len(parts)} cols): {line}")
                continue

            wall_time = datetime.now().isoformat(timespec="seconds")
            row = dict(zip(col_names, parts))
            row["wall_time"] = wall_time

            writer.writerow(row)
            csvfile.flush()
            rows.append(row)

            print(fmt_live(wall_time, row))

    print_summary(rows, csv_path, session_start)


if __name__ == "__main__":
    main()
