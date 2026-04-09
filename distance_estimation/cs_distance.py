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

# Default weights for combining RTT, IFFT, and Phase Slope distance estimates
DEFAULT_WEIGHT_RTT = 0.10
DEFAULT_WEIGHT_IFFT = 0.50
DEFAULT_WEIGHT_SLOPE = 0.40

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
    # Optional fields populated when present in the firmware output.
    valid_mask: np.ndarray | None = None  # bool array, length NUM_TONES
    tone_quality: np.ndarray | None = None  # uint8 0-3, length NUM_TONES
    freq_compensation: int | None = None  # 0.01 ppm units, signed


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

# Regex for +IQ lines. The new firmware adds ffo:, m:, q: between the quality
# tag and the IQ arrays; older firmware omits them. Make those fields optional
# so the script remains backwards-compatible.
#
# +IQ:<sid>,ap:<ap>,rtt:<half_ns>,rn:<count>,<tq>[,ffo:<n|na>,m:<hex>,q:<hex>],
#     il:[...],ql:[...],ir:[...],qr:[...]
_IQ_RE = re.compile(
    r"^\+IQ:(\d+),"
    r"ap:(\d+),"
    r"rtt:(-?\d+),"
    r"rn:(\d+),"
    r"(ok|bad),"
    r"(?:ffo:(-?\d+|na),m:([0-9a-fA-F]+),q:([0-9a-fA-F]+),)?"
    r"il:\[([^\]]*)\],"
    r"ql:\[([^\]]*)\],"
    r"ir:\[([^\]]*)\],"
    r"qr:\[([^\]]*)\]"
)


def _parse_int_array(s: str) -> np.ndarray:
    return np.array([int(x) for x in s.split(",")], dtype=np.float64)


def _unpack_valid_mask(hex_str: str) -> np.ndarray:
    """Unpack a hex-encoded validity bitmap into a length-NUM_TONES bool array.

    Bit n of byte n/8 corresponds to tone n (LSB-first within each byte).
    """
    raw = bytes.fromhex(hex_str)
    out = np.zeros(NUM_TONES, dtype=bool)
    for n in range(NUM_TONES):
        byte_idx = n >> 3
        if byte_idx >= len(raw):
            break
        out[n] = bool((raw[byte_idx] >> (n & 0x7)) & 0x1)
    return out


def _unpack_tone_quality(hex_str: str) -> np.ndarray:
    """Unpack 2-bits-per-tone quality codes (0=HIGH, 1=MED, 2=LOW, 3=NA)."""
    raw = bytes.fromhex(hex_str)
    out = np.full(NUM_TONES, 3, dtype=np.uint8)
    for n in range(NUM_TONES):
        byte_idx = n >> 2
        if byte_idx >= len(raw):
            break
        out[n] = (raw[byte_idx] >> ((n & 0x3) * 2)) & 0x3
    return out


def parse_iq_line(line: str) -> IQReport | None:
    """Parse a +IQ: line into an IQReport. Returns None for non-IQ lines."""
    line = line.strip()
    m = _IQ_RE.match(line)
    if not m:
        return None

    ffo_str, mask_hex, qual_hex = m.group(6), m.group(7), m.group(8)
    i_local = _parse_int_array(m.group(9))
    q_local = _parse_int_array(m.group(10))
    i_remote = _parse_int_array(m.group(11))
    q_remote = _parse_int_array(m.group(12))

    if any(len(a) != NUM_TONES for a in [i_local, q_local, i_remote, q_remote]):
        return None

    valid_mask = _unpack_valid_mask(mask_hex) if mask_hex else None
    tone_quality = _unpack_tone_quality(qual_hex) if qual_hex else None
    freq_comp: int | None
    if ffo_str is None or ffo_str == "na":
        freq_comp = None
    else:
        freq_comp = int(ffo_str)

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
        valid_mask=valid_mask,
        tone_quality=tone_quality,
        freq_compensation=freq_comp,
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
    tone_mask: np.ndarray | None = None,
) -> float | None:
    """Compute distance via IFFT of the Channel Transfer Function.

    Steps:
      1. Form complex PCT vectors from local and remote IQ data
      2. Compute CTF as the product of the two PCTs. Per the BT spec
         (PCT_local + PCT_remote = 2*theta_propagation), it is the *sum* of
         the two PCT phases that cancels the LO phase offset and isolates
         the round-trip propagation phase. The complex *product* (not the
         conjugate product) implements that sum.
      3. Zero out tones the firmware reported as invalid / low quality
      4. Zero-pad and IFFT to get Channel Impulse Response
      5. Find peak with parabolic interpolation
      6. Convert peak index to distance
    """
    local_pct = i_local + 1j * q_local
    remote_pct = i_remote + 1j * q_remote

    # Channel Transfer Function: H(k) = local(k) * remote(k).
    # The sum of the two PCT phases (= 2*theta_propagation) cancels the LO
    # offset. Empirically the controller's PCT sign convention is such that
    # the resulting CTF phase *decreases* with frequency, so the IFFT peak
    # lands in the *upper* half of the result (alias of a positive delay).
    # The peak search below handles both halves.
    ctf = local_pct * remote_pct

    # Mask out tones the device flagged as invalid / unmeasured. Without this,
    # CS channels excluded from the channel map (e.g. the BLE primary
    # advertising channels) appear as hard zeros and distort the IFFT peak.
    if tone_mask is not None:
        ctf = ctf * tone_mask.astype(ctf.dtype)

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

    # Find the global peak. Because the CTF phase has the opposite sign on
    # real hardware, the peak corresponding to a positive distance can lie
    # in either half of the IFFT output. Treat indices >= n_fft/2 as the
    # alias of a positive delay (n_fft - idx).
    peak_idx = int(np.argmax(cir_mag))
    refined_idx = _parabolic_interpolation(cir_mag, peak_idx)

    half = n_fft // 2
    if refined_idx >= half:
        delay_bins = n_fft - refined_idx
    else:
        delay_bins = refined_idx

    # Convert to time delay, then to distance.
    # tau = delay_bins / (n_fft * delta_f);  distance = c * tau / 2
    tau = delay_bins / (n_fft * FREQ_SPACING_HZ)
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
    tone_mask: np.ndarray | None = None,
) -> float | None:
    """Compute distance from the slope of the CTF phase vs frequency.

    With CTF formed as PCT_local * PCT_remote the phase encodes the
    round-trip propagation delay, but the controller's PCT sign convention
    makes the slope *negative* for a positive distance, so the formula
    carries a leading minus (matching Nordic's reference implementation).
    """
    local_pct = i_local + 1j * q_local
    remote_pct = i_remote + 1j * q_remote

    # Sum of PCT phases (= 2*theta_propagation) cancels the LO offset.
    ctf = local_pct * remote_pct

    # Prefer the firmware's per-tone validity mask when available; fall back
    # to a magnitude threshold for legacy firmware.
    if tone_mask is not None:
        valid = tone_mask.copy()
    else:
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


