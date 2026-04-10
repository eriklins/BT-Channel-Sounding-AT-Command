# Channel Sounding Distance Estimation

Python script that connects to the UART output of a BT Channel Sounding AT Command initiator and computes real-time distance estimates from the raw RTT and IQ data.

## Prerequisites

- Python 3.10+
- An initiator device running the CS AT Command firmware with an active ranging session

## Installation

```bash
cd distance_estimation
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
```

## Usage

First, configure the initiator via AT commands (set role, scan, start ranging). Then connect this script to the same serial port to consume the `+IQ:` data stream:

```bash
.venv/bin/python cs_distance.py /dev/ttyACM0 115200
```

### Command Line Options

| Argument | Default | Description |
|----------|---------|-------------|
| `port` | (required) | Serial port path (e.g. `/dev/ttyACM0`, `COM3`) |
| `baud` | (required) | Baud rate (e.g. `115200`) |
| `--oversample` | `16` | IFFT zero-padding oversampling factor |
| `--ifft-mode MODE` | `highest` | IFFT peak selection: `highest` (strongest peak) or `earliest` (first peak above threshold) |
| `--ifft-thr FLOAT` | `0.5` | Threshold for `earliest` mode, as fraction of global peak (0.0–1.0) |
| `--avg-window` | `5` | Moving average window size |
| `--weights RTT IFFT SLOPE` | `0.10 0.50 0.40` | Weights for combining the three estimation methods |
| `--ref-dist METER` | off | Reference distance in meters; adds a `ref` column to terminal output and CSV |
| `--log FILE` | off | Log all measurements to a CSV file |
| `--ant-comb METHOD` | off | Combine antenna paths using METHOD (e.g. `avg`) |
| `--take-samples NUM` | off | Stop after NUM output lines and terminate |

### Examples

Log to CSV for later analysis:
```bash
python cs_distance.py /dev/ttyACM0 115200 --log measurements.csv
```

Custom weights (RTT-only):
```bash
python cs_distance.py /dev/ttyACM0 115200 --weights 1.0 0.0 0.0
```

Higher IFFT resolution with larger averaging window:
```bash
python cs_distance.py /dev/ttyACM0 115200 --oversample 32 --avg-window 10
```

Log with a known reference distance of 1.5 m (for accuracy evaluation):
```bash
python cs_distance.py /dev/ttyACM0 115200 --ref-dist 1.5 --log measurements.csv
```

Earliest-arrival IFFT mode for multipath environments:
```bash
python cs_distance.py /dev/ttyACM0 115200 --ifft-mode earliest --ifft-thr 0.4
```

Combine antenna paths by averaging:
```bash
python cs_distance.py /dev/ttyACM0 115200 --ant-comb avg
```

Collect exactly 20 samples and exit:
```bash
python cs_distance.py /dev/ttyACM0 115200 --take-samples 20 --log measurements.csv
```

### Example Output

```
Connecting to /dev/ttyACM0 at 115200 baud...
IFFT oversample: 16x  |  Avg window: 5
Weights - RTT: 0.10  IFFT: 0.50  Slope: 0.40
------------------------------------------------------------------------
[Session 1, AP 0 ok ]  RTT:  1.23m  IFFT:  1.18m  Slope:  1.25m  Combined:  1.20m  (avg:  1.21m)
[Session 1, AP 0 ok ]  RTT:  1.20m  IFFT:  1.17m  Slope:  1.22m  Combined:  1.18m  (avg:  1.19m)
[Session 2, AP 0 ok ]  RTT:  3.50m  IFFT:  3.41m  Slope:  3.48m  Combined:  3.43m  (avg:  3.43m)
[Session 1, AP 0 BAD]  RTT:  1.15m  IFFT:  1.10m  Slope:  1.18m  Combined:  1.13m  (avg:  1.19m)
```

## Algorithm Overview

Three independent distance estimation methods are computed and combined:

### 1. RTT (Round Trip Time)
Direct time-of-flight measurement. The firmware accumulates RTT values from Mode 1 CS steps (Access Address correlation).

```
distance = c * (rtt_half_ns / rtt_count) * 0.5ns / 2
```

Accuracy: ~1 m (limited by 0.5 ns resolution = 7.5 cm per unit).

