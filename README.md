# CS AT Command

A Bluetooth LE Channel Sounding application for the nRF Connect SDK, controlled via AT commands over UART. The application supports both initiator and reflector roles and outputs raw IQ tone data from Channel Sounding procedures, suitable for external distance estimation on a connected host (e.g. a PC).

Built on the Nordic nRF Connect SDK v3.2.4. Targets nRF54L15-based hardware (tested on Ezurio BL54L15 DVK).

## Commands

### General

| Command | Description | Example |
|---------|-------------|---------|
| `AT` | Test command, returns `OK` | `AT` |
| `ATZ` | Software reset | `ATZ` |
| `ATI version` | Show firmware version | `ATI version` |
| `ATI board` | Show board target | `ATI board` |

### Settings (non-volatile)

| Command | Description | Example |
|---------|-------------|---------|
| `ATS role=?` | Query current role | `ATS role=?` -> `role=initiator` |
| `ATS role=<value>` | Set role (`none`, `initiator`, `reflector`) | `ATS role=initiator` |
| `ATS devicename=?` | Query advertised device name | `ATS devicename=?` -> `devicename="CS AT Command"` |
| `ATS devicename=<name>` | Set advertised device name (quotes optional) | `ATS devicename="My Device"` |
| `ATS adv_autostart=?` | Query adv_autostart setting | `ATS adv_autostart=?` -> `adv_autostart=n` |
| `ATS adv_autostart=<y\|n>` | Auto-start advertising on boot (reflector only) | `ATS adv_autostart=y` |
| `ATS conn_int=?` | Query BLE connection interval (ms) | `ATS conn_int=?` -> `conn_int=400` |
| `ATS conn_int=<ms>` | Set BLE connection interval (10-400 ms) | `ATS conn_int=50` |
| `ATS baudrate=?` | Query current UART baudrate | `ATS baudrate=?` -> `baudrate=115200` |
| `ATS baudrate=<num>` | Set UART baudrate (OK sent at old rate before switching) | `ATS baudrate=921600` |

### Scanning (initiator role)

| Command | Description | Example |
|---------|-------------|---------|
| `AT+SCAN` | Scan indefinitely for reflectors advertising the RAS UUID | `AT+SCAN` |
| `AT+SCAN <timeout>` | Scan for `<timeout>` seconds | `AT+SCAN 10` |
| `AT+SCAN stop` | Stop scanning | `AT+SCAN stop` |

Discovered devices are reported as unsolicited responses:
```
+SCAN:AABBCCDDEEFF,-45,CS AT Command
```
Format: `+SCAN:<mac>,<rssi>[,<name>]`. Scanning completion is indicated by `+SCANDONE`.

### Advertising (reflector role)

| Command | Description | Example |
|---------|-------------|---------|
| `AT+ADV start` | Start advertising with RAS UUID | `AT+ADV start` |
| `AT+ADV stop` | Stop advertising | `AT+ADV stop` |

When a remote device connects or disconnects, unsolicited responses are sent:
```
+CONNECTED
+DISCONNECTED
```

Reflector CS setup status is reported via unsolicited responses:
```
+REFLECTOR READY
+REFLECTOR ERROR
+REFLECTOR TIMEOUT
```

If `adv_autostart=y`, advertising restarts automatically after disconnect.

### Ranging (initiator role)

| Command | Description | Example |
|---------|-------------|---------|
| `AT+RANGE mac=<addr>` | Start ranging session (1000 ms default interval) | `AT+RANGE mac=AABBCCDDEEFF` |
| `AT+RANGE mac=<addr>,int=<ms>` | Start ranging session with custom interval | `AT+RANGE mac=AABBCCDDEEFF,int=500` |
| `AT+RANGEX <id>` | Stop a ranging session | `AT+RANGEX 1` |

On success, `AT+RANGE` returns a session ID:
```
+RANGE:1
OK
```

Session status is reported via unsolicited responses:
```
+RANGE:1 CONNECTING
+RANGE:1 ACTIVE
+RANGE:1 DISCONNECTED
+RANGE:1 ERROR
```

### IQ Data Output

Once a ranging session is active, raw IQ tone data is streamed as unsolicited responses after each Channel Sounding procedure:

```
+IQ:<sid>,ap:<n>,rtt:<half_ns>,rn:<count>,<tq>,il:[...],ql:[...],ir:[...],qr:[...]
```

