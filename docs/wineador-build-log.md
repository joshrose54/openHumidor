# Wineador — Build Log

The test record. What each test measured on the units actually built, and any change that was
needed to make it pass — a part, or something the firmware must do. The design and the tests
themselves are in the **spec**.

1. As built
2. Test results — in spec order
3. Track A — cabinet findings and data
4. Open issues
5. Gotchas

---

## 1. As built

### ESP32

| Field | Value |
|---|---|
| Chip | ESP32-D0WD-V3, revision 3.1 |
| Crystal | 40MHz |
| Flash | 4MB |
| MAC | 20:9b:a9:94:26:d8 |
| USB bridge | CP2102, Silicon Labs, VID 0x10c4 PID 0xea60 |
| Port (macOS) | /dev/cu.SLAB_USBtoUART |
| Onboard LED | GPIO2 |
| Spares | Two boards |

### SHT40 sensors

| Ch | Serial | Label | Position | **RH gain** | **RH offset** |
|---|---|---|---|---|---|
| 2 | 0x0AD409ED | 09ED | Top front | 1.0067 | −2.556 |
| 3 | 0x128EA036 | A036 | Bottom front | 0.9998 | −2.842 |
| 4 | 0x0ADBCB62 | CB62 | Mid front | 1.0274 | −3.878 |
| 5 | 0x10EDA1C5 | A1C5 | Mid rear | 0.9826 | −2.528 |
| 6 | 0x11D7FDF3 | FDF3 | Chamber | 1.0158 | −3.298 |
| 7 | 0x0E42EFBA | EFBA | Ambient | 1.0151 | −3.475 |

**Corrected RH = gain × raw + offset.** From B1.2 test 13. Labels are the last four hex digits of
the serial, on each probe.

### DS3231 RTC

| Field | Value |
|---|---|
| Cell | LIR2032, **3.28V** on 2026-10-08 — low (3.6V nominal); never recharged, so it only falls. Replace before service |
| Control 0x0E / aging 0x10 | 0x1C / 0 |
| Time | UTC, set 2026-10-08 10:44:00 to −0.0s |

**Drift** — `/usr/bin/python3 set_rtc.py --check`:

| Date | Offset | Elapsed |
|---|---|---|
| 2026-10-08 10:44 | −0.0s | Baseline |
| 2026-10-08 10:54 | −0.3s | 11 min, after a 10-minute unplug |
| 2026-11-07 | Time lost | Read 2000-01-01 after the J9 lead was reseated; then kept counting through a 12V cut. Not reset — the DS3231 is replaced on the next board |

### SD card

64GB high-endurance, FAT32, MBR, volume WINEADOR. SPI 4MHz. Format on macOS with
`sudo diskutil eraseDisk FAT32 WINEADOR MBRFormat /dev/diskN` — Disk Utility may refuse FAT32
above 32GB, and the ESP32 library needs MBR not GPT.

### KSD9700

35°C unit fitted. Trips 32.9–33.3°C, resets 23.2–24.6°C (B1.3 test 14).

### Board — measured

| Part | Measured |
|---|---|
| 10kΩ P26, P27, P32 to GND | 9.5–10kΩ each |
| 1kΩ P34 to 3.3V | Just under 1kΩ; 3.29V at the pin, power on |
| Plenum fan at 20%, Rev N | 414–426 RPM, steady — against 438–450 in B1.1.8 |

**Wire colours:** green SDA · yellow SCL · red 3.3V or 5V · black GND · white NC1 output · orange
NO2 output.

### Firmware — measured on Rev N

| Item | Value |
|---|---|
| SD mount at boot | 60 ms |
| Loop pass, longest per status interval | 1.2–4.5 ms; 2.7–3.1 ms with the RTC lead out |
| Build size, each ESP32 build | ~8% RAM, ~31% flash |

**Cabinet model rates** (test builds only, B3):

| Response | Rate | Source |
|---|---|---|
| Room value drifted towards | 20°C | A2 room, 20.3°C mean |
| Drift back | +0.006°C/min | A2 |
| Cooling | −0.1°C/min | A2 |
| Heating | +0.1°C/min | Chosen: 1.5°C in 15 min against the 0.5°C check |
| Humidifying | +0.1 points/min | Chosen: 2 points in 20 min against the 0.5-point check |
| Chamber | As the cabinet until stage 4 sets its own rates for harvest | — |

### Bench thermometer

Waterproof probe. **Reads 0.4°C high** (crushed ice, 2026-10-08). Subtract 0.4.

---

## 2. Test results — in spec order

### B1.1.1 ESP32 — ✅

Flashed and ran. Details under As built.

### B1.1.2 I²C bus — ✅

0x70, 0x68, 0x57 present with all channels closed.

### B1.1.3 TCA9548A — ✅

Every channel selectable; SHT40 at 0x44 on channels 2–7.

