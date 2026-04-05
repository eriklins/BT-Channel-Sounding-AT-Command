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
| `--avg-window` | `5` | Moving average window size |
| `--weights RTT IFFT SLOPE` | `0.25 0.50 0.25` | Weights for combining the three estimation methods |
| `--log FILE` | off | Log all measurements to a CSV file |

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

### Example Output

```
Connecting to /dev/ttyACM0 at 115200 baud...
IFFT oversample: 16x  |  Avg window: 5
Weights - RTT: 0.25  IFFT: 0.50  Slope: 0.25
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
The primary high-accuracy method. Uses the 75 tone Phase Correction Terms (PCTs) at 1 MHz spacing across 2404-2480 MHz.

1. Form complex PCT vectors from local and remote IQ data
2. Compute Channel Transfer Function: `H(k) = PCT_local(k) * conj(PCT_remote(k))`
3. Zero-pad to `75 * oversample` points for improved resolution
4. IFFT to obtain Channel Impulse Response (CIR)
5. Peak detection with parabolic interpolation for sub-bin accuracy
6. Convert peak delay to distance: `d = c * tau / 2`

The conjugate multiplication eliminates the local oscillator phase offset common to both devices. With 16x oversampling and parabolic interpolation, sub-decimeter accuracy is achievable in line-of-sight conditions.

### 3. Phase Slope
Supplementary cross-check method. Fits a linear regression to the unwrapped phase of the CTF vs frequency. The slope is proportional to the time-of-flight.

```
distance = -c * slope / (4 * pi)
```

Less robust in multipath environments but provides an independent estimate.

### Combining Estimates
The three estimates are combined using configurable weights (default: RTT 25%, IFFT 50%, Phase Slope 25%). Only estimates from measurements with `ok` tone quality are included in the moving average.

## Key Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| Tone channels | 75 | 2404-2480 MHz, 1 MHz spacing |
| Bandwidth | 75 MHz | Determines resolution |
| Max unambiguous range | ~150 m | c / (2 * 1 MHz) |
| RTT unit | 0.5 ns | ~7.5 cm per unit |

## Tips for Best Accuracy

- **Line of sight**: Multipath reflections degrade IFFT and Phase Slope estimates
- **Antenna orientation**: Keep antennas oriented consistently; the antenna radiation pattern affects phase measurements
- **Averaging**: Increase `--avg-window` for more stable readings at the cost of responsiveness
- **Quality filtering**: Measurements marked `BAD` (fewer than 15/75 good tones) are excluded from averaging
- **Oversampling**: Higher `--oversample` values improve IFFT peak resolution but use more memory; 16 is a good default
- **Baud rate**: Ensure sufficient UART bandwidth for your antenna path count and procedure rate (see main README)
