#!/usr/bin/env python3
"""
Bluetooth Channel Sounding Distance Estimation

Connects to the UART stream of a BT Channel Sounding AT Command initiator,
parses +IQ: output lines, and continuously computes distance estimates using
RTT-based, IFFT-based (Phase-Based Ranging), and Phase Slope methods.
"""

import argparse
import csv
import re
import sys
import time
from collections import deque
from dataclasses import dataclass, field

import numpy as np
import serial

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

SPEED_OF_LIGHT = 299_792_458.0  # m/s
NUM_TONES = 75
FREQ_START_HZ = 2_404e6  # 2404 MHz
FREQ_SPACING_HZ = 1e6  # 1 MHz spacing
RTT_UNIT_S = 0.5e-9  # each RTT unit = 0.5 ns

# Tone frequencies (Hz)
TONE_FREQS_HZ = np.array([FREQ_START_HZ + i * FREQ_SPACING_HZ for i in range(NUM_TONES)])

# ---------------------------------------------------------------------------
# Data structures
# ---------------------------------------------------------------------------


@dataclass
class IQReport:
    sid: int
    ap: int
    rtt_half_ns: int
    rtt_count: int
    quality_ok: bool
    i_local: np.ndarray
    q_local: np.ndarray
    i_remote: np.ndarray
    q_remote: np.ndarray


@dataclass
class DistanceEstimate:
    rtt_m: float | None
    ifft_m: float | None
    phase_slope_m: float | None
    combined_m: float | None


class DistanceTracker:
    """Per-session, per-antenna-path moving average of distance estimates."""

    def __init__(self, window_size: int = 5):
        self.window_size = window_size
        self._history: dict[tuple[int, int], deque[float]] = {}

    def update(self, sid: int, ap: int, distance: float) -> float:
        key = (sid, ap)
        if key not in self._history:
            self._history[key] = deque(maxlen=self.window_size)
        self._history[key].append(distance)
        return self.average(sid, ap)

    def average(self, sid: int, ap: int) -> float:
        key = (sid, ap)
        buf = self._history.get(key)
        if not buf:
            return float("nan")
        return float(np.mean(buf))


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

# Regex for +IQ lines:
# +IQ:<sid>,ap:<ap>,rtt:<half_ns>,rn:<count>,<tq>,il:[...],ql:[...],ir:[...],qr:[...]
_IQ_RE = re.compile(
    r"^\+IQ:(\d+),"
    r"ap:(\d+),"
    r"rtt:(-?\d+),"
    r"rn:(\d+),"
    r"(ok|bad),"
    r"il:\[([^\]]*)\],"
    r"ql:\[([^\]]*)\],"
    r"ir:\[([^\]]*)\],"
    r"qr:\[([^\]]*)\]"
)


def _parse_int_array(s: str) -> np.ndarray:
    return np.array([int(x) for x in s.split(",")], dtype=np.float64)


def parse_iq_line(line: str) -> IQReport | None:
    """Parse a +IQ: line into an IQReport. Returns None for non-IQ lines."""
    line = line.strip()
    m = _IQ_RE.match(line)
    if not m:
        return None

    i_local = _parse_int_array(m.group(6))
    q_local = _parse_int_array(m.group(7))
    i_remote = _parse_int_array(m.group(8))
    q_remote = _parse_int_array(m.group(9))

    if any(len(a) != NUM_TONES for a in [i_local, q_local, i_remote, q_remote]):
        return None

    return IQReport(
        sid=int(m.group(1)),
        ap=int(m.group(2)),
        rtt_half_ns=int(m.group(3)),
        rtt_count=int(m.group(4)),
        quality_ok=(m.group(5) == "ok"),
        i_local=i_local,
        q_local=q_local,
        i_remote=i_remote,
        q_remote=q_remote,
    )


# ---------------------------------------------------------------------------
# Distance computation: RTT
# ---------------------------------------------------------------------------


def compute_rtt_distance(rtt_half_ns: int, rtt_count: int) -> float | None:
    """Compute distance from accumulated RTT measurements.

    Returns distance in meters, or None if no valid measurements.
    """
    if rtt_count <= 0:
        return None
    avg_rtt_s = (rtt_half_ns / rtt_count) * RTT_UNIT_S
    return SPEED_OF_LIGHT * avg_rtt_s / 2.0


# ---------------------------------------------------------------------------
# Distance computation: IFFT (Phase-Based Ranging)
# ---------------------------------------------------------------------------