### B1.1.3a Mux reset — ✅

- Register read 0x00 after every P16 pulse, 30 of 30; the sensor answered before each pulse and
  was gone after. P16 undriven rests HIGH
- Stuck bus: channel 3's SDA to GND. Unselected, harmless. Selected, the whole bus locked — RTC
  unreachable, the deselect command failed. **One P16 pulse freed it with the fault still present**;
  channel 2 then worked normally
- **For firmware:** after the pulse, restart the I²C driver (`Wire.end()`, `Wire.begin()`) — the
  test did, as a stuck bus can leave it confused

### B1.1.4 and 4a SHT40 — ✅

All six genuine: 20/20 CRC, soft reset and all precision modes, heater rise scaling with power.

| Ch | 20mW | 110mW | 200mW |
|---|---|---|---|
| 2 | +3.16°C | +14.65°C | +23.18°C |
| 3 | +3.47°C | +15.90°C | +25.00°C |
| 4 | +3.45°C | +15.93°C | +25.05°C |
| 5 | +3.40°C | +15.73°C | +24.74°C |
| 6 | +3.60°C | +16.39°C | +25.63°C |
| 7 | +3.56°C | +16.47°C | +25.84°C |

### B1.1.5 DS3231 — ✅

- Clock held through a 10-minute unplug: offset −0.3s, OSF clear
- Set over serial with `ds3231_settime_auto.cpp` and `set_rtc.py`, which handshakes on boot and
  sends UTC on a whole-second boundary. **Never set from `__TIME__`** — local time, and frozen
  before upload
- Weekly drift still being tracked — table under As built
- See Open issues: an unexplained 2h27m loss, and the temperature register

### B1.1.6 SD module — ✅

| Test | Result |
|---|---|
| 1MB write, verify | 0 errors |
| Write / read | 366 / 410 kB/s |
| Remount ×10 | 10 of 10 |
| 100 appends | 0 failures; mean 7.3ms, worst 18ms |

**For firmware:** the worst case is from a nearly empty card; wear levelling can stall a write for
hundreds of milliseconds as it fills. Logging must not block. macOS writes `.fseventsd` and
`.Spotlight-V100` to the card — harmless; suppress with `mdutil -i off /Volumes/WINEADOR`.

### B1.1.7 DR21A01 — ✅

De-energised COM1–NC1 closed, COM2–NO2 open. Both poles switch; audible click. Coil 10–20mA at the
12V side.

### B1.1.8 Fan — ✅ after two changes

**Changes required:**
1. **Hardware — tach pull-up 10kΩ → 1kΩ, plus 1µF P34 to GND.** With 10kΩ the tach read ~9,870 RPM
   at every duty: 25kHz PWM coupling into the tach wire in the fan cable. Spec updated (2.4, 3.1)
2. **Firmware — count tach pulses by width (≥1ms low), not edges or a fixed window.** With the
   filter fitted the edges chatter: ~11 transitions per real pulse at 100%, ~40 with PWM switching.
   An 8ms acceptance window double-counted everything below ~1,875 RPM (75% read 3,520). The 1ms
   rule rejects all of it. In `fan_test.cpp` and `soak_test.cpp`

**Results with both changes:**

| Check | Result |
|---|---|
| No PWM signal | 2,260 RPM — safe-fail confirmed |
| 100% | 2,280 RPM |
| Stopped fan, 10s | **0 pulses, 0 transitions** |
| 20% | 450 RPM |
| Direct start at 20% | 3 of 3, 440–450 RPM |
| Kickstart then 20% | 3 of 3 |
| Stability at 20% | Mean 443, 438–450 — ±1 pulse of resolution |
| Stalls when slowing | 7%; lowest running 8%, ~180 RPM |
| Starts from rest | 9%, ~170 RPM — rough, barely turning |
| 12V current | Full 0.06A; 50% 0.02A; stopped 0.00A |

**RPM against duty** — baseline for bearing-wear checks:

| Duty | RPM | Duty | RPM |
|---|---|---|---|
| 100% | 2,280 | 50% | 1,200 |
| 95% | 2,160 | 45% | 1,090 |
| 90% | 2,060 | 40% | 960 |
| 85% | 1,970 | 35% | 840 |
| 80% | 1,860 | 30% | 720 |
| 75% | 1,770 | 25% | 580 |
| 70% | 1,650 | 20% | 450 |
| 65% | 1,550 | 15% | 310 |
| 60% | 1,440 | 10% | 180 |
| 55% | 1,320 | 5% and 0% | 0 |

### B1.1.8a Fan PWM idle voltage — ✅

3.85V open-circuit on the blue wire, but only 45.3µA with it held to GND: the fan's pull-up is
~85kΩ. Only microamps can reach P25 undriven. **No series resistor fitted** — spec updated.

### B1.1.9 Boot state — ✅