### 2. IFFT (Phase-Based Ranging)
The primary high-accuracy method. Uses the 75 tone Phase Correction Terms (PCTs) at 1 MHz spacing across CS channels 2-76 (2404-2478 MHz).

1. Form complex PCT vectors from local and remote IQ data
2. Compute Channel Transfer Function: `H(k) = PCT_local(k) * PCT_remote(k)`
3. **Mask out tones the firmware reported as invalid or LOW/UNAVAILABLE quality** — see below
4. Zero-pad to `75 * oversample` points for improved resolution
5. IFFT to obtain Channel Impulse Response (CIR)
6. Peak detection with parabolic interpolation for sub-bin accuracy:
   - **highest** (default): global maximum of the CIR
   - **earliest**: first peak above `--ifft-thr` fraction of the global maximum — better in NLOS / multipath environments where the direct path may not be the strongest
7. Convert peak delay to distance: `d = c * tau / 2`

The plain (non-conjugate) product implements the *sum* of the two PCT phases. Per the BT spec the local LO phases enter the two PCTs with opposite signs, so the sum cancels them and leaves `2*theta_propagation`. With 16x oversampling and parabolic interpolation, sub-decimeter accuracy is achievable in line-of-sight conditions.

### 3. Phase Slope
Supplementary cross-check method. Fits a linear regression to the unwrapped phase of the CTF vs frequency. The slope is proportional to the time-of-flight.

```
distance = -c * slope / (4 * pi)
```

Less robust in multipath environments but provides an independent estimate. The same per-tone validity mask is applied before the linear fit.

### Per-Tone Validity and Quality
Channel Sounding does not necessarily measure all 75 tones in every procedure: the BLE primary advertising channels (2402, 2426, 2480 MHz) are excluded from the channel map, the channel selection algorithm may use a subset of the remaining channels, and individual tones can be flagged "not available" by the controller. The firmware reports this on every `+IQ` line via two fields:

- `m` — 75-bit validity bitmap; bit set ⇔ a valid PCT was received for that tone.
- `q` — 2 bits per tone (`0`=HIGH, `1`=MED, `2`=LOW, `3`=UNAVAILABLE).

The script unpacks both, ANDs them into a single boolean tone mask (HIGH+MED kept, LOW/UNAVAILABLE dropped), and zeros out the corresponding bins of the CTF before the IFFT. Without this masking the unmeasured tones would appear as hard zeros in an otherwise dense spectrum and distort the channel impulse response peak. For backwards compatibility, lines from older firmware (without `m`/`q`/`ffo` fields) still parse and fall back to the old magnitude-threshold heuristic for the phase-slope method.

### Frequency Compensation
The optional `ffo` field carries the controller's per-procedure frequency compensation value (mode-0 result, in 0.01 ppm units). The script parses it into `IQReport.freq_compensation` for diagnostics and CSV logging; the IFFT and phase-slope estimators do not currently apply software compensation since the controller has already applied it to the PCTs we receive.

### Combining Estimates
The three estimates are combined using configurable weights (default: RTT 10%, IFFT 50%, Phase Slope 40%). Only estimates from measurements with `ok` tone quality are included in the moving average.

## Key Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Tone channels | 75 | CS channels 2-76, 2404-2478 MHz, 1 MHz spacing |
| Bandwidth | 75 MHz | Determines resolution |
| Max unambiguous range | ~150 m | c / (2 * 1 MHz) |
| RTT unit | 0.5 ns | ~7.5 cm per unit |

## Tips for Best Accuracy

- **Line of sight**: Multipath reflections degrade IFFT and Phase Slope estimates
- **Antenna orientation**: Keep antennas oriented consistently; the antenna radiation pattern affects phase measurements
- **Averaging**: Increase `--avg-window` for more stable readings at the cost of responsiveness
- **Quality filtering**: Measurements marked `BAD` (fewer than 15/75 good tones) are excluded from averaging. Per-tone LOW/UNAVAILABLE flags are also dropped from the IFFT and phase-slope inputs automatically.
- **Oversampling**: Higher `--oversample` values improve IFFT peak resolution but use more memory; 16 is a good default
- **Baud rate**: Ensure sufficient UART bandwidth for your antenna path count and procedure rate (see main README)