| Field | Description |
|-------|-------------|
| `sid` | Session ID |
| `ap`  | Antenna path index |
| `rtt` | Accumulated RTT in units of 0.5 ns |
| `rn` | Number of valid RTT measurements |
| `tq` | Tone quality (`ok` or `bad`) |
| `il` | Local in-phase samples (75 integers, 12-bit signed) |
| `ql` | Local quadrature samples (75 integers) |
| `ir` | Remote in-phase samples (75 integers) |
| `qr` | Remote quadrature samples (75 integers) |

The IQ values are raw 12-bit signed Phase Correction Terms (range -2048 to 2047) across 75 tone channels (2404-2480 MHz, 1 MHz spacing).

### IQ Output Bandwidth

Each antenna path produces one `+IQ` line. Worst-case line length is **1863 bytes** on the wire (1861 chars + `\r\n`), based on:

- Header (`+IQ:255,ap:255,rtt:-2147483648,rn:255,bad,`): 42 chars
- 4 IQ arrays x (3 label + 1 `[` + 75 x 5-digit values + 74 commas + 1 `]`): 4 x 454 = 1816 chars
- 3 array separators: 3 chars

The table below shows the maximum number of antenna paths that can be sustained at each baud rate, assuming one CS procedure per second. Multiply the limit proportionally for faster procedure rates (e.g. at 2 procedures/sec, halve the numbers).

| Baud rate | Bytes/sec (8N1) | Max antenna paths/sec | Suitable for |
|-----------|----------------:|----------------------:|--------------|
| 9600      |             960 |                     0 | Not usable for IQ output |
| 19200     |           1,920 |                     1 | 1 antenna path, 1 proc/sec |
| 38400     |           3,840 |                     2 | 2 paths, or 1 path at 2 proc/sec |
| 57600     |           5,760 |                     3 | 1x1 antenna at up to 3 proc/sec |
| 115200    |          11,520 |                     6 | 4 paths at 1 proc/sec (default) |
| 230400    |          23,040 |                    12 | 4 paths at up to 3 proc/sec |
| 460800    |          46,080 |                    24 | Multiple sessions, multiple antennas |
| 921600    |          92,160 |                    49 | All configurations with headroom |

The default baudrate is 115200, which supports the common case of a single-antenna initiator and reflector (2 antenna paths per procedure at 1 Hz) with margin. For multi-antenna configurations or higher procedure rates, increase the baudrate with `ATS baudrate=<num>`. The OK response is sent at the old baudrate before the switch takes effect. The setting is persisted across reboots.

Supported baudrates: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600.

## Typical Usage

### Initiator

```
ATS role=initiator
OK
AT+SCAN 10
OK
+SCAN:EC3CC2C23110,-42,CS AT Command
+SCANDONE
AT+RANGE mac=EC3CC2C23110,int=500
+RANGE:1
OK
+RANGE:1 CONNECTING
+RANGE:1 ACTIVE
+IQ:1,rtt:1234,rn:3,ok,il:[45,-102,78,...],ql:[...],ir:[...],qr:[...]
+IQ:1,rtt:1180,rn:3,ok,il:[42,-98,80,...],ql:[...],ir:[...],qr:[...]
...
AT+RANGEX 1
OK
```

### Reflector

```
ATS role=reflector
OK
ATS devicename="My Reflector"
OK
AT+ADV start
OK
+CONNECTED
+REFLECTOR READY
```

## Connection Interval Tuning

The `conn_int` setting controls the BLE connection interval used for ranging sessions. It directly affects setup speed, ranging throughput, and how many concurrent sessions the radio can sustain.

A shorter connection interval means faster connection setup and security negotiation (each BLE protocol exchange completes in fewer wall-clock seconds) and higher ranging throughput. However, it consumes more radio time per connection, which limits the number of simultaneous sessions.

A longer connection interval frees radio time for additional connections but slows down setup and reduces per-session throughput. With very long intervals, security handshakes (SMP pairing) may approach internal timeouts when multiple connections compete for the radio.

### Recommended values

| Scenario | `conn_int` | Notes |
|----------|-----------|-------|
| Single device, single antenna | 10-30 ms | Fast setup and high throughput |
| Single device, multiple antennas | 30-50 ms | Slightly more radio time per procedure |
| 2 concurrent devices | 50-100 ms | Balances throughput with scheduling headroom |
| 3+ concurrent devices | 200-400 ms | Prevents radio scheduling conflicts |

The default is 400 ms, which is conservative and works reliably for any number of devices. When connecting to only one or two devices, lowering `conn_int` significantly improves responsiveness.

The value must be between 10 and 400 ms and should be a multiple of 1.25 ms (the BLE connection interval unit); non-aligned values are rounded down internally. Changes take effect on the next ranging session without requiring a reset; already-active sessions are not affected.

## Building

```
west build -b <board> -p
```