P26, P27, P32 low through 10kΩ; P34 high at 3.3V through 1kΩ. Rechecked after the pull-up swap.

### B1.2.10+11 Combined soak — ✅

Two hours, five sensors, RTC and SD, every 15s.

| Check | Result |
|---|---|
| Sensor reads | 2,395, zero errors — no CRC, NAK, short read or mux failures |
| SD | 479 writes, 0 failures; mean 9.2ms, worst 19ms, no creep |
| RTC | 479 reads, 0 failures |
| Heap | Flat — 288,760 bytes for 115 minutes after one 240-byte drop at the first file write |

**For firmware:** the sketch's own summary showed a false 36KB heap drop by sampling before the SD
library allocated and during a write. Take the baseline after initialisation; never sample during
a write.

Uncalibrated agreement in the same room air: temperature spread 0.10°C mean, 0.17 worst; RH
1.49% mean, 1.85 worst.

### B1.2.12 Soak with fan — ✅

Two hours, fan at 100%, six sensors, pre-flight checking every serial and fan speed.

| Check | Result |
|---|---|
| Sensor reads | 2,874, zero errors |
| Boots | One |
| Heap | 286,128 bytes, every row |
| SD | 479 writes, 0 failures, **worst 30ms** |
| RTC | 479 reads, 0 failures |
| Fan | 2,234–2,304 RPM, mean 2,284; 6,021,907 transitions for 545,976 pulses — chatter fully rejected |

**Change required — firmware:** SD file dates read 1 January 1980 because the ESP32 system clock
was never set. **Set it from the RTC at boot with `settimeofday`.** Added to the calibration
sketches and to the spec (5.4).

### B1.2.13 Humidity calibration — ✅ after a container change

**Change required — method:** the magnesium chloride run would not settle in a sandwich bag or a
taped takeaway tub, stalling 5–6 points high; a diluted liquid layer on the slurry contributed.
Rerun in a **rigid gasketed clip-lid box**, one snug White-Tack-sealed hole per sensor, probes
only inside, slurry spread thin and wide: settled. Spec test 13 rewritten to this method.

**References**, corrected by the sketch for measured temperature: salt 75.47% at 20°C; magnesium
chloride 33.13% at 18.7°C.

**Salt run** — 16.75 hours, sealed bag, `salt_calibration.csv`. Settled within an hour. Offsets
from the six hours of steady temperature, cross-checked against a temperature-lag model — agree
within 0.05. The bag may have leaked slightly, so these offsets may understate the bias by up to
~0.3.

**Magnesium chloride run** — 16 hours, gasketed box, `cal_mgcl2.csv`. Still settling slowly and the
room warming at the end, so readings come from a model fitted to hours 2–16: one settling curve
shared by all six (time constant 6.0h) plus a ~45-minute temperature lag (−0.70 points per °C).
Residual 0.04; ±0.2 on the level, sensor-to-sensor differences firm. About 0.1 above the sketch's
last-4-hour means.

| Ch | Raw at 75.47% | Reads high by | Raw at 33.13% | Reads high by | **Gain** | **Offset** |
|---|---|---|---|---|---|---|
| 2 | 77.51 | +2.04 | 35.45 | +2.32 | 1.0067 | −2.556 |
| 3 | 78.33 | +2.86 | 35.98 | +2.85 | 0.9998 | −2.842 |
| 4 | 77.23 | +1.76 | 36.02 | +2.89 | 1.0274 | −3.878 |
| 5 | 79.38 | +3.91 | 36.29 | +3.16 | 0.9826 | −2.528 |
| 6 | 77.54 | +2.07 | 35.86 | +2.73 | 1.0158 | −3.298 |
| 7 | 77.77 | +2.30 | 36.06 | +2.93 | 1.0151 | −3.475 |

Every sensor reads 2–4 points high at both ends; gains within 3% of 1. Magnesium chloride was
Aksoy bath flakes (near-pure hexahydrate, up to ~2.5% other salts), which may shift the reference
by a few tenths.

**For firmware:** readings lag temperature changes by 30–45 minutes; the two salts give absolute
accuracy only between 33% and 75%.

### B1.3.14 KSD9700 — ✅ 35°C unit chosen

Readings corrected −0.4°C for the thermometer.

| Unit | Trial | Trip | Reset |
|---|---|---|---|
| 35°C | 1 | 33.3°C | 23.2°C |
| 35°C | 2 | 32.9°C | 24.6°C |
| 35°C | 3 | ~33.1°C | 24.6°C |

Resets above cabinet temperature, so it re-closes by itself. **Chamber ceiling 30°C** — spec
updated (2.6, 4.4, 5.5).

**30°C unit not used:** found open at ~20°C and closed only when tapped; tripped within a second
held in the hand. Its reset sits at about room temperature, so it would stay open after a trip.
25°C not tested. **Handle cut-outs by the terminals** — body heat through the metal face trips a
30°C unit.