def _parabolic_interpolation(magnitudes: np.ndarray, peak_idx: int) -> float:
    """Refine peak position using parabolic interpolation on 3 samples."""
    n = len(magnitudes)
    if peak_idx == 0 or peak_idx >= n - 1:
        return float(peak_idx)

    alpha = magnitudes[peak_idx - 1]
    beta = magnitudes[peak_idx]
    gamma = magnitudes[peak_idx + 1]

    denom = alpha - 2.0 * beta + gamma
    if abs(denom) < 1e-12:
        return float(peak_idx)

    correction = 0.5 * (alpha - gamma) / denom
    return peak_idx + correction


def compute_ifft_distance(
    i_local: np.ndarray,
    q_local: np.ndarray,
    i_remote: np.ndarray,
    q_remote: np.ndarray,
    oversample: int = 16,
) -> float | None:
    """Compute distance via IFFT of the Channel Transfer Function.

    Steps:
      1. Form complex PCT vectors from local and remote IQ data
      2. Compute CTF via conjugate multiplication (eliminates LO phase offset)
      3. Zero-pad and IFFT to get Channel Impulse Response
      4. Find peak with parabolic interpolation
      5. Convert peak index to distance
    """
    local_pct = i_local + 1j * q_local
    remote_pct = i_remote + 1j * q_remote

    # Channel Transfer Function: H(k) = local(k) * conj(remote(k))
    ctf = local_pct * np.conj(remote_pct)

    # Check for all-zero data
    if np.all(np.abs(ctf) < 1e-12):
        return None

    # Zero-pad for oversampling
    n_fft = NUM_TONES * oversample
    ctf_padded = np.zeros(n_fft, dtype=complex)
    ctf_padded[:NUM_TONES] = ctf

    # IFFT -> Channel Impulse Response
    cir = np.fft.ifft(ctf_padded)
    cir_mag = np.abs(cir)

    # Search only the first half (unambiguous range)
    half = n_fft // 2
    peak_idx = int(np.argmax(cir_mag[:half]))

    # Parabolic interpolation for sub-bin accuracy
    refined_idx = _parabolic_interpolation(cir_mag[:half], peak_idx)

    # Convert index to time delay, then to distance
    # tau = refined_idx / (n_fft * delta_f)
    # distance = c * tau / 2
    tau = refined_idx / (n_fft * FREQ_SPACING_HZ)
    distance = SPEED_OF_LIGHT * tau / 2.0

    return distance


# ---------------------------------------------------------------------------
# Distance computation: Phase Slope
# ---------------------------------------------------------------------------


def compute_phase_slope_distance(
    i_local: np.ndarray,
    q_local: np.ndarray,
    i_remote: np.ndarray,
    q_remote: np.ndarray,
) -> float | None:
    """Compute distance from the slope of the CTF phase vs frequency.

    The phase of H(f) is: phi(f) = -2*pi*tau*f + phi_0
    Distance = -c * slope / (4*pi)
    """
    local_pct = i_local + 1j * q_local
    remote_pct = i_remote + 1j * q_remote

    ctf = local_pct * np.conj(remote_pct)

    # Skip tones with near-zero magnitude (unreliable phase)
    magnitudes = np.abs(ctf)
    threshold = np.max(magnitudes) * 0.1
    valid = magnitudes > threshold
    if np.sum(valid) < 10:
        return None

    phases = np.angle(ctf)
    phases_unwrapped = np.unwrap(phases[valid])
    freqs_valid = TONE_FREQS_HZ[valid]

    # Linear regression: phase = slope * freq + intercept
    coeffs = np.polyfit(freqs_valid, phases_unwrapped, 1)
    slope = coeffs[0]

    distance = -SPEED_OF_LIGHT * slope / (4.0 * np.pi)
    return distance


# ---------------------------------------------------------------------------
# Combined estimate
# ---------------------------------------------------------------------------


def compute_distance(
    report: IQReport,
    oversample: int,
    weights: tuple[float, float, float],
) -> DistanceEstimate:
    """Compute distance using all methods and produce a weighted combination."""
    rtt_m = compute_rtt_distance(report.rtt_half_ns, report.rtt_count)
    ifft_m = compute_ifft_distance(
        report.i_local, report.q_local, report.i_remote, report.q_remote, oversample
    )
    slope_m = compute_phase_slope_distance(
        report.i_local, report.q_local, report.i_remote, report.q_remote
    )

    # Weighted combination of available estimates
    estimates = [rtt_m, ifft_m, slope_m]
    w = list(weights)

    total_weight = 0.0
    combined = 0.0
    for est, wt in zip(estimates, w):
        if est is not None and est >= 0:
            combined += est * wt
            total_weight += wt

    combined_m = combined / total_weight if total_weight > 0 else None

    return DistanceEstimate(
        rtt_m=rtt_m,
        ifft_m=ifft_m,
        phase_slope_m=slope_m,
        combined_m=combined_m,
    )


# ---------------------------------------------------------------------------
# Formatting
# ---------------------------------------------------------------------------