def _build_tone_mask(report: IQReport) -> np.ndarray | None:
    """Combine the firmware's validity bitmap and per-tone quality into a
    single boolean mask. Tones marked LOW or UNAVAILABLE are dropped.
    Returns None when neither field is present (legacy firmware)."""
    if report.valid_mask is None and report.tone_quality is None:
        return None

    mask = np.ones(NUM_TONES, dtype=bool)
    if report.valid_mask is not None:
        mask &= report.valid_mask
    if report.tone_quality is not None:
        # 0=HIGH, 1=MED are usable; 2=LOW, 3=UNAVAILABLE are dropped.
        mask &= report.tone_quality < 2
    return mask


def compute_distance(
    report: IQReport,
    oversample: int,
    weights: tuple[float, float, float],
) -> DistanceEstimate:
    """Compute distance using all methods and produce a weighted combination."""
    rtt_m = compute_rtt_distance(report.rtt_half_ns, report.rtt_count)
    tone_mask = _build_tone_mask(report)
    ifft_m = compute_ifft_distance(
        report.i_local, report.q_local, report.i_remote, report.q_remote,
        oversample, tone_mask=tone_mask,
    )
    slope_m = compute_phase_slope_distance(
        report.i_local, report.q_local, report.i_remote, report.q_remote,
        tone_mask=tone_mask,
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


_COMBINE_METHODS = {"avg"}


def combine_antenna_paths(
    estimates: list[DistanceEstimate],
    method: str,
) -> DistanceEstimate:
    """Combine distance estimates from multiple antenna paths into one."""
    if method == "avg":
        def _avg(values: list[float | None]) -> float | None:
            valid = [v for v in values if v is not None]
            return float(np.mean(valid)) if valid else None

        return DistanceEstimate(
            rtt_m=_avg([e.rtt_m for e in estimates]),
            ifft_m=_avg([e.ifft_m for e in estimates]),
            phase_slope_m=_avg([e.phase_slope_m for e in estimates]),
            combined_m=_avg([e.combined_m for e in estimates]),
        )
    raise ValueError(f"Unknown antenna combination method: {method!r}")


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
    ap_label: str | None = None,
) -> str:
    """Format a single measurement result for console output."""
    quality = "ok " if report.quality_ok else "BAD"
    ap_str = ap_label if ap_label is not None else str(report.ap)
    header = f"[Session {report.sid}, AP {ap_str} {quality}]"

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
        self._file = open(path, "a", newline="")
        self._writer = csv.writer(self._file)
        self._writer.writerow([
            "timestamp", "session", "antenna_path", "quality",
            "rtt_m", "ifft_m", "phase_slope_m", "combined_m", "avg_m",
        ])
        self._file.flush()

    def log(self, report: IQReport, est: DistanceEstimate, avg: float,
            ap_label: str | None = None):
        self._writer.writerow([
            f"{time.time():.3f}",
            report.sid,
            ap_label if ap_label is not None else report.ap,
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
        "--weights", type=float, nargs=3,
        default=[DEFAULT_WEIGHT_RTT, DEFAULT_WEIGHT_IFFT, DEFAULT_WEIGHT_SLOPE],
        metavar=("RTT", "IFFT", "SLOPE"),
        help=(
            "Weights for RTT, IFFT, Phase Slope "
            f"(default: {DEFAULT_WEIGHT_RTT} {DEFAULT_WEIGHT_IFFT} {DEFAULT_WEIGHT_SLOPE})"
        ),
    )
    p.add_argument(
        "--log", type=str, default=None, metavar="FILE",
        help="Log measurements to CSV file",
    )
    p.add_argument(
        "--ant-comb", type=str, default=None, metavar="METHOD",
        help="Combine antenna paths using METHOD (e.g. 'avg')",
    )
    return p.parse_args()


def main():
    args = parse_args()
    weights = tuple(args.weights)
    ant_comb = args.ant_comb
    if ant_comb is not None and ant_comb not in _COMBINE_METHODS:
        print(
            f"Unknown --ant-comb method {ant_comb!r}. "
            f"Supported: {', '.join(sorted(_COMBINE_METHODS))}",
            file=sys.stderr,
        )
        sys.exit(1)
    tracker = DistanceTracker(window_size=args.avg_window)
    csv_logger = CSVLogger(args.log) if args.log else None

    print(f"Connecting to {args.port} at {args.baud} baud...")
    print(f"IFFT oversample: {args.oversample}x  |  Avg window: {args.avg_window}")
    print(f"Weights - RTT: {weights[0]:.2f}  IFFT: {weights[1]:.2f}  Slope: {weights[2]:.2f}")
    if ant_comb:
        print(f"Antenna combination: {ant_comb}")
    if csv_logger:
        print(f"Logging to: {args.log}")
    print("-" * 72)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}", file=sys.stderr)
        sys.exit(1)

    # Enable IQ output on the device
    print("Enabling IQ output... ", end="", flush=True)
    ser.write(b"AT+IQ on\r\n")
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        resp = ser.readline()
        if not resp:
            continue
        resp_str = resp.decode("ascii", errors="ignore").strip()
        if resp_str == "OK":
            break
        if resp_str == "ERROR":
            print("device returned ERROR", file=sys.stderr)
            ser.close()
            sys.exit(1)
    else:
        print("timeout waiting for OK", file=sys.stderr)
        ser.close()
        sys.exit(1)
    print("OK")

    # Buffer for antenna path combination: {sid: {ap: (report, est), ...}}
    ap_buf: dict[int, dict[int, tuple[IQReport, DistanceEstimate]]] = {}

    def _flush_combined(sid: int) -> None:
        """Combine buffered AP results for *sid*, output, and clear."""
        buf = ap_buf.pop(sid, {})
        if not buf:
            return
        reports = [r for r, _ in buf.values()]
        estimates = [e for _, e in buf.values()]
        combined_est = combine_antenna_paths(estimates, ant_comb)
        # Use first report as representative (for sid, quality flag).
        rep = reports[0]
        # Any-OK: mark quality_ok if at least one AP was ok.
        quality_ok = any(r.quality_ok for r in reports)
        # Temporarily patch for format/log (sid is the same for all).
        rep_proxy = IQReport(
            sid=rep.sid, ap=-1,
            rtt_half_ns=0, rtt_count=0,
            quality_ok=quality_ok,
            i_local=np.empty(0), q_local=np.empty(0),
            i_remote=np.empty(0), q_remote=np.empty(0),
        )
        ap_key = -1  # sentinel for combined path
        if combined_est.combined_m is not None and quality_ok:
            avg = tracker.update(sid, ap_key, combined_est.combined_m)
        else:
            avg = tracker.average(sid, ap_key)
        label = ant_comb
        print(format_result(rep_proxy, combined_est, avg, ap_label=label))
        if csv_logger:
            csv_logger.log(rep_proxy, combined_est, avg, ap_label=label)

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

            if ant_comb is None:
                # No combination — original per-AP behaviour.
                if est.combined_m is not None and report.quality_ok:
                    avg = tracker.update(report.sid, report.ap, est.combined_m)
                else:
                    avg = tracker.average(report.sid, report.ap)

                print(format_result(report, est, avg))

                if csv_logger:
                    csv_logger.log(report, est, avg)
            else:
                # Buffer per-AP results; AP 0 always starts a new group.
                sid = report.sid
                if sid not in ap_buf:
                    ap_buf[sid] = {}
                if report.ap == 0 and ap_buf[sid]:
                    # AP 0 marks the start of a new round — flush previous.
                    _flush_combined(sid)
                    ap_buf[sid] = {}
                ap_buf[sid][report.ap] = (report, est)

    except KeyboardInterrupt:
        print("\nStopping...")
    finally:
        # Flush any remaining buffered antenna path data.
        if ant_comb is not None:
            for sid in list(ap_buf):
                _flush_combined(sid)
        # Disable IQ output before closing
        try:
            ser.write(b"AT+IQ off\r\n")
            ser.flush()
        except serial.SerialException:
            pass
        ser.close()
        if csv_logger:
            csv_logger.close()


if __name__ == "__main__":
    main()