### B1.3.15 Buck converters — ✅

| | Buck A | Buck B |
|---|---|---|
| No load | 5.13V | 5.13V |
| Loaded | 5.13V — ESP32, ~45mA | 5.13V — fan at 100%, ~150mA |
| Input at 12V | 0.02A | 0.07A, ~92% efficient |

5.13V accepted: within meter tolerance of 4.9–5.1V, nothing downstream sensitive. The ESP32's 5V
pin reads ~4.6V — the devkit's own Schottky drop.

### B1.3.15a 12V rail at full load — ✅

`full_load_test.cpp`, loads added one at a time. PSU set to 12.0V, 4A fuse.

| Step | PSU current | Added |
|---|---|---|
| Baseline — blowers, buck, ESP32 | 0.44A | — |
| + Plenum fan 100% | 0.52A | 0.08A |
| + Relay energised | 0.54A | 0.02A |
| + Heater, 12V bulb | 2.34A | 1.80A |
| + Humidifier fan — everything on | **2.48A** | 0.14A |
| All off | 0.44A | — |

Rail at the 12V WAGO, everything on: **11.8V** (0.2V below the setting). Full load is 62% of the
fuse rating.

### B1.3.16 Circulation blowers — ✅

All four GDB5015 blowers run on the 12V WAGOs; 0.47–0.50A total on the bench PSU's display.

### B2.1 Inline fuse — ✅

4A blade fuse. Continuity through the fuse and both joints; 12V on the rail.

### B2.2 Heater D4184 — ✅

Bulb off with no glow when the signal was off; followed 1, 5 and 10Hz; fully lit at steady on —
**1.84A** from the PSU including Buck B and the relay. **Relay de-energised with the signal high:
bulb stayed off.**

### B2.3 SSR-buffer D4184 — ✅

A1–A2 with the signal on: **11.96V**. Signal off: −0.03 to +0.05V, never an exact 0 —
meter noise and stray offset, not switching. **Relay energised with the signal high: stayed in that
off range.** With the PSU off and USB connected, A1–A2 read −0.11V; harmless.

### B2.4 Switched lead, SSR, X2 and both MOVs — ✅

**Built:** extension cut through; earth and neutral rejoined with crimped slide-together
connectors (insulated with heatshrink and cable-tied); rings at the SSR per spec 3.3. MOV and X2
legs extended with 18 AWG 600V lead wire, soldered and double-heatshrunk. Neutral is two crimp
terminals linked together: SOCKET LEAD blue + X2 blue in one, PLUG LEAD blue + MOV 2 blue in the
other — electrically one node.

**Unplugged checks, all as expected:** earth and neutral continuous plug to socket; live open plug
to socket; live reaches L13 from the plug and LOAD from the socket; no short between any pair at
either end; polarity cross-checks open; L–E, N–E at the plug and L–E at the socket all **OL** on the
top resistance range. X2 confirmed across the socket by charging on resistance range (the meter
has no capacitance range; no blip on continuity — too small to register).

**Live, 60W filament lamp, `ssr_lamp_test.cpp` stepped manually:** lamp off with no glow on power-up
and throughout every off phase; fully on in both on phases; **relay energised with the signal high:
stayed off.** Never switched on by itself.

### B2.5 SSR leakage — ✅

AstroAI AM33D (CAT II 600V / CAT III 300V; probes CAT II 1000V), 600V~ range, 1V resolution.
Meter proved on a wall socket: **235V**. Switched socket, empty, SSR off (bench PSU off), X2
fitted: **~20V at switch-on, settling to 6V over about 20 seconds.** Cause of the initial higher
reading not established — possibly meter settling. Settled value is the result.

### B2.6 Heater pad — ✅

100%, free air (propped, both faces open), no cut-out, `heater_pad_test.cpp`. Probe taped to the
pad centre. Readings corrected for the thermometer's +0.4°C.

| Time | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11–12 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Pad °C | 22.2 | 25.7 | 31.2 | 35.7 | 38.8 | 40.7 | 42.8 | 43.7 | 44.5 | 45.1 | 45.6 | 45.1–45.6, level |

Room 22.2°C, assumed constant (one probe). **Plateau ~45.5°C — about 23°C above room.** Time
constant roughly 3 minutes in free air. **PSU 1.83A** total, including Buck B and the relay.
Slight smell on this first heating, no smoke — taken as manufacturing residue; recheck on the next
heatings.

### B2.7 KSD9700 in series — ✅

35°C unit in series with the 12V bulb, heater output at 100% (`heater_pad_test.cpp`). Heated with
a hairdryer, cooled in air. Two cycles: bulb on at start; **off when the cut-out opened, back on
when it reset**, both times. Clean switching — no flicker or buzz. Not timed (no temperature
reference with a hairdryer; trip and reset points are from B1.3 test 14).