def _fmt(val: float | None, unit: str = "m") -> str:
    if val is None:
        return "  N/A  "
    return f"{val:6.2f}{unit}"


def format_result(
    report: IQReport,
    est: DistanceEstimate,
    avg: float,
    show_raw: bool,
) -> str:
    """Format a single measurement result for console output."""
    quality = "ok " if report.quality_ok else "BAD"
    header = f"[Session {report.sid}, AP {report.ap} {quality}]"

    if show_raw:
        return (
            f"{header}  RTT:{_fmt(est.rtt_m)}  IFFT:{_fmt(est.ifft_m)}"
            f"  Slope:{_fmt(est.phase_slope_m)}"
            f"  Combined:{_fmt(est.combined_m)}  (avg:{_fmt(avg)})"
        )

    return (
        f"{header}  RTT:{_fmt(est.rtt_m)}  IFFT:{_fmt(est.ifft_m)}"
        f"  Slope:{_fmt(est.phase_slope_m)}"
        f"  Combined:{_fmt(est.combined_m)}  (avg:{_fmt(avg)})"
    )


# ---------------------------------------------------------------------------
# CSV logging
# ---------------------------------------------------------------------------


class CSVLogger:
    def __init__(self, path: str):
        self._file = open(path, "w", newline="")
        self._writer = csv.writer(self._file)
        self._writer.writerow([
            "timestamp", "session", "antenna_path", "quality",
            "rtt_m", "ifft_m", "phase_slope_m", "combined_m", "avg_m",
        ])
        self._file.flush()

    def log(self, report: IQReport, est: DistanceEstimate, avg: float):
        self._writer.writerow([
            f"{time.time():.3f}",
            report.sid,
            report.ap,
            "ok" if report.quality_ok else "bad",
            f"{est.rtt_m:.4f}" if est.rtt_m is not None else "",
            f"{est.ifft_m:.4f}" if est.ifft_m is not None else "",
            f"{est.phase_slope_m:.4f}" if est.phase_slope_m is not None else "",
            f"{est.combined_m:.4f}" if est.combined_m is not None else "",
            f"{avg:.4f}" if not np.isnan(avg) else "",
        ])
        self._file.flush()

    def close(self):
        self._file.close()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="BT Channel Sounding distance estimation from UART IQ data",
    )
    p.add_argument("port", help="Serial port (e.g. /dev/ttyACM0, COM3)")
    p.add_argument("baud", type=int, help="Baud rate (e.g. 115200)")
    p.add_argument(
        "--oversample", type=int, default=16,
        help="IFFT oversampling factor (default: 16)",
    )
    p.add_argument(
        "--avg-window", type=int, default=5,
        help="Moving average window size (default: 5)",
    )
    p.add_argument(
        "--weights", type=float, nargs=3, default=[0.25, 0.50, 0.25],
        metavar=("RTT", "IFFT", "SLOPE"),
        help="Weights for RTT, IFFT, Phase Slope (default: 0.25 0.50 0.25)",
    )
    p.add_argument(
        "--raw", action="store_true",
        help="Show per-method estimates alongside combined",
    )
    p.add_argument(
        "--log", type=str, default=None, metavar="FILE",
        help="Log measurements to CSV file",
    )
    return p.parse_args()


def main():
    args = parse_args()
    weights = tuple(args.weights)
    tracker = DistanceTracker(window_size=args.avg_window)
    csv_logger = CSVLogger(args.log) if args.log else None

    print(f"Connecting to {args.port} at {args.baud} baud...")
    print(f"IFFT oversample: {args.oversample}x  |  Avg window: {args.avg_window}")
    print(f"Weights - RTT: {weights[0]:.2f}  IFFT: {weights[1]:.2f}  Slope: {weights[2]:.2f}")
    if csv_logger:
        print(f"Logging to: {args.log}")
    print("-" * 72)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}", file=sys.stderr)
        sys.exit(1)

    try:
        while True:
            raw = ser.readline()
            if not raw:
                continue

            try:
                line = raw.decode("ascii", errors="ignore").strip()
            except Exception:
                continue

            if not line:
                continue

            report = parse_iq_line(line)
            if report is None:
                continue

            est = compute_distance(report, args.oversample, weights)

            if est.combined_m is not None and report.quality_ok:
                avg = tracker.update(report.sid, report.ap, est.combined_m)
            else:
                avg = tracker.average(report.sid, report.ap)

            print(format_result(report, est, avg, args.raw))

            if csv_logger:
                csv_logger.log(report, est, avg)

    except KeyboardInterrupt:
        print("\nStopped.")
    finally:
        ser.close()
        if csv_logger:
            csv_logger.close()


if __name__ == "__main__":
    main()