### B2.8 Interlock with real loads — ✅ after a change

12V bulb (KSD in series) on the heater D4184, 60W lamp in the switched socket, both load signals
high, relay flipped both ways twice (`interlock_test.cpp`, stepped manually). **Only one load was
ever on: de-energised → mains lamp only; energised → heater bulb only.** Never both.

D4184 PWM inputs to board GND, DC:

| Phase | SSR-buffer PWM | Heater PWM |
|---|---|---|
| All off | 0.00–0.01V | −1.7 to −1.8V |
| Both high, relay de-energised | 3.3V | −1.7 to −1.8V |
| Both high, relay energised | −1.74V | 3.3V |

The selected input reads 3.3V. **The unselected input — its relay contact open, so floating —
read about −1.7V.** Cause not established; harmless (negative cannot drive the input
optocoupler, and the load stayed off), but undefined.

**Change needed to pass:** 10kΩ from each D4184's PWM to its GND terminal — spec updated (2.4 parts
table, 3.3 interlock relay). Fitted on the bench across the modules' input terminals; on the board
they sit on the NC1 and NO2 nets. Re-run: **unselected input 0V**, loads as before.

### B2.9 Humidifier D4184 — ✅ after a change

| Check | Result |
|---|---|
| P33 to GND, power off | 9.8kΩ |
| Power on, P33 not driven | Fan off |
| On/off cycles | Starts cleanly every time; stops when off |
| Across the fan, on | 11.8V |
| Module | Cool |

**Change needed to pass.** First run had the module and a 5V fan on the 5V rail: 3.83V across the
fan, 0.09A, a stutter on every start. The D4184 takes its gate drive from its own + terminal, so a
5V supply leaves the MOSFET only part on. Module + / − moved to the 12V WAGOs with a 12V fan
(GDB5015); re-run passed. Spec 2.6 now forbids a 5V load on a D4184.

---

**B2 complete.**

---

## BP — PCB bring-up, bench unit Rev N

**As built: Rev N**, not Rev P. Same circuit except: **no D7** (the ESP32's 5V pin is on the board's
5V rail directly), **no R17–R20** (the four control lines go straight from the ESP32 to U9), **no
TP7**. So the Rev K–M power order applies on this board: 12V on before USB in, USB out before 12V
off, flash the ESP32 off the board. D7, R17–R20 and TP7 checks are n/a here and are tested on Rev P.
Where a step says TP7, TP3 or another GND test point was used.

### BP1 Rails not shorted — ✅

Bare board: TP6, TP1, TP2 each to TP3, and TP6–TP1, all open. J1 pin 1 to TP6 open.

### BP2 Continuity spot checks — ✅

Bare board: every listed pad-to-pad path beeps; J10 pin 1 to pin 3 and J11 pin 2 to TP3 open. J10
pins 1 and 3 each reach a different K1 contact pad. The D7 checks (U1 pin 19 to D7, U1 pin 19 to TP1
open) and the R17–R20 checks are n/a on Rev N.

### BP3 Fit check — ✅

J10, a 2-way XH socket, D1/D7, a TO-220 and K1 all seat without force. No fab batch number on the
board or packaging.

### BP4 Assembly — ✅

All parts soldered, each in its outline and orientation: C1 and every diode band to the square pad,
U8 inside its outline, J10 openings to the left edge. U1, U9, U2 and U4 not plugged in.

### BP5 Inspection — ✅

No bridges, dry joints or unfilled holes; flux cleaned; nothing under the TO-220 tabs.

### BP6 Rails still not shorted — ✅

Nothing plugged in. TP6–TP3 OL. TP1–TP3 settles at ~60kΩ (falls from high while C2/C4 and U8's
output settle). TP2–TP3 rises to OL (C3 charging through R4).

### BP7 Resistance to TP3 — ✅

U1 P26, P27, P32, P33 ~10kΩ. U9 socket pins 1, 4, 9, 12 ~10kΩ (no R17–R20 on Rev N, so 10kΩ not 11kΩ). Q2–Q5 gates ~100kΩ. Q1 gate ~10kΩ.
Female sockets: probe the solder joints underneath — the tips don't reach the contacts from above.

### BP8 Diodes — ✅

Diode mode. Q1 body diode (J1 pin 1 → TP6) 0.484V, reversed OL. D2 (TP3 → TP6) 0.57V. D7 n/a on
Rev N.

### BP9 Cross-checks — ✅

All open: SCL–SDA on J3, J5, J8; J3 SCL–J5 SCL; J3 SDA–J8 SDA; J10 pins 1–3 and 2–4; J11 pin 2–TP3.

### BP10 First power, 12V only — ✅

Bench PSU 12.0V, 0.5A limit, nothing plugged in. PSU current 0.00A (below its resolution). TP6 12V,
TP1 5V, U1 pin 19 5V, J11–J15 pin 1 all 12V. U8 and Q1 cold after a minute.

### BP11 Modules fitted, boot state — ✅

U2, U4, RTC lead, U9 and the ESP32 fitted. `boot_state_test.cpp`: P26, P27, P32, P33 LOW; P34 HIGH,
every read. Q2–Q5 gates ~0V; J10 pin 1 ~12V; TP2 in range; K1 silent, nothing running. J10 pin 3
read 0.9V unloaded — leakage onto the floating NO node; 0V with 10kΩ to GND.

### BP12 Each output alone — ✅

`bp12_outputs_test.cpp`. Relay: J10 pin 1 ~12V / pin 3 ~0V de-energised, the reverse energised.
Heater bulb follows 1, 5, 10Hz and steady on; goes out when the relay is de-energised with the
signal still on. SSR +A1 to −A2 ~0V off, ~12V on, ~0V with the relay energised and the signal on.
Humidifier fan starts cleanly twice, ~12V across it on. Occasional small negative readings where
0V was expected.

### BP13 Interlock with real loads — ✅

`bp13_interlock_test.cpp`, both load signals on, relay flipped both ways twice. Mains lamp on the
bench extension. Relay off: only the mains lamp lit; relay on: only the 12V bulb. Q2 and Q3 gates
~5V throughout.

### BP14 Full load and the 12V rail — ✅

`full_load_test.cpp`, PSU set to 12.0V, 3A limit, no mains.

| Step | PSU current |
|---|---|
| Baseline — blowers, board | 0.50A |
| + Plenum fan 100% | 0.55A |
| + Relay energised | 0.56A |
| + Heater, 12V bulb | 2.35A |
| + Humidifier fan — everything on | **2.46A** |
| All off | 0.48A |

At full load: PSU terminals 11.84V, J1 11.70V, TP6 11.65V (to GND) — **0.05V across Q1 and the board's tracks**.
TP1 5V. Q1, Q3, U8, D1, D7 cool after two minutes. The rest of the drop is the bench PSU's own
sag (0.16V) and its leads, fuse holder and XT30 (0.14V).

### BP15 3.3V margin — ✅ (Rev N)

`margin_test.cpp` (first version), 10 minutes, WiFi transmitting at full power, SD writing, fan
100%, 12V on, USB in. Lowest TP2 **3.30V**; ESP32 5V pin 5.03V — the board rail, since Rev N has no
D7. No reset. **This does not test D7:** Rev P repeats BP15 with D7 fitted and USB out.

### BP16 Power in any order — n/a on Rev N

Rev N has no D7, so the power order is a rule on this board, not a test.

### BP17 Combined soak — ✅

`soak_test.cpp` (24-hour version, stopped at **620 minutes**). Pre-flight: all six serials matched,
fan 2,175rpm. Heater pad (KSD in series) on J10 pins 3–4, 1s on / 1s off for the first hour; four
blowers on; bench PSU 12.0V.

| | Result |
|---|---|
| Sensor reads | 14,874, **0 errors** — heater hour 0 of 1,440, after 0 of 13,434 |
| Boots | 1 (POWERON) — no reset |
| SD | 2,479 writes, 0 failures, mean 11.4ms, worst 30ms |
| RTC reads | 2,479, 0 failures |
| Heap | baseline 285,648; range 285,504–285,520 over the run |
| Fan | 2,174–2,252rpm, mean 2,223 |

The first start failed with I²C and SD errors: the ESP32 was not fully seated in the right-hand
socket after being refitted. Reseated, it ran as above.

---

**BP complete on Rev N.** The D7 checks, BP15 with D7 and BP16 wait for Rev P.

---

## 3. Track A — cabinet findings and data

Inkbird-controlled, before the controller. Moisture as vapour pressure (kPa) where stated, which
removes temperature's effect on RH.

### Findings that shaped the design

- **The RH dip is made entirely during the compressor run** and recovers the moment the compressor
  stops; harvest returns water, it does not strip it → dosed control, slow fan during the run
- **Run length sets how much water is stripped**, not the cooling delivered: 3°C hysteresis gave
  ~22-point dips, 0.5°C halved them → tight cooling hysteresis
- **A sensor in a discharge stream dips far deeper than settled air** — 17.5 points against 11 →
  control sensors out of the streams; stream weighting
- **Loading did not shrink the dip.** The longer run makes the plate colder, cancelling the
  buffering: 11.8 points loaded against ~11 empty. The dip is cabinet-wide — settled levels dip
  about 11 too
- **The RH overshoot after recovery almost vanished loaded** — +5 points empty, +1 loaded — from the
  boxes and packaging, which respond in minutes, not the cigars
- **The moisture balance is flat** empty and loaded; Bovedas keep up
- **The cabinet is well mixed** — spread 0.41°C mean, 0.70 max, far below the 2°C control-source
  threshold
- **The cabinet is not sealed from the room.** A humid spell raised room moisture 14%; the cabinet
  rose 0.6% and levelled within a day. Paths: the door gasket, held open by test cables, and the
  open drain. Direction follows the room's absolute moisture — in winter a heated room is drier
  than the cabinet and the Bovedas will drain faster
- **Inkbird run lengths are an upper bound.** It reads one mid-cabinet probe with no anticipation;
  the controller averages four and stops early
- **Condensate** pools on the floor and does not reach the drain lip in normal operation; some
  evaporates back from the plate. The open drain is not the leak originally feared
- **Harvest overshoot** 0.6–0.9°C empty, ~0.4°C loaded — thermal mass absorbs the residual cold

### A1 — Empty baseline

Inkbird 17.9°C, CD 0.5, fan 100%, four Govees, ambient 21.6–22.3°C.

| Measure | Value |
|---|---|
| Compressor run | 3 min 30 s |
| Cycle interval | 41–47 min, mean 43 |
| Duty cycle | ~8% |
| RH dip, mean of four | ~11 points (60 → 48) |
| RH dip, sensor in the discharge stream | 17.5 points |
| Moisture balance, 4 hours | Flat — 1.259, 1.273, 1.247, 1.259 kPa |
| Harvest overshoot | 0.6–0.9°C, scaling with run length |
| Vertical temperature spread | 0.7–1.2°C |

Average cycle, mean of the sensors: 1.214 kPa before cooling → 0.944 at the trough (−0.270) →
1.303 after harvest (+0.359) → ~1.214 at the next start, the Bovedas absorbing the 0.089 overshoot.
Net change per cycle: zero.

### A2 — Loaded baseline

2026-09-27 to 30. Cigars loaded (previously at 62% in the Klarsteins), setpoint 18°C, CD 0.5, fan
100%, door shut. 31.9 hours / 18 cycles for cycle behaviour; three days for moisture. Shelves 1 and
2 hold boxes; 3, 4 and 5 two trays each; a Boveda at the back of every shelf. Cables through the
door seal.

| Measure | A1, empty | A2, loaded |
|---|---|---|
| Ambient | 21.6–22.3°C | 19.9–20.6°C, mean 20.3 |
| Compressor run | 3 min 30 s | ~5 min 30 s |
| Cycle interval | 43 min | **98 min**, 82–114 |
| Duty cycle | ~8% | ~5.6% |
| Harvest overshoot | 0.6–0.9°C | 0.4°C, one observation |
| Stream-weighted RH dip | — | **~11.8 points** |
| Mean dip, settled levels | ~11 | 10.8 |
| RH overshoot after recovery | ~+5 | ~+1 |
| Time-weighted cabinet RH | — | **61.7%** |
| Vertical temperature spread | 0.7–1.2°C | Mean 0.41°C, max 0.70 |
| Moisture balance | Flat | Flat — 1.2468 → 1.2473 kPa over 32 hours |

Cycles half as frequent, but mostly the room: ambient 20.3°C against 22, halving the heat leaking
in. Load and weather cannot be separated in this data.

**By level:**

| Govee | Position | Plenum feature | Mean temp | Mean RH | Mean dip | Temp drop per cycle |
|---|---|---|---|---|---|---|
| L1 | Top shelf | Fan intake, no slot | 17.83°C | 61.2% | 9.9 | 0.41°C |
| L2 | Shelf 2 | 6mm slot | 17.77°C | 61.6% | 10.9 | 0.27°C |
| L3 | Shelf 3 | No slot | 17.64°C | 61.8% | 11.7 | 0.28°C |
| L4 | Shelf 4 | 9mm slot | 17.46°C | 62.6% | 15.3 | 0.80°C |
| L5 | **Floor**, beside L6 — placed there by mistake | Below the bottom slot | 17.56°C | 62.2% | 13.8 | 0.66°C |
| L6 | Floor | Below the bottom slot | 17.78°C | 60.7% | 15.4 | 0.62°C |

- **Shelf 5, the 12.5mm slot, was not measured**
- **Two Govees side by side on the floor differed** by 1.5 RH points, 1.6 in dip and 0.22°C, so
  differences of that size between levels may be the sensors
- Dip grows with slot size where measured: 6mm 10.9, 9mm 15.3. L3 with no slot dipped more than L2
  — discharge mixes across levels. L1 at the fan intake ran warmest
- The boxes on shelves 1–2 dipped less than the trays on 3–4, but slot size explains it equally
  well; the data cannot separate them
- **Top front, the controller's sensor nearest the fan intake, will read return air like L1** —
  reasonable for control; confirm it tracks the other levels

**Average cycle**, stream-weighted, 18 cycles: 62.2% at −3 min → 58.6 at onset → 52.7 at +2 →
**50.7 at +4, compressor stops** → 55.8 at +6 → 60.3 at +9 → 62.2 at +13. More than 3 points below
baseline for ~8 minutes of each 98-minute cycle.

**Humid spell, from midday 29 September:** room moisture 1.383 → 1.580 kPa (+14.2%), room +0.9°C;
cabinet 1.2475 → 1.2550 kPa (+0.59%), levelling within a day. Cycles rose from 14.5 to 18.2 a day,
interval 101 → 78 min. Every level gained about the same moisture, ~+0.35 RH points.

### Boveda position — a fallback lever, not pursued

A pack placed squarely in a discharge stream might humidify the driest air as it leaves the
plenum and soften the dip at that level. Slots are kept clear by design; if the 3–4 point target
proves hard to reach, reposition one shelf's existing pack into its stream, change nothing else,
hold a day, and compare that level's dip against its A2 average.

---

## 4. Open issues

**BP16(b) may fail on Rev P.** On Rev N, plugging USB into a running board restarts it. If the
cause is the DevKit's auto-reset circuit rather than power, D7 won't stop it, and BP16(b)'s "no
reset" fails. If so: accept it and change the pass criterion, or change the board (a capacitor on
EN, or a way to disconnect auto-reset).

**RTC lost ~2h27m before 2026-10-07.** Found at a calibration pre-flight. The cell held the clock
through a deliberate 10-minute unplug, so the cause is unknown — possibly intermittent cell contact
while the board was handled. Run `set_rtc.py --check` after each unplug and note what happened to
the board if it jumps. The boot OSF check and NTP resync cover it in service.

**DS3231 temperature register returns garbage** — 0xCE (−50°C), 0xF6, 0xCA — under automatic
conversion; forced conversions (set CONV, wait for BSY) always read plausibly; BSY set ~6% of the
time against ~0.3% expected. Ruled out: bus errors, framing, bus speed, an open mux channel,
control register changes, the 32kHz output. Timekeeping is unaffected. Firmware never uses it.
---

## 5. Gotchas

- **Floating outputs read stray volts.** With nothing on J10, pin 3 (K1's open NO side) reads ~0.9V on a 10MΩ meter. Load it with 10kΩ and it reads 0V. Probably flux trapped under K1.
- **Mux board (HW-617) pull-ups, measured:** SDA–VIN and SCL–VIN 10kΩ; SD2–VIN and SC2–VIN open. The main bus has 10kΩ on the mux plus 4.7kΩ on the DS3231; the channels have only the probes' own.
- **Female sockets:** probe their solder joints underneath; meter tips don't reach the contacts from above.
- **USB into a running DevKit restarts it,** even with 12V on (POWERON). Never re-plug USB to read a running board; leave it in and open the monitor.
- **Monitor resets the board with RTS/DTR at 0** on this Mac — fixed with 1/1 (spec 5.1). A custom serial script must clear RTS before DTR.
- **ESP32 core error messages can stall the loop.** With the RTC not answering, the core's I²C error print waited behind the boot banner and held a loop pass for 175 ms. The firmware turns them off; the watchdog, panic and reset-reason output are kept. Proved with J9 unplugged while running.
- **Arduino-ESP32 3.3.11 I²C default timeout is 50 ms.** Set it explicitly (the firmware uses 20 ms).
- **The core's task watchdog already exists** (5 s, panic, core 0 idle watched): reconfigure it, don't re-create it, and keep core 0's idle watch.
- **Header names clash with ESP-IDF** (`settings.h`, `types.h`): the firmware's core headers sit in their own `wineador/` folder.
- **iCloud makes "name 2" copies** in a synced folder, including inside `.git` (it broke `git log --all`) and the build folder. The project lives in the home folder, outside iCloud; `workspace_dir = .pio.nosync` as well.
- **pio isn't on the shell path:** `~/.platformio/penv/bin/pio`.
- **J9 lead:** on the first firmware boot the RTC didn't answer until the lead was reseated.
- **ESP32 seating:** after refitting the DevKit, press both rows home. A half-seated right-hand row kills I²C and the SD card together (pins 17–23 are all on that row).

**Parasitic powering of SHT40s.** Connecting SDA and SCL before VCC powers the sensor through its
ESD diodes — phantom ACKs, CRC failures, symptoms like a solder bridge or dead mux. **Power first,
signal second.**

**CMD (GPIO11) sits next to the 5V pin** on the 38-pin devkit and reads ~1.4V while running —
flash activity, not a fault.

**D4184 − to board GND will beep on a continuity test** — Buck B joins them internally. Check by
eye that no wire runs between them.

**Calibration sketches append to existing files.** Delete `/cal_*.csv` and `/cal_*_result.txt`
before a new run.

**Mains test lamp 40W or more** — the SSR's 100mA minimum switching current.