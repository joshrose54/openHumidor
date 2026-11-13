# Wineador — Specification

The design: parts, how it is built, the rules it runs by, and the tests it must pass. Measured
results, the units' own values and anything learned in testing are in the **build log**.

---

# 1. APPLIANCE

**electriQ eiQ201WINEBG** — 201L freestanding wine cooler

| Spec | Value |
|---|---|
| Voltage / frequency | 220–240V, 50Hz |
| Rated current | 0.8A (~190W) |
| Estimated inrush | 3.2–4.8A |
| Refrigerant | R600a, 37g — flammable |
| Capacity | 201L |
| Climate class | SN / N / ST (10–38°C ambient) |
| Energy @ 16°C / 32°C ambient | 0.158 / 0.680 kWh/24hr |
| External | 480 W × 575 D × 1285 H mm, 46.94 kg |
| Setpoint range | 5–20°C |
| Compressor | Fixed speed, binary output |
| Built-in restart delay | 5 min |

**Chamber opening** (stock fan removed): 970 H × ~390 W mm. Top corners R8.55. Bottom meets
compressor hump, square corners. Clear plenum below evaporator plate: ~250mm.

---

# 2. PARTS

## 2.1 Panel

| Item | Spec |
|---|---|
| Panel | 6mm Clear XT acrylic, 380 × 960mm |
| Insulation | Self-adhesive closed-cell PE foam, **2mm**, one layer |
| Depth-stop blocks | Acrylic, T15 × L300 × W50mm, cut to size |
| Sealant | Bostik S31 neutral cure silicone, clear, 310ml |

Closed-cell PE: ~0.039 W/mK @ 10°C, <1% water absorption, not fire retardant. **One 2mm layer.**
The panel is designed to come out if a change is ever needed.

### Panel geometry

Origin bottom-left, mm. Outline 380 × 960, top corners R10, bottom square.

| Feature | From left | From bottom | From top | Size |
|---|---|---|---|---|
| Fan aperture | 190 | 870 | 90 | ⌀76 |
| Fan holes, upper | 154.25 / 225.75 | 905.75 | 54.25 | ⌀4.4 |
| Fan holes, lower | 154.25 / 225.75 | 834.25 | 125.75 | ⌀4.4 |
| Upper slot | 190 | 610 | 350 | 280 × 6 obround, R3 |
| Mid slot | 190 | 320 | 640 | 280 × 9 obround, R4.5 |
| Bottom slot | 190 | 30 | 930 | 280 × 12.5 obround, R6.25 |

Fan hole pitch 71.5mm both axes. Total slot area 7,641mm². Slots are graded to deliver more cold
lower down.

## 2.2 Fans

### Plenum fan

**Noctua NF-A8 5V PWM** ×2 (one fitted, one spare)

| Spec | Value |
|---|---|
| Size / pitch | 80 × 80 × 25mm / 71.5mm |
| Voltage | **5V only** |
| Current | 0.15A, 0.75W |
| Speed | 400 RPM @ 20% PWM to 2,200 RPM (Noctua) |
| Airflow / pressure | 55.5 m³/h / 2.37 mm H₂O |
| PWM logic | High 3.3V or 5V, low below 0.8V |
| PWM frequency | 21–28kHz; **25kHz used** |
| Tach | Open-collector, two pulses per revolution, no internal pull-up |
| Bearing | SSO2, MTTF >150,000 hrs |

Duty below 20% undefined. No PWM signal → full speed. Power from the board's 5V rail, never a GPIO.
Conformal-coat the hub PCB. Integrated reverse-polarity diode.

**Never connect 12V or 24V to the PWM pin** — it destroys the fan. **The appliance's stock fan
header is 12V; the 5V NF-A8 must never be connected to it.**

### Circulation blowers

**12V blower ×4** — GDSTIME GDB5015, 50 × 15mm, 4,500 RPM, 0.12A each, 2-wire, hydraulic bearing
(one spare). Mounted on the cabinet side of the acrylic panel at four heights, blowing across the
shelves; **intake open to the cabinet, no opening through the panel** — they recirculate cabinet air
only, so the plenum stays sealed apart from the slots and the Noctua alone sets the airflow across
the plate. Run 24/7 from the board's 12V rail through J12–J15 (3.3). Not controlled or monitored by
the firmware on Rev P. Rev Q adds voltage speed control, full speed by default (3A.6).
Sensors sit out of their streams (B5).

## 2.3 Sensors

**SHT40 ×6** — sealed probe, 2m cable, bare wire ends. Five in the cabinet on channels 0–4, one
ambient on channel 5.

| Spec | Value |
|---|---|
| Supply | 1.08–3.6V. **Never 5V** |
| Accuracy | ±0.2°C (0–65°C), ±1.8% RH (30–70% @ 25°C) |
| Range | −40 to 125°C, 0–100% RH |
| Response time | 6s (tau63%) |
| Long-term drift | <0.03°C/yr, <0.2% RH/yr |
| Interface | I²C 0x44, serial via command 0x89, CRC per reading |
| Wiring | Red VCC · Black GND · Yellow SCL · Green SDA |
| Pull-ups | 10kΩ fitted inside each probe |

**TCA9548A** — 8-channel I²C mux, 0x70; on Rev P the board HW-617 (Rev Q: on the board, 3A.7). **Power VIN from 3.3V, never 5V** — a
5V-powered mux would reference channel logic to 5V and destroy all six sensors. Module's own 10kΩ
pull-up on RST retained.

### I²C pull-ups

Rev P (Rev Q: 3A.7):

| Bus segment | Existing | Action |
|---|---|---|
| Master (ESP32 ↔ mux + RTC) | DS3231 carries 4.7kΩ; the mux board carries 10kΩ (≈3.2kΩ together) | Fit nothing |
| Each mux channel (2–7) | 10kΩ inside each SHT40 probe; none on the mux board | Fit nothing |

Bus **25kHz**: 40µs bit period against ~1.7µs rise time on a 2m run. Fallback if a channel proves
unreliable: 4.7kΩ in parallel on that channel only. Do not fit pre-emptively.

### Channel roles

| Channel | Position | Where | Role |
|---|---|---|---|
| 0 | Top front | Top level, front half — nearer the door | Control |
| 1 | Bottom front | Bottom level, front half | Control |
| 2 | Mid front | Middle level, front half | Control |
| 3 | Mid rear | Middle level, rear half — nearer the plenum wall | Control |
| 4 | Chamber | In the plenum, halfway down, near but not touching the cooling plate | Safety limit and harvest termination — never control |
| 5 | Ambient | Outside the cabinet, away from the fridge's warm parts and the controller enclosure — see below | Dosing feedforward, analysis, disturbance direction. Never control |

Cabinet sensors (ch 0–3) sit **out of the plenum discharge and blower streams**, at least ~10cm from
the door and from the plenum wall, about 20mm off the side liner in their printed mounts, each with a baffled shroud to deflect
air streams off the probe.

**Channels 0–5** are the numbers used everywhere — control rules, firmware, logs, settings, tools.
Rev Q wires channel n to mux channel n (3A.7). Rev P wires them to mux channels 2–7 (3.3), so on
Rev N/P the board settings map them; earlier B1–B4 records use Rev P's mux numbers.

**Ambient sensor placement:** true room air at roughly cabinet height; not near the back or bottom of
the fridge (condenser and compressor are warm); not inside the controller enclosure; away from
sunlight, radiators and draughts. Its failure never stops control — dosing falls back to feedback
alone and the failure is logged.

### Calibration

**Required, two points:** saturated table salt (75.5%) and saturated magnesium chloride (33.1%),
all six sensors sealed above each reference in turn and read by the ESP32. Each sensor gets a
straight-line correction, **RH = gain × raw + offset**, applied in firmware per serial. The
procedure is B1.2 test 13; the coefficients are in the build log.

| Reference | 15°C | 20°C | 25°C |
|---|---|---|---|
| Table salt, NaCl | 75.6% | 75.5% | 75.3% |
| Magnesium chloride, MgCl₂ | 33.3% | 33.1% | 32.8% |

Two points because the sensors' errors change across the humidity range; two points bracket the
cabinet's 60–70% and capture that change. No temperature reference is used; temperatures are
uncorrected.

## 2.4 Controller

**This section and 3.1–3.3 describe Rev P, the bench design** (built as Rev N; B3 and B4 ran on it).
**The cabinet board is Rev Q — chapter 3A.** Items here that Rev Q replaces: the DevKit, terminal
adapter, SD module, HW-084 RTC and D7.

| Item | Spec |
|---|---|
| ESP32-WROOM-32 DevKit ×3 | 38-pin, USB-C, 4MB flash, WiFi + BT. **Must have a diode between USB and its 5V pin** — check each unit before use (3.3). **Every unit modified before use: two pull-ups (3.3)** |
| Terminal adapter | 38-pin narrow-hole, no mounting holes |
| SD module | 3.3V SPI |
| SD card | 64GB high-endurance, FAT32, **MBR partition table** |
| DS3231 RTC | HW-084. I²C 0x68 clock, 0x57 EEPROM. LIR2032 cell |
| PSU | **Mean Well IRM-60-12ST** — 12V 5A encapsulated, screw terminals, inside the enclosure. Mains design pending (4.10) |
| On-board 5V | Traco **TSR 3-2450N** switching regulator, 12V → 5V 3A, on the PCB (U8) |
| Controller PCB | **Rev P**, 108.7 × 96.0mm, 4-layer — the bench board. All 12V loads switched on the board; full parts list in the board notes. **Rev Q** for the cabinet (3A) |
| Enclosure, glands | Minimum ~200 × 150 × 80mm. Carries a BS 1363 socket for the fridge (Pro Elec 9143) and an M16 gland (Multicomp PP001702) for the mains lead |
| Mains joints | Genuine WAGO 221 lever connectors (450V) for the live, neutral and earth splits; insulated ring terminals at the SSR, hole to suit its screws |
| Mains lead | 13A plug, 3-core flex 1.0mm² or more, **5A BS 1362 fuse** (the SSR's 5A AC-53 motor rating), into the enclosure through the M16 gland |
| Extension lead | 1-gang, 13A, cut to insert the SSR — the **bench harness** (4.10), 5A plug fuse |

### On-board power

12V enters on **J1** (XT30, keyed; pin 1 / square pad = +12V, pin 2 = GND) through the 4A inline
fuse. Everything else is on the board:

| Ref | Part | Job |
|---|---|---|
| Q1 | IRF4905 P-channel MOSFET, high side | Reverse-polarity protection for the whole 12V rail. D3 (1N4744A 15V zener) protects its gate; R8 (10kΩ) turns it on |
| D2 | P6KE20A TVS | Clamps spikes on the 12V rail below ~28V |
| D1 | 1N5822 Schottky | Feeds U8 only. Stops anything on the 5V side back-feeding the 12V rail |
| U8 | TSR 3-2450N | 12V → 5V, 3A. C1 47µF (polarised, + to U8 input) at its input; C4 10µF and C2 100nF at its output |
| D7 | 1N5822 Schottky | Board 5V rail → the ESP32's 5V pin. USB can never power the board's 5V rail |
| TP6 | Test point | +12V, after Q1 |

**U8 orientation:** go by its pins, not the printed face. The pin row is 1.44mm from one face and
6.16mm from the other; fit it so the body sits inside its silkscreen outline — the face the pins
are close to towards the sensor connectors (right), the bulk of the body towards C1 (left). The
other way round puts the body 5mm outside the outline and **12V onto the 5V rail**.

**USB and 12V in any order.** D7 stops USB powering the board; the DevKit's own diode between USB
and its 5V pin stops the board powering the host. So USB can be connected or removed, and 12V
switched on or off, in any order, and **the ESP32 is flashed on the board**. With USB in and 12V
off, only the ESP32 and the 3.3V parts run; U9, the fan and every load are unpowered and R13–R16
hold every MOSFET off.

**3.3V margin.** D7 drops the DevKit's 5V pin to ~4.6–4.7V. The fitted DevKit's AMS1117-3.3
regulator needs ~1.0–1.3V of headroom, so under a WiFi burst its 3.3V rail may dip towards 3.2V —
inside the ESP32's 3.0–3.6V rating. Proven at BP; **pass is TP2 ≥ 3.1V under load**. If it fails,
D7 becomes a lower-drop DO-201AD Schottky (1N5820 or SB520), same footprint. A different DevKit
must have a low-dropout regulator or be checked the same way. The DevKit diode check (3.3) still
applies to every unit fitted.

### Power budget

| Rail | Load | Current |
|---|---|---|
| 5V | ESP32, WiFi TX peak | 250mA |
| 5V | Noctua NF-A8 | 150mA |
| 5V | SD card, write peak | 100mA |
| 5V | Mux, RTC, sensors, tach pull-up, module LEDs, U9 | ~25mA |
| **5V total** | | **~525mA peak, from U8 (3A)** |
| 12V | Heater pad, 20W | 1.67A |
| 12V | Circulation blowers, 4 × 0.12A, continuous | 480mA |
| 12V | Humidifier fan | 120mA |
| 12V | U8 input | ~250mA |
| 12V | K1 coil | 14mA |
| 12V | SSR control input | ~12mA |
| **12V total** | | **~2.55A peak** — see Fusing |

### Fusing

| Circuit | Protection |
|---|---|
| Mains to SSR and fridge | **5A fuse in the extension plug** — protects the switched lead and SSR wiring. The fridge's own plug fuse stays in series. Neither protects the SSR itself from a dead short; that would need an ultra-fast semiconductor fuse, not fitted |
| Mains to PSU | Internal to the plug-top PSU |
| **12V** | **4A blade fuse, inline between the PSU and J1, off the board** — protects the PSU lead and the board. The 12V and heater tracks are 1.5mm (~3.2A continuous) |
| 5V rail | U8's own over-current protection |
| Reverse polarity | Q1; J1 is keyed as well |
| Surge | D2 |

### RTC battery

Rev P (Rev Q's charging cell: 3A.8).

**The HW-084's charger is inert at 3.3V** — a 200Ω resistor and diode from VCC leave under 3V at
the cell, below a LIR2032's 3.6V nominal, so no current flows. **The LIR2032 stays and is never
recharged**; it runs on its factory charge, only powers the clock through a power cut, and the
charger does it no harm. Never power the module at 5V — its pull-ups would put 5V on the I²C bus.
When the cell goes flat the boot-time OSF check in 5.6 catches it; **replace the cell when the
warning appears.**

### Interlock relay

**K1 — Finder 34.51.7.012.5010**, on the board

| Spec | Value |
|---|---|
| Coil | 12V, 840Ω, 14mA, from the 12V rail; switched by Q5, D6 flyback |
| Contacts | 1 × changeover, gold-plated, 6A |
| Operate / release | ≤5ms / ≤3ms; bounce ≤1ms (NO), ≤6ms (NC) |
| Action | Non-latching; energised = heating |

**It switches the 12V feed, not the control signals**: COM is +12V, NC feeds the SSR control input
(+A1), NO feeds the heater (+). Heat and cool can't both be powered, even if a MOSFET fails shorted.
Gold contacts suit the SSR's ~12mA. Exercise the relay weekly, three full cycles with both loads
off, and the heater/cooling-ineffective checks in 5.6 reveal a bad contact.

### Load switching

| Ref | Part | Switches | From |
|---|---|---|---|
| Q2 | IRLZ44N, low side | SSR control −A2 | P26 |
| Q3 | IRLZ44N, low side | Heater − | P27 |
| Q4 | IRLZ44N, low side | Humidifier fan − | P33 |
| Q5 | IRLZ44N, low side | K1 coil | P32 |

**U9, SN74HCT08** (5V, in a DIP-14 socket) turns the 3.3V GPIO signals into ~4.4V gate drive; each
gate's spare input is tied to 5V; C5 100nF decouples it. Per channel: 1kΩ series from the GPIO to
U9's input (R17 P26, R18 P27, R19 P33, R20 P32 — limits the current into U9 to ~3mA if a GPIO is
driven high while U9 is unpowered), 100Ω gate resistor (R9–R12), 100kΩ gate pull-down (R13–R16 —
holds the MOSFET off if U9 is unpowered or missing). Flyback diodes 1N5819: D4 across the heater,
D5 across the humidifier fan, D6 across the relay coil. IRLZ44N on-resistance ≤35mΩ at 4V gate:
~0.1W in the heater switch, no heatsinks; the tabs sit on bare board and connect to nothing.

### Passive components on the board

| Part | Value | Where | Status |
|---|---|---|---|
| Tach pull-up | **1kΩ** | P34 to 3.3V | Fitted |
| Tach filter | **1µF ceramic, non-polarised**, 10V+, radial | P34 to GND, close to the ESP32 | Fitted |
| Boot pull-downs | 10kΩ ×4 (R1, R2, R3, R7) | P26, P27, P32, P33 to GND | PCB |
| U9 series | 1kΩ ×4 (R17–R20) | GPIO to U9 input | PCB |
| Gate resistors / pull-downs | 100Ω ×4 (R9–R12) / 100kΩ ×4 (R13–R16) | U9 output to gate / gate to GND | PCB |
| Q1 gate | 10kΩ (R8), 15V zener (D3) | Q1 gate to GND / gate to source | PCB |
| U8 input | **C1 47µF 50V polarised** — + to U8's input; safe because Q1 and D1 mean it can never see reverse voltage | U8 Vin | PCB |
| U8 output | C4 10µF ceramic, C2 100nF | U8 Vout | PCB |
| U9 decoupling | C5 100nF | U9 VCC | PCB |

Tach filter: the pull-up and capacitor are a pair with a 1ms time constant; change one and the
other must change with it. 10kΩ with 1µF breaks the tach above ~1,080 RPM. Dielectric not
critical.

## 2.5 Mains switching

**Finder 77.A1.9.024.8250** SSR — Farnell 4703602

| Spec | Value |
|---|---|
| Control input | 3–32V DC, nominal 24V; 25mA at 24V |
| Must drop-out voltage | 1.25V |
| Output | 25A, 24–280V AC, SPST-NO, zero-crossing |
| Max peak current, 10ms | 300A |
| **Minimum switching current** | **100mA @ 250V** |
| Off-state leakage | 0.1mA at rated voltage |
| On-state drop at 25A | 1.5V |
| Operate / release | 10ms / 10ms |
| dv/dt without input control | 500 V/µs |
| Max switching frequency | 1800 cycles/hour |
| Terminals | Screw, plate clamp — DIN 46234 ring terminals intended. Input 1.5Nm, output 2.2Nm |
| Protection / ambient | IP20, −30 to +80°C |

- **Control input fed from the 12V rail** through K1's NC contact (+A1) and switched by Q2 (−A2), so no GPIO supplies the SSR's current (~12mA at 12V)
- **No heatsink**: ~1.2W at 0.8A
- **Minimum switching current 100mA**: the fridge at 0.8A is fine; **the mains test lamp must be
  40W or more** (174mA; 25W would sit on the threshold at 109mA)
- The ordered part is DC input; distributor photos often show the AC-input 77.A1.8.230.8650

**X2 capacitor — Panasonic ECQUAAF104SA**, 0.1µF 275V AC, metallized PP, 15mm pitch. Fitted
permanently across the fridge to give off-state leakage a path around the fridge's electronics.

**MOV — Bourns MOV-14D431K** ×2 fitted (×3 bought). 275V RMS, 430V @ 1mA, clamps 710V @ 50A, 14mm
disc, UL 1449.

### SSR protection — both MOVs mandatory

| Part | Across | Job |
|---|---|---|
| **MOV 1** | SSR L13 and SSR LOAD | Mains surges — an off SSR blocks a surge, so it appears across the SSR; the SSR is rated 600V peak |
| **MOV 2** | SSR LOAD and neutral — across the fridge | The compressor's own switch-off spike |
| X2 | SSR LOAD and neutral | Leakage path around the fridge's electronics |

The SSR has no built-in snubber or varistor. **No external snubber is fitted**: one would leak
several milliamps through the fridge when off, the flicker problem this SSR was chosen to avoid.
Back-to-back thyristors handle motor loads well and Finder rates it for 5A AC53. **If the SSR
ever switches the fridge on by itself, in B2 or B5, revisit this.**

## 2.6 Heating

**Silicone heater pad** — 12V, 20W, 200 × 150mm, 0.67 W/1000mm². Free-air surface temperature at
100%: see build log B2.6.
Suspended in the plenum below the evaporator plate. Wire exit sealed with neutral-cure silicone,
all joins outside the cabinet.

**Switching** is on the board (2.4, Load switching). Each 12V load sits between a 12V feed and a
low-side IRLZ44N:

| Load | Feed | Switch | Signal | Drive |
|---|---|---|---|---|
| Heater pad, through the KSD9700 | K1 NO (energised) → J10 pin 3 | Q3 → J10 pin 4 | P27 | PWM 1–10Hz |
| SSR control input +A1/−A2 | K1 NC (de-energised) → J10 pin 1 | Q2 → J10 pin 2 | P26 | On/off |
| Humidifier fan | +12V → J11 pin 1 | Q4 → J11 pin 2 | P33 | On/off |

**KSD9700 thermal cut-out** — normally closed, 250V 5A, auto-reset. **The 35°C unit is fitted**,
in series with the heater's 12V feed, in the airstream away from the element, sensing plenum air.
Measured trip and reset in the build log; the chamber ceiling in 4.4 is set from them.

## 2.6a Humidification

**Active evaporative humidifier** — own design, on the cabinet floor. Tank ~500ml, wick, hood
turning the airflow down across the wick. **12V blower, 50 × 15mm, 4,500 RPM, 0.12A, 2-wire** — on/off via
Q4 on J11; no tach, no PWM. No Boveda.

| Item | Rule |
|---|---|
| Water | Distilled only, with a humidor biocide. A propylene-glycol solution also self-limits evaporation near ~70% RH |
| Condensate | The water trap under the plate feeds the tank; **the stock drain is blocked**. Stripping stores water rather than losing it |
| Overflow | None by design — the sealed cabinet gains almost no net water. **Max fill line; check the level monthly** |
| Fail-safe | Off. P33 pulled down; every guard in 4.4 and 4.7b stops it |

## 2.7 Wiring and hardware

### Cable

| Item | Spec | Purpose |
|---|---|---|
| 4-core 24AWG shielded | 5m | Sensor or fan extensions, only where 2m does not reach |
| Cat5e | offcuts | Alternative for I²C runs |
| Silicone 24AWG, assorted | few metres | Signal wiring |
| **Silicone 18AWG, red and black** | few metres | **12V feed and heater circuit** |

### Gauge by circuit

| Circuit | Current | Gauge |
|---|---|---|
| Sensors, I²C | Milliamps | 24AWG |
| Fan | 150mA | 24AWG |
| SSR control, J10 pins 1–2 | ~12mA | 18AWG with ferrules (the block takes 30–12AWG) |
| Circulation blowers, one lead each to J12–J15 | 120mA each | The fans' own leads, 28–22AWG, crimped XH plugs |
| Humidifier fan to J11 | 120mA | The fan's own leads, crimped XH plug |
| **PSU → fuse → J1** | **2.55A** | **18AWG**, XT30U-F at the board end |
| **J10 pins 3–4 → KSD9700 → pad** | **1.67A** | **18AWG with ferrules** |
| Mains | 0.8A | Extension lead flex, 1.0mm² minimum |

### Sensor wiring

**No trunk.** Each sensor is its own branch to its own mux channel, through its own four-pin
terminal on the board — VCC, GND, SCL, SDA, in the probe's wire order. All six VCC join the 3.3V
rail and all six GND join ground **on the board**. SDA and SCL are individual to each channel and
must never be joined together.

### Consumables

Heat shrink · ferrules and crimper · WAGO levers — genuine WAGO 221 only, also used on mains ·
insulated ring terminals, red and blue · cable glands · **JST XH 2.5mm kit** — 4-way housings,
XH crimp contacts SXH-001T-P0.6 (22–28AWG), **2-way housings** for the five 12V fans, and an
XH-capable crimper, for every lead into the PCB (3.3) · XT30U-F female plug for the 12V input lead
(J1) · ferrules for the 18AWG SSR and heater leads into J10.

---

# 3. ELECTRICAL DESIGN

## 3.1 Pin allocation

Rev P (Rev Q: 3A.4).

**Restrictions:** GPIO 34, 35, 36, 39 input only. GPIO 6–11 internal flash, unusable. GPIO 0, 2, 5,
12, 15 strapping pins.

| Function | Pin | Notes |
|---|---|---|
| I²C SDA / SCL | P21 / P22 | Mux and RTC |
| Fan PWM | P25 | 25kHz |
| Fan tach | P34 | Input only. External 1kΩ pull-up **and** `pinMode(34, INPUT)` |
| SD CS / SCK / MISO / MOSI | P17 / P18 / P19 / P23 | CS moved off strapping pin GPIO5 |
| Cooling (SSR) | P26 | U9 → Q2. Live only when K1 is de-energised |
| Heater PWM | P27 | U9 → Q3. Live only when K1 is energised |
| Interlock relay K1 | P32 | U9 → Q5 |
| **I²C mux reset** | **P16** | To TCA9548A RST, active low. Free on WROOM-32 (PSRAM only on WROVER) |
| **Humidifier fan** | **P33** | U9 → Q4. Not through the interlock. Software-guarded only |
| Spare | **Output-capable (input/output):** P4, P13, P14. P14 is reported by some sources to pulse at boot; fit a pull-down if it ever drives a load. **Input only:** P35, SVP, SVN. P5 only for something that cannot pull it low at power-on | |

The four load signals go through U9 (74HCT08) to the MOSFET gates (2.4); the fan PWM, mux reset and buses drive directly from 3.3V GPIO.

### Supply voltage

**Every I²C device runs at 3.3V** — pull-ups reference their own VCC, so a 5V-powered module would
put 5V on SDA and SCL and destroy the ESP32 and all six sensors.

| Device | Supply |
|---|---|
| TCA9548A | 3.3V — board accepts 5V, do not use it |
| DS3231 | 3.3V — board accepts 3.3–5V, do not use 5V |
| SHT40 ×6 | 3.3V — absolute maximum 3.6V |

The ESP32's onboard regulator supplies the 3.3V rail: ~20mA plus SD write peaks of ~100mA.

### P34 tach — rules

- **1kΩ pull-up to 3.3V and 1µF to GND**, per Noctua's microcontroller guide. GPIO34–39 have no
  internal pull-ups and the tach is open-collector. A weak pull-up lets the 25kHz PWM running
  alongside the tach in the fan cable couple noise onto it
- **Always to 3.3V, never 5V** — 5V exceeds the pin limit
- **`pinMode(34, INPUT)` is mandatory** — the input buffer is off by default and `digitalRead`
  returns 0 regardless of pin voltage. Without it the tach reads zero and compressor and heater are held off
- **Count pulses by width, not edges.** The filtered edges are slow and GPIO34 has no hysteresis,
  so the input chatters on every transition. A pulse counts only when the line has stayed low for
  ≥1ms. See 5.3

### P16 mux reset

Pulsed low, RST resets the mux and deselects every channel. A sensor's SDA or SCL shorting to
ground — plausible on a 2m cable in a damp cabinet — holds the whole main bus low when its channel
is selected, and **an ESP32 reset does not clear it**: the mux keeps its selection. P16 is the only
recovery short of cutting power. The module's 10kΩ pull-up holds RST high while P16 is undriven
during boot.

### SD chip select on P17

GPIO5 is a strapping pin that must be high at boot and some boards clock it during startup. P17
has no strapping role; SCK, MISO and MOSI stay on the VSPI defaults.

### Boot-state safety

Every GPIO floats as an input for ~300ms from power-on until firmware configures it.

| Pin | Load | Mitigation |
|---|---|---|
| P26 | SSR, via Q2 | 10kΩ pull-down (R1); the gate's own 100kΩ pull-down behind it |
| P27 | Heater, via Q3 | 10kΩ pull-down (R2); gate pull-down behind it |
| P32 | Interlock relay K1, via Q5 | 10kΩ pull-down (R3) — floating = de-energised = cooling, which is safe, but made deterministic |
| P33 | Humidifier fan, via Q4 | 10kΩ pull-down (R7) — the humidifier must never start on a floating pin |
| P16 | Mux RST | None — the module's pull-up holds RST high |
| P25 | Fan PWM | None — undriven = full speed, which is safe. The fan's own pull-up is ~85kΩ to 3.85V, so only microamps reach the pin. **No series resistor** |

**Verify boot state on the bench with a meter before connecting any load.**

### I²C addresses

| Device | Address |
|---|---|
| TCA9548A | 0x70 |
| DS3231 clock | 0x68 |
| DS3231 EEPROM | 0x57 |
| SHT40 | 0x44, one per mux channel 2–7 |

## 3.2 Heat/cool interlock

K1 sits in the **12V feed** to the two loads, not in the control signals.

| Relay state | 12V path | Result |
|---|---|---|
| De-energised, P32 low | COM → NC → J10 pin 1 → SSR +A1 | Cooling can be powered (Q2 on P26); heater feed open |
| Energised, P32 high | COM → NO → J10 pin 3 → KSD9700 → heater + | Heating can be powered (Q3 on P27); SSR feed open |

De-energised selects cooling, so a dead or unpowered controller can only cool. Both loads can be
off in either position, and a shorted MOSFET cannot defeat the interlock because the other load
has no feed.

**Switching sequence:** drop the load signal, change the interlock, allow **20ms** settle (K1
operates in ≤5ms, releases in ≤3ms, NC bounce up to 6ms), assert the new load signal.

## 3.3 Master connection list

Terminal adapter labels for the ESP32 side.

### Board connectors

Logic leads leave the board through **JST XH 2.5mm 4-way sockets** (B4B-XH-A, vertical); the five
12V fans through **XH 2-way sockets** (B2B-XH-A); the SSR and heater through **J10**, a 4-way
5.08mm screw block; 12V in through **J1**, an XT30. **Pin order matches wire order** so a
connector cannot be wired crossed; the XH and XT30 plugs are keyed. Each lead's stock connector is
cut off and an XH housing crimped on — a crimped lead cannot be pulled off by the compressor's
vibration.

| Connector | Ref | Pins | Signals, in pin order |
|---|---|---|---|
| 12V in: XT30PW-M, PCB right-angle, male | J1 | 2 | **+12V**, GND. The PSU lead (via the 4A fuse) on an XT30U-F plug, square pad = + |
| Fan — XH | J2 | 4 | GND, 5V, tach (P34), PWM (P25) |
| Sensor, channels 0–5 (mux 2–7) — six, XH | J3–J8 | 4 | 3.3V, GND, SCL, SDA |
| RTC — XH, back face | J9 | 4 | SCL, SDA, 3.3V, GND — a short crimped lead to the HW-084, not jumpers |
| SSR and heater — 5.08mm screw block, Multicomp MP012012 | J10 | 4 | 1 SSR +A1 (from K1 NC) · 2 SSR −A2 (to Q2) · 3 heater + (from K1 NO) · 4 heater − (to Q3). Counted from the top of the board; wire openings face the left edge, screws on top. 18AWG with ferrules |
| Humidifier fan — XH 2-way | J11 | 2 | 1 +12V · 2 − (to Q4) |
| Blowers 1–4 — XH 2-way | J12–J15 | 2 | 1 +12V · 2 GND. Always on |

Silk beside J10 reads SSR + / − and HEATER + / −; beside J11–J15, HUMID and BLOWER 1–4 with +
and −. Pin 1 is the square pad (top) on every XH socket.

**12V is on the board.** It carries 12V from J1 (after Q1), 5V from U8 and 3.3V from the ESP32's
regulator. The 3.3V rail needs no added decoupling.

### Power distribution — one ground

| From | To |
|---|---|
| PSU + | 4A inline fuse, off the board |
| 4A fuse | J1 pin 1 (+12V in) |
| PSU − | J1 pin 2 (GND) |
| J1 pin 1 | Q1 → **+12V rail** (TP6): K1 COM, J11–J15 pin 1, D1 → U8 |
| U8 Vout | **5V rail** (TP1): fan, U9, D7 |
| D7 | ESP32 5V / VIN — the ESP32's only supply from the board |
| ESP32 3V3 | 3.3V rail (TP2) |
| ESP32 GND | Board GND |
| ESP32 USB-C | Serial and flashing, any time, 12V on or off (2.4) |

All load returns go through the board's ground planes to J1 pin 2. The old two-ground rule and the
off-board 12V WAGOs are gone; the mains WAGOs (4.10) are unchanged.

**Diode check, for every DevKit before it goes on the board:** off the board, USB from the Mac
only, meter its 5V pin to GND. **4.4–4.8V** — diode present, fit it. **4.9V or more** — no diode,
never fit it: the board would drive the Mac's USB port.

**Pull-up modification, for every DevKit before it goes on the board:** solder **2.2kΩ from 3V3 to
EN** and **2.2kΩ from 3V3 to P0 (GPIO0)**, on the top side, flat, the pins underneath left clean so it
still seats. Without them the DevKit's USB chip, unpowered when USB is absent, holds EN low (the
ESP32 never starts) and GPIO0 low (it boots into flashing mode): the board cannot run without USB.
Check off the board on 5V alone into the 5V pin, no USB: **EN and P0 each ≥ 3.0V**. Auto-reset
flashing still works with both fitted. Applies to Rev N and Rev P.

### I²C bus

| From | To |
|---|---|
| ESP32 P21 | TCA9548A SDA, DS3231 SDA |
| ESP32 P22 | TCA9548A SCL, DS3231 SCL |
| 3.3V rail | TCA9548A VIN, DS3231 VCC |
| Board GND | TCA9548A GND, DS3231 GND |
| ESP32 P16 | TCA9548A RST |

TCA9548A A0–A2 unconnected. DS3231 32K and SQW unused.

### Sensors

| Channel | Position | Red | Black | Yellow | Green |
|---|---|---|---|---|---|
| 0 (mux 2) | Top front | 3.3V rail | Board GND | Mux SC2 | Mux SD2 |
| 1 (mux 3) | Bottom front | 3.3V rail | Board GND | Mux SC3 | Mux SD3 |
| 2 (mux 4) | Mid front | 3.3V rail | Board GND | Mux SC4 | Mux SD4 |
| 3 (mux 5) | Mid rear | 3.3V rail | Board GND | Mux SC5 | Mux SD5 |
| 4 (mux 6) | Chamber | 3.3V rail | Board GND | Mux SC6 | Mux SD6 |
| 5 (mux 7) | Ambient | 3.3V rail | Board GND | Mux SC7 | Mux SD7 |

### SD card module

Pin order on the module: 3v3, CS, MOSI, CLK, MISO, GND.

| Module pin | To |
|---|---|
| 3v3 | 3.3V rail |
| CS | ESP32 P17 |
| MOSI | ESP32 P23 |
| CLK | ESP32 P18 |
| MISO | ESP32 P19 |
| GND | Board GND |

### Fan

| Pin | Colour | To |
|---|---|---|
| 1 | Black | Board GND |
| 2 | Yellow | 5V rail (U8) |
| 3 | Green | ESP32 P34, tach |
| 4 | Blue | ESP32 P25, PWM |

Plus 1kΩ P34 to 3.3V and 1µF P34 to GND. The PWM wire needs no external pull-up; the fan pulls it
up internally to the input's level.

### Interlock relay K1

| K1 pin | To |
|---|---|
| A1 | +12V |
| A2 | Q5 drain (P32 via U9); D6 across the coil |
| 11 COM | +12V |
| 12 NC | J10 pin 1 — SSR +A1 |
| 14 NO | J10 pin 3 — heater + |

### Heater

| From | To |
|---|---|
| J10 pin 3 (K1 NO) | KSD9700 terminal 1 — 18AWG |
| KSD9700 terminal 2 | Heater pad lead 1 |
| Heater pad lead 2 | J10 pin 4 (Q3 drain) — 18AWG |

D4 across J10 pins 3–4 on the board.

### Humidifier

| From | To |
|---|---|
| J11 pin 1 (+12V) | Humidifier fan + (red) |
| Humidifier fan − (black) | J11 pin 2 (Q4 drain) |

D5 across J11 on the board.

### Circulation blowers

| From | To |
|---|---|
| J12–J15 pin 1 (+12V) | Each blower + (red) |
| J12–J15 pin 2 (GND) | Each blower − (black) |

One blower per socket; always on.

### SSR and mains

Terminals as marked on the module — control on one side, mains on the other:

| SSR terminal | Side | Function |
|---|---|---|
| **+A1** | Control | DC control + |
| **−A2** | Control | DC control − |
| **L13** | Mains | Live in, from supply |
| **LOAD** | Mains | Live out, to the fridge |

| From | To |
|---|---|
| J10 pin 1 (K1 NC) | SSR +A1 — 18AWG, ring terminal at the SSR |
| SSR −A2 | J10 pin 2 (Q2 drain) — 18AWG |

The extension lead is cut through. **PLUG LEAD** = the length with the plug; **SOCKET LEAD** = the
length with the socket.

| Joint | Conductors |
|---|---|
| SSR L13 — one ring terminal | PLUG LEAD brown + MOV 1 leg |
| SSR LOAD — ring 1 | SOCKET LEAD brown + other MOV 1 leg |
| SSR LOAD — ring 2, stacked on ring 1 | MOV 2 leg + X2 leg |
| Neutral WAGO, 4- or 5-way | PLUG LEAD blue, SOCKET LEAD blue, other MOV 2 leg, other X2 leg |
| Earth WAGO, 2- or 3-way | PLUG LEAD green/yellow, SOCKET LEAD green/yellow |

**No more than two conductors in any crimp barrel or SSR clamp.** Barrel size by total: flex +
component leg → blue; two component legs → red. Fold thin legs back on themselves before crimping.
Pull-test every crimp. Component legs heatshrunk to the barrel or WAGO; WAGO strip length 11mm.
MOV and X2 legs may be extended with **18 AWG stranded, mains-rated (600V silicone or 300/500V)**
lead wire: leg and wire twisted or wrapped first, then soldered, two layers of heatshrink. Never
tin wire ends that go into a WAGO or crimp — solder creeps under clamping pressure.

The SSR goes into a 1-gang extension lead, not the fridge flex; the fridge plugs into the
extension socket unmodified. Live is broken by the SSR; neutral never touches it. **Earth is cut
and rejoined in its own WAGO, and must be continuous plug to socket** — the fridge's earth depends
on it. Label the socket "wineador only": it switches by itself.

The bench lamp tests use this same lead: the lamp, fitted with a plug, goes into the switched
socket in place of the fridge.

---

# 3A. CONTROLLER — REV Q

**Rev Q is the board that goes in the cabinet** (B5 onwards). Sections 2.4 and 3.1–3.3 describe
**Rev P**, the bench design (built as Rev N), on which B3 and B4 were run. Rev Q does the same job
from the same 12V input with the same heat/cool interlock; this chapter is everything that differs.
Every part and every connection is in the Rev Q board notes (`6-revQ-design-report.md`, revision 15,
218 parts, 134 nets, rules check clean). Layout, outline and heights: 3A.12; the full placement and routing records are
in the layout folder (`7-revQ-placement.md`, `8-revQ-routing-setup.md`, `9-revQ-routing.md`).

## 3A.1 What changes from Rev P

- **ESP32-S3-WROOM-1-N8R2 module on the board**, flashed over its own USB. No DevKit, no
  USB-serial chip, no auto-reset circuit.
- **I²C mux, RTC and micro-SD socket on the board** as chips, not modules.
- **LCD** (lcdwiki MSP4031, 4.0" ST7796, capacitive touch) on a 14-way FPC.
- **Three separate 3.3V regulators**; a **supervisor with hardware watchdog** on EN; **hold-up
  capacitors** behind D1.
- **Blower speed control** and an **ion generator output** (new).
- Mostly SMD, assembled by JLCPCB on the top side; a short list of through-hole parts hand-soldered,
  also on top. **Every component on the top side.** Four layers.

**Unchanged from Rev P:** the 12V input path (XT30, 4A inline fuse off the board, Q1 IRF4905
reverse protection with its zener and 10kΩ), U8 (TSR 3-2450N), K1 and the interlock, the 74HCT08
gating of the four original loads with their pull-downs, series resistors, gate resistors and gate
pull-downs, the plenum fan circuit, J10's pin-out, the sensor connector pin-out and TP1–TP7.

## 3A.2 Parts

| Ref | Part | Job |
|---|---|---|
| U1 | **ESP32-S3-WROOM-1-N8R2** (JLCPCB C2913204) | 8MB flash, 2MB quad PSRAM on internal pins. **Never substitute any R8 or R16V variant** (e.g. N8R8, N16R8, N16R16VA) — their octal PSRAM takes GPIO35–37, used here for I²C0 and the mux reset, and the R16V runs GPIO47/48 (touch interrupt, display reset) at 1.8V |
| U2, U3, U4 | ST LDL1117S33R, 3.3V 1.2A, SOT-223 | 3V3_RF, 3V3_Q, 3V3_SENS (3A.3) |
| U5 | **Maxim / Analog Devices MAX823SEUK+T** (JLCPCB C239902) | Supervisor and hardware watchdog on EN (3A.5). **Never a TI TPS3820/3823/3824/3828 (non-A)**: toggling WDI while they assert reset can leave reset stuck low (TI SLVS165O 7.3.4) |
| U6 | TCA9548APWR | I²C mux, 0x70 |
| U7 | DS3231SN | RTC, 0x68. No EEPROM — 0x57 is gone |
| BT1 | **Panasonic ML-2020/H1CN** | Rechargeable 3V backup cell, solder tabs, charged on the board (3A.8) |
| U8 | Traco TSR 3-2450N | 12V → 5V 3A, as Rev P. **At most 820µF on its output** (datasheet); about 150µF fitted |
| U9 | SN74HCT08N, DIP-14 in a socket | Gate drive for SSR, heater, K1, humidifier, as Rev P |
| U11–U18 | Semtech SRV05-4.TCT ESD arrays | USB (U11), sensor connectors (U12–U15), LCD lines (U16–U18) |
| Q1 | IRF4905 | Reverse protection, as Rev P |
| Q2–Q7, Q11 | AO3400A | Q2 SSR · Q3 + Q11 heater, in parallel · Q4 humidifier · Q5 K1 coil · Q6 ion generator · Q7 blowers |
| Q8, Q9 | BSS138 | Touch I²C level shifter |
| Q10 | AO3401A | SD card power switch |
| K1 | Finder 34.51.7.012.5010 | Interlock, as Rev P |
| D1 | SS54 | Feeds the hold-up and U8; stops the 5V side back-feeding the 12V rail |
| D2 | Littelfuse SMBJ15A | 12V surge clamp |
| D3 | Diodes Inc BZT52C15-7-F | Q1 gate clamp |
| D4, D5, D6, D8, D9 | SS34 | Flyback: heater, humidifier, K1 coil, SSR, ion generator |
| D10 | SS54 | Blower stage freewheel |
| D11 | 1N4148W | RTC cell charger |
| R21, R22 | 6.8Ω 2W, in parallel (3.4Ω) | Inrush limiter ahead of the hold-up |
| C12, C13 | 470µF 35V polymer | Hold-up |
| L1 | 220µH, Sunlord SWRB1207S-221MT | Blower stage |
| J19 | USB-C | Data only |
| J20 | 14-way 0.5mm FPC socket, JUSHUO AFC24-S14FIC-00, **bottom contacts** | LCD |
| J22 | Micro-SD socket | Logging card |

**Assembly: JLCPCB Standard** — the module is "PCBA Type: Standard Only". SMD parts from JLCPCB
stock, preferring Basic parts and reusing one part for several jobs.

**Hand-soldered, all on top.** From **Farnell UK**: U8 TSR 3-2450**N** (4644820 — not the older TSR 3-2450), K1 (1169342, low stock — order
early), J10 Multicomp Pro MP012012 (4179046), Q1 IRF4905PBF (8648190), U9 SN74HCT08N (3120497),
BT1 ML-2020/H1CN (4167439), J23 Multicomp Pro 2211S-05G 1×5 header (1593414). **Supplied by Josh:** J1 XT30PW-M, the fifteen JST XH headers, the
twelve Keystone through-hole test points, the DIP-14 socket. Not fitted: R95–R98 (spare-connector pull-ups, 3A.7).

**Monitor cable:** a USB-serial cable for J23 (3A.5) with **3.3V logic on TX and RX** — a 3.3V VCC
pin alone is not enough. Female 2.54mm ends; its VCC wire is insulated and never used.

## 3A.3 Power

```
J1 XT30 ── 4A fuse (off board) ── Q1 ── D2 clamp ──► +12V
  +12V ──► K1 COM, humidifier +, ion +, blower +, 12V sense divider
  +12V ── D1 ── 3.4Ω ──► +12V_HOLD [2 × 470µF] ── U8 ──► +5V
     +5V ──► plenum fan J2, U9, LCD (through ferrite bead FB1 = LCD_5V)
     +5V ── U2 ──► 3V3_RF    ESP32 module; EN, BOOT, blower, LCD chip-select and display-reset
                             pull-ups; supervisor; USB ESD array
     +5V ── U3 ──► 3V3_Q     SD card (through Q10), mux, RTC, I²C pull-ups, tach pull-up,
                             touch level shifter, LCD-line ESD arrays U16 and U17, spare header
                             (U18, on the 5V touch lines, takes its supply from LCD_5V)
     +5V ── U4 ──► 3V3_SENS  the eight sensor connectors, each through 220Ω
USB VBUS ──► not connected
```

- **Separate 3.3V regulators** so the ESP32's WiFi bursts never reach the SD card, mux, RTC or
  sensors. Each regulator: 10µF in, 10µF + 100nF out; 3V3_RF has a further 22µF at the regulator
  and 22µF + 100nF at the module.
- **Hold-up (calculated):** about 22ms of ride-through at 0.3A on 5V, 12ms at 0.5A, 4ms at the 1.0A
  peak; a longer dip ends in a clean reset. D1 isolates it from dips on the 12V rail. The 3.4Ω
  limits the plug-in pulse to about 3.6A for about 10ms and drops about 0.5V at 0.3A on 5V, about 2V
  at the 1.0A peak.
- **USB powers nothing** and nothing on the board puts a voltage on VBUS. **USB and 12V are safe in
  any order.** With 12V off nothing runs, so **the board is always flashed with 12V on.** The S3's
  1.5kΩ D+ pull-up is always present (under 2mA into a powered-off host's data line).
- **12V sense:** 100kΩ / 15kΩ with 100nF, to GPIO8; 12V reads 1.57V. At D2's 24V clamp level the
  pin sees about 3.2V, inside its limit.

**Power budget (design estimates):** 5V about 1.0A peak (ESP32 0.5A, plenum fan 0.15A, LCD 0.2A,
SD 0.1–0.2A, the rest about 30mA). 12V about 2.9A peak (heater 1.67A, blowers 0.48A, U8 input about
0.58A, humidifier 0.12A, ion 0.04A, K1 and SSR about 0.03A). **The 4A fuse stays** (2.4).

## 3A.4 Pin map

| GPIO | Function | Undriven (boot, reset, flashing) |
|---|---|---|
| 0 | BOOT button, TP11 | High: 10kΩ pull-up to 3V3_RF |
| 1 | Hardware watchdog toggle | Watchdog disabled |
| 2 | Spare, J23 pin 4 | Unrestricted |
| 3 | **Not connected** | — |
| 4 | Cooling (SSR), through U9 | Low: 10kΩ pull-down. Off |
| 5 | Heater, through U9 | Low: 10kΩ pull-down. Off |
| 6 | Interlock K1, through U9 | Low: 10kΩ pull-down. De-energised = cooling selected |
| 7 | Humidifier fan, through U9 | Low: 10kΩ pull-down. Off |
| 8 | 12V sense, ADC1 channel 7 | — |
| 9 | LCD data/command | — |
| 10 / 11 / 12 | LCD CS / MOSI / SCK (SPI2, dedicated pins) | CS high: 10kΩ pull-up to 3V3_RF |
| 13 | LCD backlight | The LCD pulls it high: backlight on |
| 14 | I²C1 SCL — RTC and touch | 2.2kΩ pull-up to 3V3_Q |
| 15 | Blower speed, direct | **High: 10kΩ pull-up to 3V3_RF. Full speed** |
| 16 | Plenum fan tach | 1kΩ pull-up to 3V3_Q, 1µF to GND |
| 17 | Ion generator, direct | Low: 10kΩ pull-down. Off |
| 18 | Plenum fan PWM, 25kHz | Fan at full speed, as Rev P |
| 19 / 20 | USB D− / D+ | — |
| 21 | I²C1 SDA — RTC and touch | 2.2kΩ pull-up to 3V3_Q |
| 35 / 36 | I²C0 SDA / SCL — mux only | 2.2kΩ pull-ups to 3V3_Q |
| 37 | Mux reset | High: 10kΩ pull-up to 3V3_Q |
| 38 | SD MISO (SPI3) | 10kΩ pull-up to the card's supply |
| 39 | SD card power switch | Card powered (internal pull-up at reset; R91 holds the switch fully on against it) |
| 40 / 41 / 42 | SD SCK / MOSI / CS (SPI3) | MOSI, CS: 10kΩ pull-ups to the card's supply |
| 43 / 44 | **Serial monitor port** (UART0 TX / RX), J23 pins 2 and 3 through 220Ω / 4.7kΩ | 43 prints boot text at reset; 44 has a pull-up at reset |
| 45, 46 | **Not connected** | Internal pull-downs give the right boot state |
| 47 | Touch interrupt | High: 10kΩ pull-up to 3V3_Q |
| 48 | LCD reset **and** touch reset, together | High: 10kΩ pull-up to 3V3_RF |

**One unrestricted spare, GPIO2** (J23 pin 4). GPIO43/44 are the monitor port; usable as spares only
with no monitor cable attached. No LCD MISO (the screen is never read). No I²C expander.

**Reset states (Espressif ESP32-S3 datasheet v2.2, Tables 2-1 and 2-2):** no internal pull at or after
reset on any connected pin except GPIO0, 39, 43, 44 (pull-ups) and the unconnected 45, 46. GPIO1–14
and 15–17 are driven low for about 60µs at power-up, GPIO18 low and high, GPIO19/20 high twice.
Harmless: low is off for every load, a lone SCL low is not an I²C start, the LCD is reset and
re-initialised after boot, and on GPIO1 a glitch is at most one watchdog kick.

## 3A.5 Reset, boot and USB

- **EN net — exactly these and nothing else:** the module's EN pin, 10kΩ to 3V3_RF, 1µF to GND,
  the reset button SW1, U5's reset output (push-pull, its pull-up limited to 0.4mA so SW1
  still pulls EN low), TP10.
- **U5 MAX823S** on 3V3_RF holds EN low below 2.93V (2.85–3.00V) and for 200ms (140–280ms)
  after. **Hardware watchdog:** GPIO1 → R26 (0Ω link) → WDI. **Timeout 1.12s minimum** (1.6s
  typical, 2.4s maximum), cleared by a rising or a falling edge. An undriven WDI (under 10µA leakage,
  under 200pF) disables it; leaving R26 out removes it.
- **BOOT:** GPIO0, 10kΩ to 3V3_RF, button SW2, TP11.
- **USB-C J19:** D+ and D− through 22Ω to GPIO20 / GPIO19; CC1 and CC2 each 5.1kΩ to GND; ESD
  array U11 with its supply pin on 3V3_RF; shield to GND; **VBUS not connected.** Nothing on the
  USB side touches EN or GPIO0, so the board runs with no USB. **Native USB is for flashing** (lid
  open). A monitor on it can reset the chip: whenever the host leaves RTS on with DTR off, even for
  an instant (Espressif TRM table 33-2); DTR on with RTS off sets download mode for the next reset.
  There is no run-time switch, and the fuses that disable it are never burned.
- **Monitor port J23** (1×5 header, fitted): 1 GND · 2 TX, from GPIO43 through 220Ω · 3 RX, to
  GPIO44 through 4.7kΩ · 4 GPIO2 spare · 5 3V3_Q. UART0 has no path to EN or GPIO0, so **a monitor
  here can never reset the board.** A 3.3V USB-serial cable uses pins 1–3 only: cable GND to 1,
  cable RX to 2, cable TX to 3. **The cable's VCC wire is never connected.** The cable may be attached with
  the board on or off: with 12V off its TX feeds at most about 0.6mA through R94 onto 3V3_RF.
  **R94 is 4.7kΩ, not higher:** GPIO44's internal pull-up (45kΩ typical, no range given) is on at
  reset, and a low must stay under 0.825V against it.

## 3A.6 Loads

| Output | Chain | Default |
|---|---|---|
| SSR, heater, K1, humidifier | GPIO → 10kΩ pull-down → 1kΩ → U9 gate (other input to +5V) → 100Ω → AO3400A, 100kΩ gate pull-down. **As Rev P** | Off — also with U9 unpowered or out of its socket |
| Ion generator, TFB-YD12102, 12V, two leads (J16) | GPIO17 → 10kΩ pull-down → 100Ω → Q6. No U9 | Off |
| Blowers (J12–J15) | GPIO15 → 10kΩ pull-**up** to 3V3_RF → 100Ω → Q7. No U9 | **Full speed** |
| Plenum fan (J2) | GPIO18 direct to the fan's PWM wire, as Rev P | Full speed |
| LCD backlight | GPIO13 → 100Ω → the LCD's LED pin | On |
| SD card power | GPIO39 → 220Ω → Q10 gate; 10kΩ gate pull-down; 1µF gate-to-source | On |

- **Interlock, as Rev P:** K1 coil A1 to +12V, A2 to Q5. Contact 11 (COM) +12V; 12 (NC) to J10
  pin 1, SSR +A1; 14 (NO) to J10 pin 3, heater +. J10 pin 2 (SSR −A2) to Q2; pin 4 (heater −) to
  Q3 and Q11. The SSR and the heater can never both have a feed.
- **Heater switch:** Q3 and Q11 in parallel, each with its own 100Ω gate resistor and 100kΩ
  pull-down.
- **Blower stage — smoothed PWM, low side.** Blower + pins on +12V; blower − pins joined
  (BLW_NEG, TP12) → L1 → Q7 → GND; D10 from Q7's drain to +12V. Across the blowers: 10µF ceramic,
  plus 470µF polymer in series with 3.3Ω (three 10Ω in parallel). **Blower voltage ≈ duty × 12V**
  (about 11.8V at 100%), about 100kHz, no feedback. Simulated: no overshoot above the supply on a
  reset from any speed, with none to four blowers. Below about 70mA total load the voltage reads
  higher than duty × 12V.
- A flyback diode across every switched output (3A.2).

## 3A.7 Sensors and buses

- **I²C0 (GPIO35/36), mux only, 25kHz.** U6 at 0x70 (address pins to GND), reset on GPIO37, 2.2kΩ
  pull-ups to 3V3_Q, no pull-ups on the channels (the 10kΩ inside each probe, 2.3).
- **Sensor connectors:** six sensors on **mux channels 0–5** (J3–J8); channels 6 and 7 on two spare
  connectors (J17, J18). Each: pin 1 3.3V through **220Ω** from 3V3_SENS with 100nF after it; pin
  2 GND; pin 3 SCL and pin 4 SDA each through 100Ω; one ESD array per pair of connectors. A lead
  shorted to ground draws 15mA and affects that sensor only. **The 220Ω feeds rely on the SHT40
  heaters never being used.** J17 and J18 have **unfitted** 10kΩ pull-up positions (R95–R98) from
  each connector's own 3.3V feed to its SCL and SDA, connector side of the 100Ω — fitted only for a
  sensor with no pull-ups of its own.
- **Channel numbers match the mux:** sensor channel n (2.3) is mux channel n — no mapping.
- **I²C1 (GPIO21 SDA / GPIO14 SCL), RTC and touch, 100kHz** (50kHz fallback if touch misbehaves), 2.2kΩ
  pull-ups to 3V3_Q. A faulty sensor holding I²C0 can't take the clock down.

**I²C addresses on Rev Q:** I²C0 — TCA9548A 0x70; SHT40 0x44, one per mux channel 0–5. I²C1 —
DS3231 0x68; the touch controller, **FT6336U at 0x38** (from lcdwiki's MSP4031 demo code — the
FT6336U datasheet doesn't give it; BP2 test 29's I²C1 scan confirms it).

## 3A.8 RTC

DS3231SN on 3V3_Q. Cell BT1 charged from 3V3_Q through D11 (1N4148W) and 2.2kΩ; it floats at about
about 3.0–3.15V (calculated; D11 drops only about 0.2V at the sub-µA float current), nearer 3.2V if
this board's 3V3_Q is at the top of its tolerance. The cell's range is 2.8–3.2V; if test 30 reads
above 3.2V, about 1MΩ across C49 raises D11's drop (no new board). Backup
comfortably over a year (estimate). **The cell is soldered and stays fitted**; it is never removed
for a test (BP2). 32kHz, INT/SQW and RST not connected.

## 3A.9 LCD

lcdwiki **MSP4031** (4.0" 320 × 480 ST7796, capacitive touch), on **J20** (14-way 0.5mm FPC,
bottom contacts: the ribbon's **bare contacts face the board**) with the supplied **30cm cable, no
longer**. Pin 1 is the left-hand contact, marked `1`, looking at the top face with the antenna at the
top; the ribbon leaves towards the bottom edge and arches over U16–U18 just in front of the socket.
The module's own ribbon socket (P2, on its back) is marked 1 and 14 with the fourteen pins in
header order (lcdwiki size drawing), so J20 matches it pin for pin; a ribbon is either right (n to
n) or reversed (n to 15−n). P2's contact side is not documented: if the supplied "reverse" ribbon
comes out reversed, the other standard type (same-side) is right. The ribbon is the only LCD connection.

| Pin | LCD | Path |
|---|---|---|
| 1 | VCC | LCD_5V: +5V through FB1; 2 × 47µF + 100nF at the connector |
| 2 | GND | |
| 3 | LCD_CS | 33Ω to GPIO10 |
| 4 | LCD_RST | 100Ω to GPIO48 |
| 5 | LCD_RS | 33Ω to GPIO9 |
| 6 | MOSI | 33Ω to GPIO11 |
| 7 | SCK | 33Ω to GPIO12 |
| 8 | LED | 100Ω to GPIO13 |
| 9 | MISO | Not connected |
| 10 | CTP_SCL | BSS138 Q9 to GPIO14 |
| 11 | CTP_RST | 100Ω to GPIO48 (its own resistor) |
| 12 | CTP_SDA | BSS138 Q8 to GPIO21 |
| 13 | CTP_INT | 100Ω to GPIO47 |
| 14 | SD_CS | Not connected — the LCD's own SD slot is not used |

- **On the module:** touch SDA and SCL pulled to **5V** (10kΩ); LED and SD_CS to the module's
  3.3V; CTP_INT and CTP_RST no pull-up (the touch controller runs from the module's 3.3V).
- **Touch level shifter:** Q8, Q9 (gates on 3V3_Q) keep I²C1 at 3.3V; 10kΩ to LCD_5V on the cable
  side of each line. ESD arrays on every LCD line.
- **On the module** (lcdwiki schematic): the LCD and reset lines go through a 74LVC245 buffer on
  the module's own 3.3V regulator, so 3.3V drive is right. The touch lines pass through the
  module's own BSS138 shifter (10kΩ to its 5V on the connector side), so I²C1 crosses **two
  shifters in series** — fine at 100kHz; worth knowing if touch is ever flaky. The LED pin drives a
  BSS138 gate with a 10kΩ pull-up: high or floating = on. The module's SD slot is held deselected
  by its own pull-up.
- **SPI2 at 15MHz or less** (ST7796S minimum write cycle 66ns). **The S3 makes 80MHz ÷ a whole
  number and rounds to the nearest**, so asking for 15MHz gives 16MHz: ask for **13.3MHz (÷6)**.
  20MHz (÷4) is a bench trial only.
- If touch reads fail: R83 and R84 duplicate pull-ups the module already has and can be removed.
- Which end of J20 is pin 1 is fixed at layout; continuity checked before first power (BP2).

## 3A.10 Micro-SD

J22 on **SPI3**, its own bus. 33Ω in series with the clock; 10kΩ pull-ups to the card's supply on
CS, MOSI, MISO and the two unused data lines; 47µF + 100nF at the socket. **Power switch Q10**
(AO3401A) from 3V3_Q: 10kΩ gate to GND, 220Ω from GPIO39, 1µF gate-to-source — switch-on about 3ms after
GPIO39 is released, then about a 1ms rise. The socket's card-detect contact is not connected.

## 3A.11 Firmware rules for Rev Q

- **Pins:** 3A.4. LCD on SPI2, SD on SPI3; mux on I²C0, RTC and touch on I²C1; sensors on mux
  channels 0–5 (3A.7). **Console on UART0 (J23).**
- **Hardware watchdog (GPIO1):** an input until the control loop is running, then its level
  changes **every pass, from the loop** — never a timer. **Never more than 0.9s between changes**,
  including WiFi reconnects, SD writes and OTA updates. To pause it, set the pin back to an input **with both
  internal pulls off** (never pad hold) — and pause it for an OTA erase, which can block for
  seconds, or erase in pieces. The 5s software watchdog (5.2) stays.
- **Reset reason:** the S3 reports every EN-pin reset as a power-up (POWERON), so a U5 watchdog
  reset, SW1 and a supply dip below U5's threshold cannot be told apart from power-on. The boot
  event logs the reason as reported; the software watchdog and brown-out keep their own reasons.
- **SD power (GPIO39):** drive **high to cut** power. **To switch the card back on, set the pin to
  an input with its internal pull-up off** (the soft start); never drive it low — that switches the
  card on hard. Before cutting power, set the four SD lines low or to inputs, or the card is
  back-fed through them. After cutting, wait at least 100ms before switching back on.
- **Blowers (GPIO15):** high = on, duty = speed, about 100kHz; undriven = full speed. Duty is
  calibrated against blower voltage with all four fitted (BP2); the table is in the build log.
- **Ion generator (GPIO17):** high = on; undriven = off.
- **LCD:** GPIO48 resets the LCD and the touch controller together: hold it low at least 5ms, then
  wait at least 120ms before Sleep Out and 300ms before the first touch read. SPI2 at 13.3MHz (÷6;
  3A.9). **Start-up sends the maker's register list** (lcdwiki ST7796_Init.txt: unlock, B0–E8,
  C2 with 0xA7 as its parameter, both gammas, lock), source noted in the code. **Touch monitor mode
  off:** write 0x00 to the FT6336U's register 0x86 after every reset, so it keeps answering while
  idle.
  Backlight on until the firmware drives GPIO13; PWM dims it.
- **LCD at boot:** cleared to black, backlight off until the first walk-past screen has been sent;
  then full, dimmed per 5.5. The BP2 test sketches keep their own behaviour.
- **Nothing the LCD or the web server does holds the loop more than 50ms:** screens are drawn in
  pieces across passes (a full screen at 13.3MHz takes about 184ms, so redraw only what changed, or
  send it by DMA in the background); the web server runs in its own task and never calls the
  control code. **The one exception is an OTA update** (5.2): flash erases stall the loop, which is
  harmless with every load off.
  **Backlight PWM 5kHz, 8-bit.**
- **SD card recovery (Rev Q):** when the remount for a failed write's retry (5.4) fails, power-cycle
  the card once (off, at least 100ms, on, remount) before setting SD FAIL.
- **DS3231 EOSC:** every build clears the EOSC bit at boot if it finds it set, and logs an event (a
  test command sets it for B3 test 16; this stops a forgotten bit stopping the clock at every power cut).
- **32kHz crystal stays disabled** (GPIO15/16 are its pins; the default).
- **SHT40 heaters: never used** (3A.7).
- **12V sense (GPIO8, ADC1):** read every 100ms, averaged over 1s, logged (5.4). **A dip** — ten 100ms
  readings in a row below **11.0V** (exactly 11.0V is not below) — is an event; it has recovered after
  ten in a row at or above 11.0V, and the recovered event gives the lowest 100ms reading and how long
  it lasted. The hourly limit (5.4) counts a dip with its recovery as one. The scale factor is
  calibrated at BP2 (build log); nominal 12V = 1.57V. **ADC at 12dB attenuation** (at the next
  range down it clips at about 12.3V).
- **Serial monitor: J23 only in the cabinet** (3A.5). The native USB is for flashing; never open a
  monitor on it with the board in service.
- **WiFi stays connected** and reconnects by itself (5.4); card writes are not paused for it.
  **Outage buffers in PSRAM**; their sizes are set when the firmware is ported.
- **Free internal heap never below 30 KB at all times** (5.2), WiFi connects included.

### Rules elsewhere in the spec that Rev Q adds

Blower speeds 4.6b · ion generator runs 4.6c · OTA updates 5.2 · the LCD and the web dashboard 5.5.

## 3A.12 Connectors, test points, layout

| Ref | What | Pins |
|---|---|---|
| J1 | 12V in, XT30PW-M | 1 +12V, 2 GND |
| J2 | Plenum fan, XH 4-way | 1 GND, 2 +5V, 3 tach (GPIO16), 4 PWM (GPIO18) — as Rev P |
| J3–J8 | Sensors, mux channels 0–5, XH 4-way | 1 3.3V, 2 GND, 3 SCL, 4 SDA — as Rev P |
| J17, J18 | Spare sensors, mux channels 6 and 7, XH 4-way | same |
| J10 | SSR and heater, 4-way 5.08mm screw block | 1 SSR +A1, 2 SSR −A2, 3 heater +, 4 heater − — as Rev P |
| J11 | Humidifier fan, XH 2-way | 1 +12V, 2 − |
| J12–J15 | Blowers 1–4, XH 2-way | 1 +12V, 2 − (switched) |
| J16 | Ion generator, XH 2-way | 1 +12V, 2 − |
| J19 | USB-C | Data only |
| J20 | LCD FPC socket | 3A.9 |
| J22 | Micro-SD | |
| J23 | Monitor port and spare, 1×5 header, fitted | 1 GND, 2 TX (GPIO43), 3 RX (GPIO44), 4 GPIO2, 5 3V3_Q. Silk: GND, TX, RX, IO2, 3V3 |
| SW1 / SW2 | Reset / BOOT | |

J9 (Rev P's RTC lead) no longer exists.

Test points are Keystone loops, printed with what they carry — **no TP numbers on the board**:

| TP | Net | Printed | TP | Net | Printed | TP | Net | Printed |
|---|---|---|---|---|---|---|---|---|
| TP1 | +5V | 5V | TP5 | Fan PWM | PWM | TP9 | 3V3_SENS | 3V3S |
| TP2 | 3V3_RF | 3V3RF | TP6 | +12V | 12V | TP10 | EN | EN |
| TP3 | GND | GND | TP7 | GND | GND | TP11 | GPIO0 | IO0 |
| TP4 | Fan tach | TACH | TP8 | 3V3_Q | 3V3Q | TP12 | Blower − | BLW− |

**Layout (final, routed, JLCPCB DFM-checked):**

- **Board 117.5 × 88mm, 1.6mm, 4 layers** (JLC04161H-7628). Layer 1 parts and signals · layer 2
  unbroken ground · layer 3 one 12V area under the loads, ground elsewhere · layer 4 signals and
  ground fill. **Every part on the top; the back is bare** (logo only).
- **Antenna cut-out:** board removed above y = 6.8mm from x = 61.4mm to the right-hand edge (measured
  from the top-left corner, looking at the top). The module's antenna sits in it. **No metal within
  15mm of the antenna** in the enclosure.
- **Mounting holes, M3 (3.2mm):** (4.0, 4.0) · (4.0, 84.0) · (113.5, 84.0) · (113.5, 29.0) · (49.0,
  47.1). None in the top-right corner (the cut-out).
- **Edges:** top — blowers 1–4, ion, humidifier (+12V on the inner pin of each), USB-C; left — J10
  (wires from the left), J1 (plug from the left, overhangs 0.4mm), J2; bottom — sensors CH0–CH6;
  right — micro-SD (card from the right), J18 (CH7). RESET and BOOT, J23, and the EN and IO0 loops on
  the right below the SD socket.
- **Tallest parts:** K1 15mm · J10 14mm · C12, C13, C33 13mm · U8 10.2mm · J23 8.5mm (more with a
  cable) · U9 in its socket and L1 about 8mm · XH headers 7mm (about 10mm with a plug).
- Positions of every connector, hole, button and test point: `placement-B-positions.md`.

---

# 4. CONTROL RULES

## 4.1 Targets

| Target | Value |
|---|---|
| Temperature | **Default 20°C**, user-adjustable 15–22°C |
| RH | **Default 62%**, user-adjustable 55–70%. Controlled actively: the humidifier below setpoint (4.7b), RH stripping above it (4.7) |
| **RH dip per cooling cycle** | **Maximum 3–4 points** in cabinet mean RH |
| Room ambient | 20–22°C measured in autumn; summer maximum ~28°C |

**The RH dip target** protects wrappers: a thin leaf that responds far faster than the filler, so
repeated sharp dips crack it while the interior cannot follow. The time-weighted average protects
the filler; the dip limit protects the wrapper. **Check cigars on shelves 4, 5 and the floor every
week or two** for fine cracks, splitting at the cap or lifting wrapper — those levels swing most. A
cigar hygrometer reads bulk moisture and will not show wrapper stress.

**The dip** is the fall in the mean RH of the healthy cabinet sensors (ch 0–3) from its pre-cycle
baseline to the trough. The sensors sit out of the plenum and blower streams (B5 step 5), so no
weighting is applied.

The fridge's own thermostat never acts once the controller is in charge.

## 4.2 Control source

The controller averages the four cabinet sensors, channels 0–3. The chamber sensor is excluded: it
reads plenum conditions next to the plate and heater, not the air the cigars sit in. Not user
configurable.

| Measured spread | Control source |
|---|---|
| Under 1°C | Mean |
| 1–2°C | Mean; investigate the cause |
| Over 2°C | Warmest healthy sensor — warm is the risk for cigars |

If the spread is large, fix the airflow before changing the control source. Log mean, min and max
every row.

## 4.3 Sensor health

Automatic exclusion on any of:

| Condition | Threshold |
|---|---|
| CRC failures | 3 consecutive |
| I²C timeout | 3 consecutive |
| No response | Absent from mux channel |
| Implausible value | Outside −10 to 50°C, or RH outside 0–100%, **on the raw value before clamping** |
| Divergence — cabinet sensors (ch 0–3) only, and only while ≥3 are healthy | >5°C from the median of the healthy cabinet sensors for 10 min |
| I²C bus fault | The channel selected when the main bus locked — see 5.6 |

Recovery: 10 consecutive good plausible reads — and, after a divergence exclusion, all 10 within
5°C of the median. Both logged. The chamber and ambient sensors get every check except divergence:
the chamber reads plenum air by design (4.2), the ambient the room. **At boot** a sensor is healthy
from its first good, plausible read with the right serial; the boot hold covers the first minutes.

| Requirement | Threshold |
|---|---|
| Chamber sensor (ch 4) | Must be healthy |
| Ambient sensor (ch 5) | Not required — failure degrades dosing to feedback only |
| Cabinet sensors (ch 0–3) | At least 3 of 4 healthy |

If either requirement fails, compressor and heater shut off immediately and stay off until it is
met **and** a 5 min hold has elapsed. The humidifier needs only the cabinet requirement, and shuts
off the same way when it fails. The fan continues. The chamber sensor is never promoted to a
control source.

## 4.4 Runtime guards

Evaluated every control pass. They override all other logic; no override, manual mode or test
mode disables them.

### Compressor

| Guard | Value |
|---|---|
| Minimum off | 5 min — hard floor |
| Minimum on | 1 min |
| Maximum continuous run | 30 min, then rest ≥ minimum off |
| Startup delay after power restore | 5 min |
| Cooling hysteresis | 0.5°C — see 4.5 |
| Crossover delay since heater stopped | 10 min |
| Interlock | Cooling position |
| Fan | Tach non-zero with PWM asserted |
| Sensors | Chamber healthy AND ≥3 of 4 cabinet healthy |

Minimum on exists for fault conditions only — natural runs are far longer. Each compressor start
is its most wearing event.

### Heater

| Guard | Value |
|---|---|
| Minimum off | 2 min |
| Minimum on | 1 min — anti-flap backstop only |
| Maximum continuous run | 60 min, then rest ≥ minimum off |
| Startup delay after power restore | 5 min |
| Crossover delay since compressor stopped | 10 min |
| Interlock | Heating position |
| Fan | Tach non-zero with PWM asserted |
| **Ceiling, cabinet** | Any healthy cabinet sensor above **setpoint + 3°C** — protects the cigars, so it follows the setpoint |
| **Ceiling, chamber** | Chamber sensor above **30°C** — the lowest measured KSD9700 trip, 32.9°C, less 2°C, rounded down. Protects the hardware: software stops the heater before the cut-out has to |
| Sensors | Chamber healthy AND ≥3 of 4 cabinet healthy |

The cut-out is the last resort; a trip means something upstream failed. Its reset is above cabinet
temperature, so it re-closes by itself; the heater-ineffective check in 5.6 detects a trip that has
not yet reset.

### Humidifier

| Guard | Value |
|---|---|
| Minimum off | 5 min |
| Minimum on | None — hysteresis and minimum off prevent chatter |
| Maximum continuous run | 30 min, then rest ≥ minimum off — even if RH is still rising |
| Startup delay after power restore | 5 min |
| **Hard ceiling** | Any healthy cabinet sensor at or above **70% RH** — off immediately |
| RH stripping active | Never |
| Compressor on, temperature-driven run | Only if *humidify during cooling runs* is enabled — default off, B6 step 9 |
| Sensors | ≥3 of 4 cabinet healthy |
| Disturbance hold-off | Suppressed, like the other loads |

No hardware interlock: software is the only guard. Fail-safe is off, so every rule errs that way.

### Timing state at boot

Last-off times are **not** saved across a restart. On every boot, assume worst case: **treat every
load as having just stopped**, so the boot hold, minimum-off and crossover delay all run from boot.
On the real profile the compressor and heater are first allowed 10 min after boot (the crossover
delay), the humidifier after 5. Saved times could only shorten the wait, and a power cut costs one
short wait.

## 4.5 Deadband and crossover

Two defences against bouncing between heating and cooling: the **deadband**, where nothing runs,
is primary; the **crossover delay** is the backstop.

| Zone | At a 20°C setpoint | Action |
|---|---|---|
| Above setpoint + cooling hysteresis | >20.5°C | Cool |
| Dead zone | 18.5–20.5°C | Neither runs |
| Below setpoint − heating deadband | <18.5°C | Heat |

**Until B6 sets dosed control (4.5b), runs end on simple thresholds:** cooling stops **at setpoint**;
heating stops at **setpoint − 1.0°C**, leaving room for its overshoot before the cooling zone. The
heater runs at full duty while on; its PWM cap stays a tuning option. B6 replaces both rules.

Cooling hysteresis 0.5°C, heating deadband 1.5°C — deliberately asymmetric:

**Cooling hysteresis must be tight.** A fixed-speed compressor cools at full intensity, and the
plate gets colder the longer it runs; condensation depends on how far the plate sits below dew
point, so **run length sets how much water is stripped**, not the cooling delivered. 3°C hysteresis
gave ~22-point dips; 0.5°C halved them. Hysteresis decides when a run starts; under dosed control
(4.5b) run length is calculated directly.

**The deadband must exceed the worst harvest overshoot in either direction**, or one load's
overshoot triggers the other and the cabinet ping-pongs. Empty-cabinet overshoot measured 0.6–0.9°C
cooling; loaded ~0.4°C. Anticipation — stopping early and letting harvest finish the approach —
widens the margin. Heating overshoot is measured in B2 step 6 and B6.

**Crossover delay** — minimum time between one load stopping and the other starting. Default **10
minutes**, both directions; a tuning value set once in B6 to match the overshoot settling time.
Repeated crossover within the delay means the deadband is too narrow: log every crossover and
show the count on the dashboard. **A crossover** is a temperature load starting when the last one to
run was the other; a start the crossover guard blocks is a guard block, not a crossover.

The dead zone is where the cabinet should sit almost all the time. If either load runs frequently,
widen the bands before anything else.

## 4.5b Dosed control

**The RH dip is made entirely during the compressor run**, by air crossing the cold plate, and
recovers the moment the compressor stops. So the controller controls run length directly rather
than letting it emerge from a threshold.

| Lever | Effect on dip depth |
|---|---|
| **Run length** | Reduces it — a shorter run stops the plate reaching its coldest |
| **Fan speed during the run** | Reduces it — less air across the cold plate |
| Fan speed during harvest | None; speeds recovery only |
| Anticipation | Reduces it, by ending the run early |

**Dose, then correct from feedback:** the controller estimates the run from the temperature error
and the room–cabinet gap (channel 5), delivers it, compares the result against target, and nudges a
single correction factor each cycle — clamped so it cannot wander far. The dosing function's shape
and starting coefficients come from the B6 mapping test; feedback then tracks load, season and
ageing. Without the ambient sensor, feedback alone corrects a cycle or two later.

All 4.4 guards still bind; the dose is clamped between the compressor's minimum on and maximum run.

**The floor on run length:** harvest only has cold to give because the run built it. Too short and
harvest fails to reach setpoint, the compressor restarts sooner, and starts and total stripping
both rise. The optimum is a run just long enough for harvest to finish the approach. Whether
short frequent runs strip less than long infrequent ones is the key B6 measurement.

**Harvest profile:** fan fast at the start, when the plate holds the most cold and water,
progressively slower approaching setpoint.

## 4.6 Fan control

The circulation blowers mix the cabinet 24/7 and the humidifier supplies moisture, so the plenum
fan has no job at idle except keeping the plenum from stagnating. Whether it runs slow or stops at
idle is decided in B6 step 3. It must run whenever the compressor or heater runs, and through
harvest.

| State | Fan |
|---|---|
| Idle | Minimum PWM, or stopped — B6 step 3. If stopped: the fan starts (with its kickstart) as soon as a load is asked for, and the fan guard is judged once it is running — otherwise a stopped fan's zero tach would block every start |
| Compressor on | Set in B6 step 1 — likely slow, to reduce stripping, limited by mixing |
| Cooling harvest | Profile — fast at first, slowing towards setpoint |
| Heater on | Must be running |
| Heating harvest | Elevated, until the chamber sensor approaches cabinet air |
| Harvest, chamber sensor unhealthy | Runs to the backstop |
| Any fault state | Continues to run |

**Harvest** recovers residual cold from the plate (and clears frost) or residual heat from the
pad. Both terminate on **sensor convergence** — the chamber sensor returning to within a set margin
of cabinet air — never a fixed timer, because overshoot depends on load and fan speed. Stop the
compressor slightly before target and let harvest complete the approach: the plate is coldest at
the very end of a run, so stopping early strips less and overshoots less.

### Duty, not RPM

Firmware controls the fan by duty cycle. RPM is never a control input; the tach serves only the
**fan-failure guard**, which needs a reliable zero-versus-non-zero, and diagnostics — bearing wear
shows as falling RPM at the same duty against the baseline curve in the build log.

| Command | Result |
|---|---|
| 0% | Fan stops |
| 1–19% | **Never command** — Noctua define nothing below 20% |
| 20% | Minimum reliable, ~400 RPM |
| 20–100% | Normal operating range |

**Kickstart:** any transition from 0% to running ramps to 100% for 1 second, then drops to the
target duty. Required at boot before any other startup step, on recovery from a tach-stall fault,
and on any commanded transition from 0% — including every restart from an idle stop, if B6 adopts
one.

## 4.6b Circulation blowers (Rev Q)

The four blowers run **24/7** (2.2). Rev Q sets their speed (3A.6); Rev P runs them at full speed.

| Condition | Blowers |
|---|---|
| Normal | **Idle speed** |
| Cabinet sensors disagree: spread (highest minus lowest of the healthy cabinet sensors, ch 0–3) above **1.0°C** or above **3 points RH** | **Boost: full speed** |
| Back to idle | Spread below **0.5°C and 2 points RH**, held for **10 minutes** |
| **Fault** (5.5's meaning), or no fresh reading for twice the sensor read rate | Full speed |
| During an OTA update (5.2) — a fault included, as every load is off | Idle |
| Reset, boot, flashing (GPIO15 undriven) | Full speed (hardware) |

**The rule runs normally during the boot hold.** After any spell at full speed (boot, a fault
ending, an update ending) it starts in **boost**, so it needs its 10 minutes of low spread before
idle. "Above" is strict; between the thresholds the blowers stay as they are. The hold is timed by
uptime.

The idle speed is a duty setting chosen by ear and airflow from BP2 test 22's duty-to-voltage
table, confirmed in B6; never 0 — the blowers run 24/7. Its floor is the lowest duty at which all
four start and run cleanly (BP2 test 22). The spread thresholds sit below the divergence check (4.3), so mixing acts
before a sensor is judged faulty.

## 4.6c Ion generator (Rev Q)

Short, infrequent runs only: the module's benefit lasts only while it runs, and ionisers give off a
little ozone, which cigars must not be exposed to for long.

| Rule | Value |
|---|---|
| Runs per day | **2**, evenly spaced: **24/N hours apart in fixed UTC slots from 00:00** (2 a day: 00:00 and 12:00 UTC). If the time is not trusted, every 24/N hours from boot, the first one 24/N hours after boot; once the clock is trusted, the next run follows the clock. A run missed while the board was off is not caught up. **Each slot runs once:** a run starts only when the clock reaches its slot by normal running; a clock stepped forward past a slot's start skips it as missed, and a clock stepped back never repeats a slot already run |
| Minutes per run | **2**, timed by uptime |
| Settings over 10 minutes a day | **Refused**: runs × minutes must be 10 or less ("at most 10 minutes a day in total") |
| Hard cap | **10 minutes in any 24 hours** of actual on-time — never exceeded, whatever the settings; a backstop. **Kept across reboots** in NVS (each run's start and on-time); **every stored run counts as recent until 24 hours of uptime have passed since it started, added up across reboots, whatever the clock says** (a wrong clock can't age runs away; time with the board off only makes a run older); a reboot during a run counts its whole planned length. At the default 2 × 2 min this changes nothing; at the maximum, a reboot can mean no runs for up to a day. **If the record can't be saved, no new run starts** until a save works again (logged; shown as held) |
| Not during | The boot hold, an OTA update, or a **Fault** (5.5). A run due then is **skipped**, not moved, and logged; one cut short is not resumed, and its on-time counts towards the cap |
| Switched off | **Ion generator enable** (user setting, 5.5) off: no runs at all; the run settings keep their values for when it is switched back on. Off mid-run (or runs or minutes set to 0): off at once, logged; switching back on waits for the next slot. A settings change mid-run leaves that run's length as it started |

A sharp, chlorine-like smell when the door is opened means too much: reduce the runs.

## 4.7 RH stripping

Running the compressor condenses moisture on the plate, which the water trap returns to the
humidifier tank. The **upper half of RH control**; the humidifier (4.7b) is the lower half. The
humidifier's off point sits below the stripping trigger, so the two are never called together.

| Condition | Value |
|---|---|
| Trigger | Mean cabinet **RH** ≥ setpoint + offset (default 5%), **sustained** — persistence set in B6 step 5 |
| Release | Mean cabinet **vapour pressure** ≤ that of setpoint RH at setpoint temperature — 62% at 20°C is 1.45 kPa |
| Temperature floor | Stop if any healthy cabinet sensor falls below setpoint − 2°C |

RH from the healthy cabinet sensors, channels 0–3; never the chamber sensor. **Mean vapour pressure
is the mean of each healthy cabinet sensor's own vapour pressure.** After the floor stops a strip,
the full persistence runs again from zero, starting once every cabinet sensor is back above the
floor.

**Trigger on RH, release on vapour pressure.** Cooling air raises its RH even as water is removed
(62% at 20°C reads 85% at 15°C with nothing removed), so a loop releasing on RH could never come
and would cool harder. Vapour pressure only moves when water enters or leaves.

**Rules:** all compressor guards apply unchanged. The temperature floor wins — cigars tolerate
slightly high RH better than cold and wet. Never runs against the heater or the humidifier. Expect
RH to keep falling as the cabinet warms back; the humidifier restores it afterwards, subject to its
minimum off. **Repeated triggering is a fault signal** — more than a few times a week means a door
ajar, a failed seal, very fresh cigars, or a humidify band set too wide. Log every event and show
the count.

## 4.7b Humidification

| Condition | Value |
|---|---|
| Trigger | Mean cabinet RH ≤ setpoint − humidify band (default 2 points), **sustained** — persistence set in B6 step 8 |
| Release | Mean cabinet RH ≥ setpoint |
| Ceiling | Any healthy cabinet sensor ≥ 70% — off immediately, regardless of the mean |

RH from the healthy cabinet sensors, channels 0–3; never the chamber sensor. All 4.4 humidifier
guards apply unchanged. **A call stays on until RH reaches setpoint**: guards (max run and rest,
sensors, ceiling) only pause the humidifier, and it resumes when they clear, with no fresh count.
**The persistence count runs only while no strip is active**; it starts from zero when a strip ends.

**Never during RH stripping** — the two are opposite actions. **During a temperature-driven
compressor run** the plate pulls some of the added water straight back out, but cabinet air still
ends the run wetter than without; whether that shrinks the dip enough to justify the water loop is
measured in B6 step 9. Until then the setting is off. During harvest and heating the normal trigger
decides — no special case.

**Response is slow and sized by the fan and wick**, not controllable: the band, persistence and
ineffective window are set from B6 measurement of how fast the cabinet RH rises with the
humidifier on.

## 4.7a Disturbance detection

A door opening lurches the cabinet towards room conditions within seconds. Detected from the
sensors — no door switch — and control actions held off briefly while the cabinet recovers.

| | Door opening | Genuine change |
|---|---|---|
| Speed | Seconds | Minutes to hours |
| Sensors affected | All cabinet sensors together | Usually one region first |
| Direction | Towards room conditions, either way | Gradual drift |

**Detection** — all must hold:

| Condition | Threshold |
|---|---|
| Rate of change of cabinet mean temperature, either direction | Set in B6 |
| Cabinet sensors moving the same way | At least 3 of 4 |
| Time since the controller last switched any load | Longer than a value set in B6 — harvest has the same signature |

RH may corroborate. The ambient sensor confirms direction: a shift *away* from ambient is not a
door. Detection continues without it if it has failed.

**Disabled until B6** sets the thresholds: built in, switched off, so it can't act on guessed values.
Built and native-tested before B6, not in B3.

**Response:** hold-off of a few minutes (B6), extended by re-triggers, with a **hard cap** (B6) —
when reached, control resumes regardless and the event is logged.

| Suppressed during hold-off | Never suppressed |
|---|---|
| Compressor calls, heater calls, RH stripping, humidification | Every safety guard, the fan, logging |

Log every detection: direction, magnitude, hold-off duration, whether the cap was reached.

**Thresholds come from measurement:** B6 measures normal rates of change (compressor runs, harvest,
heater cycles) and door-opening rates (deliberate tests), and sets the threshold in the gap. If the
two overlap, the approach needs rethinking.

## 4.8 Layered protection

| Layer | Mechanism | Independent of |
|---|---|---|
| 1 | Software guards | — |
| 2 | K1 interlock in the 12V feed | Software, and MOSFET failure |
| 3 | KSD9700 thermal cut-out | Software and interlock |

The humidifier has software guards only. Its independent limits are physical: the tank's fill
line, and a PG biocide solution's own ~70% evaporation limit if one is used.

## 4.9 Safety rules

- Heat and cool never run together — enforced in hardware
- Refrigerant circuit off limits. The four evaporator plate screws stay in
- R600a is flammable — no drilling, cutting or heat near the rear wall or machine compartment
- All electrical parts outside the cabinet except the plenum fan, circulation blowers, humidifier
  fan, sensors, heater pad and KSD9700
- No GPIO drives a load directly: the four load signals go through U9 to IRLZ44N MOSFETs, each
  with a gate pull-down that holds it off when U9 is unpowered or missing

## 4.10 Mains wiring

**In service: enclosure mains.** The same circuit as the bench harness, inside the enclosure, with
the PSU hung off the incoming side. A mains lead (13A plug, **5A fuse**) enters through the M16
gland; the fridge plugs into the BS 1363 socket on the enclosure (Pro Elec 9143), which is
switched by the SSR. **Identify conductors with a meter, not by colour.**

| Conductor | Route |
|---|---|
| Live | Lead → **live WAGO** → (a) IRM-60-12ST L, (b) SSR L13. SSR LOAD → socket L |
| Neutral | Lead → **neutral WAGO** → IRM-60-12ST N, socket N, MOV 2 leg, X2 leg. Not switched |
| Earth | Lead → **earth WAGO** → socket E. The IRM-60-12ST is fully potted with L and N only, so nothing else is earthed. Continuity lead plug to socket earth before any power |

MOV 1 across SSR L13–LOAD; MOV 2 and X2 across socket L–N — exactly as on the bench harness (3.3,
2.5). Joints per 3.3: rings at the SSR, WAGO 221 for the three splits. The lead's 5A plug fuse
protects everything in the box; the IRM-60-12ST is internally fused; the fridge's own plug fuse
stays in series. The SSR's +A1/−A2 go to J10 pins 1–2 on the PCB (3.3). One gland, mains in only;
every 5V/12V lead leaves the PCB through its own clamp, well away from the mains side. **Prove it with the B2.4 and B2.5
checks repeated on the enclosure** (B5 step 2) before the fridge is plugged in.

**Bench harness. Cutting the extension flex.** The fridge flex is never cut. The extension is cut straight
through into PLUG LEAD and SOCKET LEAD. **Identify conductors with a meter, not by colour**,
unplugged: continuity from each PLUG LEAD core to its plug pin.

| Conductor | Route |
|---|---|
| Live | PLUG LEAD → SSR L13; SSR LOAD → SOCKET LEAD |
| Neutral | PLUG LEAD → neutral WAGO → SOCKET LEAD. Not switched |
| Earth | PLUG LEAD → earth WAGO → SOCKET LEAD. Continuity checked plug to socket before any power |

Joints and component placement per 3.3. The extension plug's 5A fuse protects this circuit, with
the fridge's plug fuse in series. On the bench the 12V comes from the bench PSU through the 4A fuse. Strain-relieve both leads
where they enter the enclosure.

**Cable entry** is a hole drilled in the compressor hump, not the drain. Inspect the machine
compartment from below with a torch first — refrigerant tubing, drain and wiring run there. Drill
from inside, slowly, sharp bit, depth stop. Fit a grommet or gland sealed with neutral-cure
silicone; route sensor, fan and heater cables together.

---

# 5. FIRMWARE

## 5.1 Environment

Apple Silicon Mac, macOS 26.

| Component | Choice |
|---|---|
| Editor | VS Code |
| Extension | **pioarduino IDE**, not PlatformIO IDE (deprecated for ESP32, stuck on core 2.0.17). Never install both |
| Platform | pioarduino, pinned release 55.03.311 |
| Board | `esp32dev` (Rev N/P). Rev Q: `esp32-s3-devkitc1-n8r2` (8MB quad flash, 2MB quad PSRAM), console on UART0 (J23), flashing over native USB — environment below |
| Framework | Arduino |
| Core | Arduino-ESP32 3.3.11 on ESP-IDF 5.5.5 |
| Toolchain | xtensa-esp-elf 14.2.0 |
| Serial driver | **Silicon Labs CP210x VCP driver** — Apple's built-in driver rejects every `tcsetattr` on macOS 26. Port `/dev/cu.SLAB_USBtoUART`; ignore `/dev/cu.usbserial-0001`. Not the WCH CH34x driver |

```ini
[env:esp32dev]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = esp32dev
framework = arduino
monitor_speed = 115200
upload_port = /dev/cu.SLAB_USBtoUART
monitor_port = /dev/cu.SLAB_USBtoUART
monitor_filters = time
monitor_rts = 1
monitor_dtr = 1
board_build.partitions = min_spiffs.csv
```

`min_spiffs`: ~1.9 MB for firmware with two app slots, so wireless (OTA) updates stay possible;
logs live on the SD card, so flash file storage is not needed.

**Rev Q:**

```ini
[revq_common]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip
board = esp32-s3-devkitc1-n8r2
framework = arduino
build_flags = -Wall -DCORE_DEBUG_LEVEL=0 -DWINEADOR_BOARD_REVQ -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=0
board_build.partitions = default_8MB.csv
monitor_speed = 115200
monitor_filters = time
test_ignore = *
monitor_rts = 0
monitor_dtr = 0
; upload_port: the native USB's /dev/cu.usbmodem… (set at BP2 test 15)
; monitor_port: the J23 cable's port (set when the cable is here)
```

`-Wall` keeps the no-warnings rule; `CORE_DEBUG_LEVEL=0` stops the framework's own error printing
holding up the loop. `ARDUINO_USB_CDC_ON_BOOT=0` puts `Serial` (and the boot messages) on UART0, GPIO43/44 — J23.
`default_8MB.csv`: two 3.2 MB app slots for OTA; NVS at the same address and size as Rev N.
`monitor_rts`/`monitor_dtr` at 0: J23 has no RTS/DTR wires; on the native USB, both off is the state
that neither resets the chip nor sets download mode — but never monitor on the native USB (3A.5).
Rev N builds carry `-DWINEADOR_BOARD_REVN`; a build with neither or both flags stops with an error.
**Heap:** the prebuilt S3 framework keeps WiFi and network-stack buffers in internal RAM (PSRAM is on,
but not for WiFi/lwIP). Outage buffers go in PSRAM. If the 30 KB floor (5.2) is not met at all
times on the board, the fix is pioarduino's `custom_sdkconfig` to move WiFi/lwIP buffers to PSRAM —
decided on measurement (BP2 test 39).

`monitor_rts` and `monitor_dtr` at **1** stop the monitor resetting the board when opened or closed —
essential for long runs. On the DevKit, EN is pulled low only when RTS is on and DTR off; at 0 the
monitor turns DTR off first, leaving RTS on alone for a moment, which resets the board. A custom
serial script must likewise clear RTS before DTR. Pin the platform to a release tag so a rebuild years later matches. Use a
data-capable USB cable. If auto-reset fails: hold BOOT, tap EN, release BOOT, flash. Close the
monitor before uploading. On Rev P, flash and monitor with the ESP32 on the board, USB and 12V in any
order (2.4). **Rev N** has no D7: **keep 12V on whenever USB is connected** — 12V on before USB in, USB out before 12V off. With that, flashing on the board is fine. Any test that cuts the 12V (B3 test 12) runs with USB unplugged first.
**Rev Q:** USB powers nothing, so USB and 12V are safe in any order; flash and monitor with 12V on.
The CP210x driver and the DevKit's RTS/DTR behaviour do not apply. **Monitor on J23** with a 3.3V
USB-serial cable (3A.5); the native USB only for flashing.

**With the DevKit pull-ups fitted (3.3)**, USB can be plugged into or unplugged from a running
board without restarting it, and the board runs with no USB at all.

## 5.2 Non-blocking architecture

**Nothing blocks. Ever.**

| Operation | Requirement |
|---|---|
| I²C read | Hard timeout on every transaction |
| SD write | Queue and flush asynchronously, timeout |
| WiFi connect | Never block; retry in background |
| HTTP handler | Serve from buffer, never read SD synchronously |
| RTC read | Timeout; fall back to millis-based timing |
| Relay settle | State machine, not a delay |
| `delay()` | Never used, anywhere |

- Non-blocking superloop, or FreeRTOS with the control loop at higher priority
- All timing from `millis()` against stored timestamps; all state in explicit, resumable state
  machines
- Peripheral failure returns an error immediately, no inline retry
- Watchdog enabled, **5 s** timeout. **Rev Q also has a hardware watchdog** (3A.11)
- **Heap monitoring:** take the baseline after all initialisation completes, and never sample
  during a file write
- **Weekly maintenance task:** exercise the interlock relay, three full cycles with both loads off —
  each cycle 1 s energised, 1 s released, ending on the cooling side.
  Log it as an event

**Minimum job each pass, regardless of any other failure:** evaluate compressor, heater and
humidifier guards — the 70% RH ceiling included — and turn off any load whose guards are
unsatisfied. Turning a load off must never depend
on a sensor read, a log write or the network.

**Free internal heap never below 30 KB** (internal RAM only — on Rev Q the framework's ordinary free-heap calls include PSRAM, so the
internal-only calls are used; PSRAM's lowest free is logged separately, 5.4), including during WiFi bursts and SD writes; the lowest seen is
logged in the readings. On **Rev N** the floor applies outside NTP windows: while WiFi connects its
own buffers dip it to ~29 KB, which is logged (card writes are paused). **Rev Q must meet
it at all times.**

**OTA updates** (from B6/B7): on Rev Q only inside the LCD's Allow update window, and only a Rev Q cabinet build (5.5); a new image that fails to start properly rolls
back to the previous one. **Started properly** = the control loop has run 60s with the settings
check passed, the hardware watchdog toggling and the first full sensor read done; only then is the
new image marked good, and a reset before that rolls back. A rollback is logged and shown on the
Update page (two app slots: `min_spiffs` on Rev N, `default_8MB` on Rev Q); after any update the normal boot hold
applies.
- **Starts only when the compressor is not running** — otherwise it is refused with a message, so
  an update never cuts a cooling run short.
- **During an update:** cooling, heater, humidifier and ion generator off; blowers at idle; plenum
  fan at its idle setting. **Flash erased in 4 KB pieces with the hardware watchdog still running**
  (each erase well under its 0.9s; if one ever ran longer the board would reset with the old image
  still in place), or the watchdog paused for the erase (3A.11). During the restart
  itself the blowers and plenum fan run at full speed (hardware default).
- After the restart the normal boot rules apply (boot hold; 4.4's timing state, so the compressor
  waits its full minimum off).
- **An upload that stalls** (no data for 60s) is abandoned: the half-written image is discarded,
  normal control resumes, and an event is logged. If the writing stops moving for 70s, the loop
  ends Updating by itself, so the loads are never left off.
- **"First full sensor read"** = the first sweep of all six channels has finished, whatever its
  results — a dead sensor never rolls back a good image.
- **Not marked good within 4 minutes 30 seconds of boot** (running, but never meeting the rule):
  the image rolls itself back, logged — inside the 5-minute boot hold, so a rollback restart never
  cuts a run short.
- **During an update** no sensor sweeps and no fan-stall check (the flash pauses would cause false
  timeouts and stalls; every load is off); both resume after the restart. **No readings rows are
  written while Updating** (5.4): the gap shows as a break in the graphs, and the update events mark
  why.
- **The Update page names the image before it is sent**, and says plainly when it is the rollback
  test image (B7 test 6).
- **While a new image is still being checked** (not yet marked good), a further upload is refused:
  "the last update is still being checked: try again in N s".
- **Rev Q only:** the identity block is in Rev Q builds; a Rev N image is refused by its chip ID.

## 5.3 Sensor handling

- I²C 25kHz, explicitly set. Internal pull-ups disabled — **after every bus step**: the Arduino core
  creates the bus with them enabled, and ESP-IDF re-enables them whenever it resets the bus after a
  timeout
- **Pulse mux RST once at every boot** before the first I²C access — the mux keeps its channel
  selection across an ESP32 reset
- Select the mux channel at 0x70, then address the SHT40. Verify CRC on every reading; log failures
  per channel. Timeout every transaction
- **Read every serial at boot and check it against the expected serial for its channel.** The
  calibration coefficients belong to the serial, not the channel. **A channel whose serial doesn't
  match is excluded and logged** — its coefficients would be wrong — and the 4.3 requirements decide
  what still runs. **Re-read the serial before accepting any recovery** (after absence or a bus
  fault): plugs may have been swapped while it was out
- Apply the calibration per serial: **RH = gain × raw + offset**, from the build log. Temperatures
  uncorrected
- Readings lag a temperature change by 30–45 minutes while the surrounding material rebalances —
  RH dips as the air warms and rises as it cools

### Fan PWM and tach

Working reference code: `soak_test.cpp` (PWM, tach width check, sensors, SD, RTC).

**PWM, P25:**
- 25kHz, 8-bit. Arduino-ESP32 3.x attaches **by pin**: `ledcAttach(25, 25000, 8)`, then
  `ledcWrite(25, duty)`; the 2.x channel calls no longer exist. Duty = percent × 255 ÷ 100
- **Leave P25 untouched until `ledcAttach`** — undriven, the fan runs at full speed, the safe-fail
  state
- 20% minimum and the kickstart on every start from 0% — see 4.6

**Rev Q:** PWM on GPIO18, tach on GPIO16 — read from `GPIO_IN_REG` bit 16 (GPIO0–31 register);
otherwise as below.

**Tach, P34:**
- `pinMode(34, INPUT)` first
- **Interrupt on CHANGE.** Read the level directly from the register — `REG_READ(GPIO_IN1_REG)`,
  bit 2, from `soc/soc.h` and `soc/gpio_reg.h` — `digitalRead` is not guaranteed safe in an ISR
- On a falling edge note the time; on a rising edge **count a pulse only if the line was low for
  ≥1ms.** Set the starting state from the pin before attaching the interrupt
- Counters `volatile`, written only by the handler, read and cleared with interrupts paused.
  Increment as `x = x + 1` — C++20 deprecates `++` on a volatile
- **RPM = pulses × 60,000 ÷ 2 ÷ window ms.** One pulse is 6 RPM over 5s, 30 over 1s; the fan
  guard needs only zero against non-zero
- Also count every transition and log the transitions-per-pulse ratio; a rising ratio flags a
  degrading wire or filter

**A stopped fan reads exactly zero.** Zero pulses over the window with ≥20% commanded is a stall —
see 5.6. Expected speeds and the full duty curve are in the build log.

## 5.4 Logging

Local to SD, CSV throughout. Nothing pushed off the device.

| Activity | Rate |
|---|---|
| Sensor read for control | 15s |
| Log, steady state | 60s |
| Log, compressor or heater active | 15s |
| State changes | Immediate, to the events file |

| File | Contents | Kept |
|---|---|---|
| Readings | Every sample at full resolution | Indefinitely |
| Events | Starts, stops, faults, setting changes, boots | Forever |
| Hourly | Min, max and mean per hour, each channel and derived value; each load's on-time; lowest-ever free heap; on Rev Q also the 12V mean and lowest 1s average, and the lowest free PSRAM | Forever — what the dashboard graphs from |

**Readings row:** timestamp · per-channel temperature, RH and raw RH (before calibration) · ambient temperature and RH · cabinet
mean, min and max temperature · cabinet mean RH · active control value · fan
PWM · fan RPM · compressor state · heater duty · humidifier state · interlock position ·
per-channel error flags · time trusted (0/1) · longest loop pass in the interval · free heap · lowest-ever
free heap (internal RAM) · worst
SD write time · 12V mean and lowest 1s average in the interval · lowest-ever free PSRAM (the last
three empty where the board has neither). Cabinet aggregates cover channels 0–3 only. In test builds, the real sensor readings
are logged alongside the scripted ones.

**Events row:** timestamp · uptime (ms, so timings survive a clock correction) · type · detail. Types: compressor on/off with reason · heater on/off ·
interlock change · harvest start and end · guard blocks · CRC failures and timeouts per channel ·
sensor excluded or recovered · fan tach stall · settings changed with old and new values and where from (LCD, dashboard or serial) · boot,
with reason · SD flush failure · I²C bus fault, with the channel excluded and the mux reset · RTC
lost time at boot and the NTP correction · crossover · RH stripping start and stop with reason ·
disturbance detected with direction, magnitude, hold-off and cap · relay exercise · door test, with its note · OTA update started, done, refused (with the reason), abandoned, rolled back · LCD Allow update pressed · LCD drawing step over 50ms (at most once a minute) · LCD screens off at boot (no PSRAM) · LCD touch setup failed (monitor-mode write, first failure after each reset) · blowers: boost (with the spread), back to idle, full speed (with the reason) and its end · ion generator: run started (minutes), ended (on-time), cut short or skipped (with the reason), switched off mid-run · humidifier
on/off with reason · heater, cooling or humidifier ineffective · heap below the 30 KB floor (first
time) · 12V dip and recovery with the lowest value (Rev Q) · WiFi lost, and back after N seconds
(Rev Q). **12V dips, WiFi events and refused OTA uploads:** up to 10 an hour each (a dip with its recovery, or an outage with its
"back", counts as one), then one "N more this hour" line. **Guard blocks:** one event when
an asked-for load becomes blocked, another when the reason changes or clears — not every pass.
**CRC failures and timeouts:** up to 10 per channel per hour, then one "N more this hour" line.

**Layout:** `/logs/2026/2026-09-readings.csv`, `/logs/2026/2026-09-events.csv`, `/logs/hourly.csv` —
from schema 2 each name carries the schema as an `-sN` suffix (e.g. `hourly-s2.csv`), so every file
holds one schema only. Names do not sort in time order (`2026-11-readings-s2.csv` sorts before
`2026-11-readings.csv`): tools order log files by month, then schema.
Monthly files in year folders; a month of readings is ~12 MB. FAT32 holds no file of 4 GB or more:
a log file that would pass 3.9 GB continues in a `-p2` part file. **Every write is verified:** flushed and the file size checked before the file is closed; a
mismatch is treated as a failed write. No write may be lost without being logged. **A failed write is retried once
after a remount** — a permanent requirement, not a workaround; a recovered retry is logged and counted, not a failure. Only a second failure
sets SD FAIL. **SD SPI clock: 1 MHz**, from one constant used by every mount and remount —
logging needs very little bandwidth. **During a card outage,** events, readings rows and pending hourly
rows are kept in RAM and written in order when the card returns; beyond that the oldest are dropped
and counted. Buffer sizes are set per board — **Rev N: 60 events, 30 readings rows, 24 hourly rows**
(heap); **Rev Q: 1,000 events, 1,440 readings rows (24 hours at the steady rate), 168 hourly rows
(a week), in PSRAM** (about 0.94 MB). If PSRAM is not found at boot: an event, and Rev N's sizes in
internal RAM.

**Self-describing headers** — comment lines then a column header. A file keeps the header of the build
that created it; each boot event names its build, so later rows are still traceable.

```
# wineador readings
# schema 2
# board rev-q (channel n = mux n)
# firmware 1.0.0
# profile real
# ch0 serial 0x0AD409ED top_front    rh_gain <from build log> rh_offset <from build log>
# ...
timestamp,ch0_t,ch0_rh,...
```

Bump the schema number whenever columns change, renamed columns included; a new schema starts a new
file. **The `# board` line names the board that wrote the file:** `rev-n (channel n = mux n+2)` or
`rev-q (channel n = mux n)`. Schema 1 (B1–B4 cards) used channels 2–7; schema 2 uses 0–5.

**Wear:** every open-write-close rewrites the directory entry and FAT. **Buffer rows in RAM and
flush ~16 at a time** — ~130 writes a day instead of ~2,100. A power cut loses at most 16 rows.
**Events flush immediately.** High-endurance card.

**Retention:** ~140 MB of readings a year against a 60 GB card. Safety net only: on boot, if free
space is below 10%, delete the oldest month of readings until above 20%. Events and hourly are
never deleted. The Mac is the real archive — pull the card or download every few months.

**Clock:** timestamps from the DS3231 in **UTC**, ISO 8601 with a trailing Z; files named by UTC
month. **At boot, set the ESP32 system clock from the DS3231 with `settimeofday`, TZ UTC** — the SD
library dates files from the system clock, and left unset every file is dated 1 January 1980.
**NTP** (pool.ntp.org, time.google.com) at boot and every 24 h, in the background; control never
waits for it; after a missed sync, the next try is 1 h later. **WiFi, per board:** on Rev N it connects only for the NTP sync and turns off once the
sync has worked or after 2 minutes (it caused the SD errors and most of the heap use); on Rev Q
it stays connected and reconnects by itself, for the dashboard and OTA. **On Rev N, card writes
pause while WiFi is on**; rows and events wait in the outage buffers and are written once it is
off, so WiFi and card writes never overlap. Each sync sets the system clock and
writes the DS3231 if it is more than 2 s out; the correction is logged. Until NTP has corrected a
clock that lost time (OSF, 5.6), timestamps are marked untrusted. Local time only on the dashboard.

Flushes non-blocking and timeout-protected. An SD failure never stalls control; it is flagged.

## 5.5 Dashboard

Production dashboard, delivered in B7. B3–B5 use serial only; B6 a rough tuning dashboard. Served
by the ESP32 over home WiFi, **on the home network only** (reached as `wineador.local`); no access
from outside. **Control never depends on WiFi or the dashboard**: with WiFi down everything runs as
normal and the LCD still works.

**Two interfaces** (Rev Q):
- **LCD** on the enclosure — **landscape, 480 × 320, fixed** (no rotation setting). Which of the two
  landscape ways is up is one fixed constant, set at BP2 test 29 to suit the enclosure (build log).
  Three screens:
  - **Walk-past screen:**
    - cabinet temperature and RH, large (the mean of the healthy cabinet sensors; "--" when too few
      are healthy — fewer than 3, the 4.3 minimum — or no fresh reading for twice the sensor read rate);
    - **what it is doing, as icons** — one per active mode, so two can show at once (e.g. heating
      and humidifying): **Cooling**, **Heating**, **Humidifying**, **Drying** (RH stripping; shown
      instead of Cooling while it runs), **Standby** (nothing running), **Starting** (the boot hold,
      with its countdown, m:ss), **Updating** (an OTA update, with its progress). **Fault**
      replaces every other icon;
    - **icons show loads actually running**, not what is asked for: the heater counts as on through
      its PWM off-phase; a call held by a guard, and the relay exercise, show Standby. Priority:
      Fault, then Updating, then Starting (shown alone during the boot hold), then the running
      modes, then Standby;
    - **Fault** = the sensor requirement of 4.3 not met (including its 5-minute recovery hold), the
      plenum fan stalled (5.6, until loads resume), or the settings failing their check at boot. Its
      alert names the cause;
    - a small **BENCH** or **TEST** tag on any build that is not the cabinet build;
    - the two setpoints, small; the time; a WiFi icon (connected or not);
    - an **alert line**, only when an alert is active: the most serious one, plus "+N more" when
      there are others. **Tapping it opens the alerts screen;**
    - a **Settings button**.
  - **Settings:** target temperature (0.5°C steps) and RH setpoint (1-point steps), within the
    user bounds below, with Save and Cancel; and an **Allow update** button (below). Nothing else.
    Not available while Updating; if an update starts while settings or alerts are open, both
    close and unsaved settings are dropped (the alerts screen can still be opened while Updating).
  - **Alerts:** every active alert, most serious first (oldest first within the same rank), each
    with when it started — "14:32" today, "13 Nov 14:32" otherwise, "for 12 min" while the clock is
    not trusted. Page buttons when they don't fit. Each excluded sensor is its own alert, named by
    position. **An alert leaves the list by itself once resolved**; with none left the screen says
    "No active alerts". The list is rebuilt from the current state after a reboot. A Back button.
  - **Allow update:** opens a 5-minute window in which the dashboard may send one OTA update; the
    walk-past screen shows the window and its countdown. It closes after one update, at 5 minutes,
    or on a reboot. **An update sent outside the window is refused.** Pressing it again while open
    restarts the 5 minutes; there is no cancel. It opens with the compressor running (the update
    itself is refused then, 5.2). The countdown has its own line on the walk-past screen.
  - **Leaving a screen:** settings and alerts return to the walk-past screen after 60s without a
    touch; unsaved settings are dropped.
  - **Alerts, most serious first:** an active fault (5.6) · a 12V dip in progress (Rev Q) · heater,
    cooling or humidifier ineffective · a sensor excluded, automatic or manual · SD card failure ·
    the clock lost or not yet set (timestamps untrusted: the DS3231 lost time and NTP has not yet
    corrected it, or the RTC can't be read and there has been no NTP sync since boot). WiFi is shown only by its icon, never
    as an alert.
  - **Time:** UK local time (Europe/London, summer time included), 24-hour; "--:--" while the
    clock is not trusted (the same cases as that alert). Logs stay in UTC.
  - **Backlight:** dims to about **20%** 60s after the last touch — never off, so the walk-past
    screen stays readable; the exact level is set at BP2 test 29 (build log). A touch brings it
    back to full, and that first touch only wakes the screen.
  - **Icons and fonts** are drawn by the firmware or come from openly licensed sets, with the licence
    noted in the source.
  - **Colours:** the palette approved at stage Q4 is `lib/lcdgfx/src/lcdgfx/palette.h` in the firmware; the
    web dashboard uses the same values.
  - **Without PSRAM** (5.4's fallback) the screens stay off — cleared to black, backlight off —
    and an event is logged; control is unaffected.
- **Web dashboard** — everything else: graphs, events, log downloads, OTA updates. **It changes user
  settings only**; tuning settings are shown read-only (changed only on the B6 tuning dashboard);
  safety values are never shown.
  - **Pages:** Overview (now) · Graphs · Sensors (health per channel, and the manual exclude) ·
    Events · Settings (user settings to change, tuning read-only) · Logs (downloads) · Update (OTA).
  - **No password.** Anything on the home network can read and change user settings; every value
    is clamped to its bounds and safety values are never exposed. **OTA only inside the LCD's
    Allow update window** (above), and only per the OTA rules (5.2). Plain http — no https.
  - **Rev Q only:** the dashboard and OTA. Rev N builds stay as they are (WiFi for NTP only).
  - **One page file** holding all seven pages.
  - **Fetch only what the open page shows, and only while it is visible** (nothing while the tab
    is hidden or the phone screen is off; it catches up on return):
    - Overview and Sensors: the status every 30s; the header on every page (mode icons, time,
      alert) comes from the same reply;
    - Graphs: loaded when opened or the range changes; the hour view adds its newest points every
      30s; day, week and month refresh every few minutes;
    - Events: loaded when opened, then checked for new ones every 30s; 50 at a time, paging back
      through the card's monthly event files;
    - Settings: loaded when opened, never refreshed while open (it would overwrite an edit);
    - Logs: loaded when opened;
    - Update: the window countdown runs in the browser from the seconds left; the browser shows
      its own upload progress; after the restart it checks every few seconds until the board
      answers with its new version.
  - **Graphs:** hour and day from the readings; week and month from the hourly rows. Drawn with
    **uPlot** (MIT), a copy kept in the repo and served from flash.
  - **Weekly counts** (crossovers, RH strips, humidifier runs, disturbances) are counted from the
    last 7 days of event files at boot, then kept up live — no log change.
  - **Times** are UK local time from the board, as on the LCD, never the browser's own zone.
  - **Overview** also shows the blower speed and the ion generator's state. **Ion generator
    enable** is on Settings now (until 4.6c is built the generator stays off, and the page says
    so); the runs-per-day and minutes settings appear once 4.6c is built.
  - **Board** section on Overview: firmware version, uptime, the last restart reason, 12V now and
    its lowest today, WiFi signal strength, free internal memory, card space.
  - **Events** read as a plain sentence (e.g. "Humidifier called: RH 59.4%, below 60%"), with the
    logged text in small print beneath; the card keeps the logged text, which tools read.
  - **Door test:** a button on the Events page logs a "door test" event, with an optional note.
  - **Allow update window, at the edges:** a refused upload (wrong build, compressor running)
    doesn't use the window up, but one that started and then failed (its image check at the end)
    does — press Allow update again; an upload started inside it finishes even if the 5 minutes run
    out; settings writes are refused while Updating, as on the LCD.
  - **OTA accepts only a Rev Q cabinet build** (real profile, not a test build); a bench, test or
    Rev N image is refused before anything is written. The image carries its build identity and
    the firmware checks it.
  - **Limits:** at most **2 browser connections** at once — **a third is closed at once, never
    queued** (the web server itself would queue it, so it is allowed 3 and the firmware closes the
    third; the page retries and shows "board busy" only if that keeps happening) — and at most one long transfer (a
    download or an update) at a time; log downloads sent in pieces, read by the SD task between its
    own log writes, within the 50ms rule (3A.11) and the 30 KB internal-heap floor (5.2). **A new
    transfer is refused when free internal RAM is near the floor**, the margin set from BP2 test 39.
    Downloads run at the card's normal clock: a month of readings takes a few minutes.
  - **At most one saved dashboard change every 10s** (settings and the manual exclude): one that
    comes too soon is refused with "try again in N s" (and the wait in a Retry-After header), so a faulty page or script can't keep writing
    to flash or pausing the loop. The LCD and the serial console are not limited.
  - **Writes only as JSON** (an OTA image as a raw file), and the board grants no other website
    permission to send to it — so, with no password, a web page open elsewhere can't quietly change
    a setting.
  - **Served from the ESP32's flash, working with no internet:** small and compressed; nothing
    loaded from outside — no web fonts or chart libraries from the internet (system fonts, or
    files stored on the board).
  - **Name and logo:** the pages show **openHumidor** with the logo `docs/brand/openhumidor-logo.svg`;
    the board's network name stays `wineador.local`.
  - **Works on a phone** as well as a computer. **Same colours and icons as the LCD.**

**Display:** current temperature and RH, all six sensors · cabinet mean, min, max, spread · state:
compressor, heater, humidifier, fan PWM and RPM, interlock · graphs over hour, day, week, month ·
compressor and humidifier duty over range · time since last compressor stop and any guard blocking
· current zone · crossover count, RH stripping events, humidifier runs and disturbance detections
over the last week · active faults · sensor health per channel.

### Settings — three tiers

| Tier | Who sets it | Where | Changes |
|---|---|---|---|
| **User** | You, any time | Production dashboard | Freely, within bounds |
| **Tuning** | Established in B6 | Tuning dashboard, then fixed | Once, from measured data |
| **Safety** | Fixed in firmware | Never exposed | Only by reflashing |

A setting you would never correctly change is only a way to make a mistake.

**User settings** — persisted to NVS; firmware clamps to bounds and rejects out-of-range input. The
two setpoints keep their steps everywhere (LCD, dashboard, API, serial): a value off the step is
refused, and a stored value off the step is rounded to the nearest step at boot, with an event:

| Setting | Default | Min | Max |
|---|---|---|---|
| Target temperature (0.5°C steps) | 20°C | 15°C | 22°C |
| RH setpoint (1-point steps) | 62% | 55% | 70% |
| Heater enable | On | — | — |
| Humidifier enable | On | — | — |
| Ion generator enable (Rev Q, 4.6c) — off stops every run | On | — | — |
| Manual sensor override — force-excludes a channel for maintenance | None | — | — |
| Ion generator runs per day (Rev Q, 4.6c) | 2 | 0 | 4 |
| Ion generator minutes per run (Rev Q) — runs × minutes ≤ 10 | 2 | 0 | 5 |

**Tuning settings** — exposed on the B6 dashboard, then fixed; read-only at most in production. Starting
values run B3–B5 unchanged and are chosen to be safe, not good; **B6 replaces them**:

| Setting | Starting value | Range for B6 |
|---|---|---|
| Cooling hysteresis | 0.5°C | 0.3–1.5°C |
| Heating deadband | 1.5°C | 0.5–3.0°C |
| Crossover delay | 10 min | 5–60 min |
| Fan PWM, idle | 20% | 20–50% |
| Fan PWM, during compressor run | 40% | 20–100%, B6 step 1 |
| Fan PWM, cooling harvest start / end; profile shape | 100% / 40%, falling with the chamber–cabinet gap — 40% at convergence | B6 step 2 |
| Fan PWM, heating and its harvest | 60% | 20–100%, B6 |
| Harvest convergence margin | Chamber within 1.0°C of the cabinet mean | B6 step 2 |
| Harvest backstop — only if convergence never comes; logged | 30 min | B6 |
| Dosed control | **Off** — threshold run ends (4.5) | B6 step 2 |
| Dosing coefficient, adaptation rate, minimum dosed run | — (dosing off) | B6 step 2; minimum never below compressor minimum on |
| RH strip offset | 5% | 3–10% |
| RH strip trigger persistence | 30 min | B6 step 5 |
| RH strip temperature floor | 2°C below setpoint | 1–5°C |
| RH stripping enable | On | — |
| Humidify band | 2 points | 1–5 points |
| Humidify trigger persistence | 10 min | B6 step 8 |
| Humidify during cooling runs | Off | On/Off — B6 step 9 |
| Heater PWM cap | 100%; 1Hz period when below 100% | 0–100% |
| Heater ineffective | Chamber rise ≥0.5°C within 15 min; retry after 30 min | From B2 step 6, set in B6 |
| Cooling ineffective | Chamber fall ≥1.0°C within 15 min | B6 |
| Humidifier ineffective | Cabinet mean **vapour pressure** rise ≥ the equivalent of 0.5 points at setpoint conditions (~0.012 kPa at 62%/20°C) within 20 min; retry after 2 h | B6 step 8 |
| Disturbance detection | **Off** (4.7a) | B6 |
| Disturbance rate threshold, exclusion after load switch, hold-off, hold-off cap | — (detection off) | B6 |
| Blower PWM, idle (Rev Q, 4.6b) | Full speed until chosen at BP2 test 22 | BP2 test 22's floor to 100%, never 0; B6 |
| Blower boost on / off spread | Above 1.0°C or 3 points / below 0.5°C and 2 points for 10 min | B6 |
| Log rate, steady / active | 60s / 15s | 30–600s / 10–120s |
| Sensor read rate | 15s | 5–60s, ≤ active log rate |

Constraints: deadband must exceed the worst measured overshoot; fan PWM never 1–19%.

**Safety values** — fixed in firmware, never exposed:

| Value | Setting |
|---|---|
| Compressor minimum off / on / maximum run | 5 min / 1 min / 30 min |
| Heater minimum off / on / maximum run | 2 min / 1 min / 60 min |
| Humidifier minimum off / maximum run | 5 min / 30 min |
| Boot hold, all loads | 5 min |
| Cabinet ceiling | Setpoint + 3°C |
| Chamber ceiling | **30°C** |
| RH hard ceiling | **70%** |
| Sensor exclusion thresholds | Per 4.3 |
| Minimum healthy sensors | Chamber + 3 of 4 cabinet |
| Ion generator cap (Rev Q) | 10 min in any 24 hours |
| Sensor recovery hold | 5 min |

These are the **real profile**. The bench profile in B3 shortens the timers and must never reach
the cabinet.

**Serial console** (B3–B5, and on Rev Q through J23): commands that take a sensor channel accept
the number **0–5**, the **position name** (e.g. `top_front`) or the **probe label** (e.g. `09ED`),
and **echo what they acted on** — channel, position and probe, e.g. `ch3 = mid_rear (probe A1C5):
excluded`.

**Implementation:** static page plus JSON endpoints; the browser graphs. Serve pre-aggregated data
for wide ranges.

**The JSON endpoints are a documented API**, used by the dashboard and by tools on the Mac alike:
- **Read** — status, readings, events, hourly data, and log file downloads. Open on the LAN.
- **Write** — user settings (the manual exclude included), and a door-test marker; tuning settings only in the B6 tuning build (refused in every other build); the same bounds as
  everywhere else. **No password** (above); OTA only inside the LCD's Allow update window.
- **Documented in the repo** (`docs/api.md`), every endpoint and its JSON, with a **version number** that changes when
  any endpoint does, so tools on the Mac fail clearly rather than silently. Every reply carries
  the version; a native test fails if the document and the firmware disagree.
- **Safety settings are never exposed**, through the API or anywhere else.

## 5.6 Fault handling

No audible alarms. Faults surface on the dashboard and in the log.

| Fault | Response |
|---|---|
| CRC failures on one channel | Automatic exclusion per 4.3 |
| Chamber sensor unhealthy | Compressor and heater off until recovered + 5 min |
| Fewer than 3 healthy cabinet sensors | Compressor, heater and humidifier off until recovered + 5 min |
| Fan tach zero with PWM asserted | Compressor and heater off **immediately**; a kickstart at once, then another every 5 min while the tach reads zero; loads resume only once the tach reads normally |
| WiFi down | Control and logging continue |
| SD write failure | Control continues; flag |
| **I²C bus stuck** — a sensor holding SDA or SCL low | **Pulse mux RST via P16.** Re-initialise the ESP32's I²C driver (`Wire.end()`, `Wire.begin()`). Exclude the channel that was selected, per 4.3, and log it. Control continues if 4.3 is still met |
| Reboot | Compressor, heater and humidifier held off 5 min |
| **Heater ineffective** — commanded on, chamber temperature not rising | Most likely a tripped KSD9700 not yet reset; also K1's NO contact, Q3 or the pad. Heater off, flag, log; retry at intervals. Window and expected rise from B2 step 6 and B6 |
| **Cooling ineffective** — commanded on, chamber temperature not falling | K1's NC contact, Q2, the SSR, or the fridge. Flag and log; cooling continues to be called. Window from B6 |
| **Humidifier ineffective** — commanded on, cabinet vapour pressure not rising (RH moves with temperature; vapour pressure only with water) | Empty tank, dry wick, failed fan or Q4. Humidifier off, flag, log; retry at intervals. Window and expected rise from B6 step 8 |
| **RTC lost time** — OSF set at boot | Warn, flag, resync from NTP. Control unaffected: the boot hold applies regardless |

**Ineffective checks** run once per run, measured from the chamber reading at load start; a run
that ends inside the window gives no result; a flag clears on a later passing window.

**On every boot, check the DS3231's oscillator stop flag** — status 0x0F bit 7, **before anything is
logged**, so the first rows are marked correctly. If set, the clock
stopped while unpowered: log, flag, correct from NTP before trusting any timestamp, then clear it.

**Never use the DS3231's temperature register** — it returns garbage under automatic conversion.
The SHT40s cover all temperature measurement.

**Detecting a stuck bus:** a transaction on a selected channel fails, and a follow-up to the mux or
RTC on the main bus also fails. One sensor failing alone is a sensor fault (4.3); one that takes
the main bus with it is a bus fault.

---

# 6. BUILD AND TEST

## 6.1 Structure

Two tracks, independent until the final swap.

| Track | Scope | Status |
|---|---|---|
| **A — Cabinet** | Inkbird-controlled characterisation of the fridge | ✅ Complete — A1, A2 |
| **B — Controller** | Build and validate the controller, then swap it in | B3 next; Rev P's D7 checks outstanding |

**Track B order: all hardware first, firmware second.** B1 and B2 verified every component with
throwaway sketches on the perfboard; the Rev O PCB then replaced the off-board switching with
on-board parts, so BP tests the new parts and proves the board, and B3 builds the firmware **on
the PCB**. A failure in B3 is then a firmware fault.

| Stage | Where | Scope | Status |
|---|---|---|---|
| B1 | Bench, perfboard | Low-voltage bring-up | ✅ |
| B2 | Bench, perfboard | Mains and load hardware | ✅ |
| BP | Bench, PCB | Bring-up — new on-board parts tested, the perfboard retired | ✅ Rev N — D7 checks, BP15 and BP16 repeat on Rev P |
| B3 | Bench, PCB | Firmware — safety logic, bench profile, serial interface | ✅ |
| B4 | Bench | Soak, real profile, at least 20 hours | ✅ |
| BP2 | Bench, Rev Q | Bring-up of Rev Q, the B3 tests re-run on it, the deep SD test and a soak | Pending — after the order |
| B5 | Cabinet | Install, on Rev Q. Fixed settings, serial interface | Pending |
| B6 | Cabinet | Tuning. Settings to NVS, rough web dashboard | Pending |
| B7 | Cabinet | Production dashboard; B6 findings become defaults | Pending |

**Safety is proven on the bench, control quality is tuned in the cabinet.** Results are never
recorded here — only in the build log. Test tables use the same columns: Procedure, Pass criteria,
Status (✅ / ⚠️ / Pending).

## 6.2 Test equipment

| Item | Spec | Used for |
|---|---|---|
| 12V bulb | P21W, 1156 BA15S, 12V 21W halogen, ~1.75A | Stands in for the heater |
| 12V bulb holder | 1156 BA15S socket, flying leads | Lands in J10 pins 3–4 |
| Mains bulb | E27, 40–60W incandescent or halogen. **Not LED or CFL** | Stands in for the fridge; a filament shows SSR leakage |
| Mains bulb holder | E27, 2-core flex, fitted with a plug | Class II. Plugs into the switched extension socket; plug earth pin unconnected |
| Multimeter | Continuity and current ranges | Throughout |
| Reference thermometer | Waterproof probe. Check in crushed ice before use | KSD9700 water test |
| Bench PSU | Current limited, 12V | All bench work into J1 through the 4A fuse |
| Calibration | Table salt; magnesium chloride **bath flakes, not Epsom salt**; rigid gasketed box | Two-point sensor calibration |

**Lamp tests use the real switched extension lead** (3.3, 4.10). The lamp plugs into its socket in
place of the fridge, so the bench tests prove the harness that goes into service.

---

# TRACK A — CABINET CHARACTERISATION ✅ complete

Inkbird ITC-308 gating mains at the socket, appliance thermostat at minimum (5°C) so it always
wants to cool, Noctua fan powered independently from its USB adaptor at 100% continuously.
Probe and fan cables through the door seal; probe free in mid-cabinet air away from the slots.

| Inkbird | Value |
|---|---|
| TS target | Setpoint + ~0.9, to allow for harvest overshoot |
| CD cooling differential | 0.5 |
| HD heating differential | 15.0 — nothing on the heating socket |
| AH / AL alarms | 24.0 / 12.0 |
| PT compressor delay | 5 min |
| CA calibration | 0.0 |

Govee hygrometers sealed in a box with a Boveda pack for 24 hours, then set to read the same —
relative agreement only, for gradients.

**Limits of empty-cabinet testing:** an empty cabinet's air holds ~2g of water; loaded, the cigars
and boxes hold thousands of times that. Empty results are valid for temperature spread, cycle
timing, harvest overshoot and the hysteresis–stripping relationship; not for whether Boveda alone
holds humidity.

| Test | Procedure | Status |
|---|---|---|
| A1 Empty baseline | Empty cabinet, Inkbird at 17.9°C, four Govees, several cycles | ✅ |
| A2 Loaded baseline | Loaded, setpoint stepped down 0.5°C every few hours to 18°C, seven Govees — one per shelf level, floor, ambient — about three days, door shut | ✅ |

Findings and data are in the build log. The rules they produced are in section 4: the 3–4 point
dip target (4.1), the tight cooling hysteresis (4.5), dosed control and a
slow fan during the run (4.5b, 4.6), control sensors out of the discharge streams (B5), and the
slot discharge paths kept clear of packs and trays.

Active humidification and 24/7 circulation were originally deferred to the end of B6; both are
now in the design (2.2, 2.6a) and tuned in B6 steps 3, 8 and 9.

---

# TRACK B — CONTROLLER

## B1 — Low-voltage bring-up ✅ complete

Perfboard: ESP32 on terminal adapter, SD module, TCA9548A with six sensors, RTC, DR21A01 with
NC1/NO2 on terminals, Buck B 5V terminal, four-pin terminal per sensor, fan terminal, SSR and heater
D4184 terminals. Bucks off the board. Passives per 2.4. **Buck B needs 12V on its input to produce
5V** — the bench PSU must be on for fan and relay tests.

### B1.1 — Individually

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | ESP32 | Flash a blink sketch with serial output. Record chip, revision, flash, MAC, USB bridge | Flashes, runs, serial received | ✅ |
| 2 | I²C bus | Scan the main bus with all mux channels closed | 0x70, 0x68, 0x57 present | ✅ |
| 3 | TCA9548A | Select each channel in turn and scan | Mux acknowledges every channel; SHT40 at 0x44 on each populated channel | ✅ |
| 3a | Mux reset | `mux_reset_test.cpp`. Part 1: on every channel, select it, pulse P16 low, read the mux register, confirm the sensor is unreachable; five rounds; P16 rests high undriven. Part 2: short a spare channel's SDA to GND to lock the bus, then recover with the reset | Register 0x00 on every channel, every round. Part 2: bus locks, then frees with the fault still present | ✅ |
| 4 | SHT40 ×5 | Read serial (0x89) per channel. Heater at 20, 110 and 200mW, all three precision modes, soft reset. Label each probe | Unique serials; 20/20 CRC; heater rise scales with power | ✅ |
| 4a | Ambient SHT40, channel 7 | As test 4, channel 7 only — do not refire the heaters on 2–6 | As test 4 | ✅ |
| 5 | DS3231 | Set the clock to UTC over serial (`ds3231_settime_auto.cpp` with `set_rtc.py`). Unplug for 10 minutes and check offset and OSF. Measure drift over a week | OSF clear and time held after the unplug; drift under a few seconds a week | ✅ — drift tracking continues |
| 6 | SD module | Mount; write 1MB, read back and verify; unmount and remount ×10; 100 open-write-close appends | Zero verify errors; every remount succeeds; no append failures | ✅ |
| 7 | DR21A01 | Continuity across both poles, energised and de-energised | De-energised: COM1–NC1 closed, COM2–NO2 open. Both poles switch | ✅ |
| 8 | Fan | `fan_test.cpp`, counting raw tach edges alongside accepted pulses. Fan on the board, bench PSU on. Note PSU current at full, half and stopped | No signal → full speed. 0% → stops **and the tach reads zero**. 20% runs, with and without kickstart. ~2,200 RPM at 100% ±10%. Stall and start points found | ✅ |
| 8a | Fan PWM idle voltage | Fan powered, blue PWM wire disconnected from P25. Measure that wire to GND, then the current with it held to GND | Below 3.6V, or a pull-up weak enough that only microamps reach P25 undriven | ✅ |
| 9 | Boot state | Resistance of each pull resistor, power off; read pin states with firmware not driving them | P26, P27, P32 ~10kΩ to GND and low. P34 ~1kΩ to 3V3 and high | ✅ |

### B1.2 — All together

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 10+11 | Combined soak | Five sensors, RTC and SD, logging every 15s for 2 hours. `soak_test.cpp`, `FAN_ENABLED = false` | Near-zero CRC failures; zero SD and RTC failures; heap stable; SD latency not creeping | ✅ |
| 12 | Soak with fan | As 10+11 with the fan at 100% and all six sensors. `soak_test.cpp`, `FAN_ENABLED = true`; pre-flight checks clock, every serial and fan speed. Delete `resets.txt` first | `resets.txt` one entry; no CRC increase; heap stable; fan RPM stable | ✅ |
| 13 | Humidity calibration, two points | All six sensors above saturated table salt, then saturated magnesium chloride, each 16–24 hours. `salt_calibration.cpp`, `mgcl2_calibration.cpp`. **Rigid gasketed box**; one snug hole per sensor sealed with putty, probes only inside, putty away from the sensor element; slurry like wet sand with crystals at the surface, spread thin and wide in a dish with sides, centred under the probes; untouched throughout. Delete `/cal_*` files from the card first — the sketches append. Offsets from the steady-state, not from a period of changing temperature | Each settles and holds; gain and offset derived per serial, gains near 1 | ✅ |

### B1.3 — Standalone

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 14 | KSD9700 | Meter on continuity across the cut-out, metal face in a water bath, terminals dry, probe thermometer beside it, thermometer checked in crushed ice. Start a few degrees below the expected trip; warm in small splashes, stirring, ~1°C a minute — trip is where continuity breaks. Warm 2°C more, cool slowly with ice — reset is where it returns. Two trials, a third if they differ by >1°C. Handle by the terminals only | Trip and reset consistent; **reset above cabinet temperature** | ✅ — 35°C unit chosen |
| 15 | Buck converters | 12V in. Measure output unloaded and loaded | 4.9–5.1V on both | ✅ perfboard — bucks retired by Rev O; U8 tested at BP |
| 15a | 12V rail at full load | Everything on at once: heater (12V bulb) at 100%, four blowers, humidifier fan, plenum fan at 100%, relay energised, board powered. Meter the 12V rail at the WAGO and the PSU current | Rail within 0.3V of the PSU setting; PSU current consistent with the 2.4 budget and below the fuse rating | ✅ perfboard — repeated on the PCB at BP |
| 16 | Circulation blowers | All four on the 12V rail. Meter the current | All four run; total ~0.48A | ✅ |

## B2 — Mains and load hardware ✅ complete

Each part tested with a throwaway sketch on the perfboard, with the D4184 modules and the DR21A01
that Rev O replaced. **Bench PSU on throughout.** Steps 2, 3, 8 and 9 proved parts that are no
longer in the design; their replacements are tested at BP. Steps 1, 4–7 still stand.

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Inline fuse | Power off: fit the 4A fuse between PSU + and the 12V rail; continuity PSU + to rail. Power on: rail to GND | Continuity through the fuse; rail reads PSU voltage | ✅ |
| 2 | Heater D4184 | 12V bulb between the 12V rail and LOAD. `heater_d4184_test.cpp`: relay energised, signal off; 1, 5 and 10Hz; steady on; relay de-energised with the signal on. Meter checks before power: no short across the rail, LOAD to − open, relay COM2–NO2 open | Bulb follows each rate; no glow when off; stays off with the relay de-energised | ✅ perfboard — superseded by Rev O, BP 12 |
| 3 | SSR-buffer D4184 | SSR control input +A1/−A2 as the load, no mains. `ssr_buffer_test.cpp`: signal off/on cycles with the relay de-energised, then relay energised with the signal on. Meter red on +A1, black on −A2 | ~0V off, ~12V on; stays ~0V with the relay energised | ✅ perfboard — superseded by Rev O, BP 12 |
| 4 | Switched lead, SSR, X2 and both MOVs | Extension lead wired per 3.3 and 4.10. **First, unplugged, nothing in the socket** (open the shutters with something non-metal in the earth slot): continuity plug→socket for earth, neutral and live; plug live→L13 and socket live→LOAD; each pair of pins at plug and at socket; polarity cross-checks (plug N→socket L, plug L→socket N). Resistance, top range: L–E and N–E at the plug, L–E at the socket. X2 across the socket: capacitance ~100nF, or on resistance a reading that climbs to OL. Then lamp into the switched socket, lead into an RCD-protected wall socket; watch 30s; `ssr_lamp_test.cpp`, stepped manually | Earth and neutral continuous; live open plug→socket; live reaches L13 and LOAD; no shorts; cross-checks open; all resistances OL; X2 present. Lamp off on power-up; switches cleanly; no glow or flicker off; stays off with the relay energised; **never switches on by itself** | ✅ |
| 5 | SSR leakage | X2 fitted, SSR off, **nothing in the socket** — the worst case, as a standby load draws almost nothing. Meter rated CAT II 300V+ on AC volts, red lead in the V socket (never 10A). Open the shutters before power; extension into the wall; socket L–N; unplug at the wall | Settled reading well under 20V (a few volts expected) — the X2 holds the off-state voltage down. Prove the meter on a known live socket first | ✅ |
| 6 | Heater pad | Bulb swapped for the pad, no cut-out. 100% for 10 minutes in free air; surface temperature | Plateaus below 50°C | ✅ |
| 7 | KSD9700 in series | The 35°C unit in series with the heater, 12V bulb as load, 100% (`heater_pad_test.cpp`). Heat the cut-out's body above its trip — hairdryer, or hot water with terminals dry — then let it cool. Two cycles | Bulb off when the cut-out opens, back on when it resets | ✅ |
| 8 | Interlock with real loads | 12V bulb (KSD in series) on the heater D4184, mains lamp in the switched socket. `interlock_test.cpp`: both load signals high, relay flipped both ways twice. Watch both loads; meter each D4184 PWM input to board GND | Energised: only the heater path live. De-energised: only the SSR path live. Selected PWM input ~3.3V; **unselected input ~0V** (needs the NC1/NO2 pull-downs) | ✅ perfboard — superseded by Rev O, BP 13 |
| 9 | Humidifier D4184 | Third D4184 wired per 3.3, 12V humidifier fan between the 12V rail and LOAD, 10kΩ P33 pull-down fitted. Power off: P33 ~10kΩ to GND. Power on, firmware not driving P33: fan off. `humidifier_test.cpp`: off/on cycles, stepped manually. Meter across the fan | P33 low at boot, fan off. On: fan runs and starts cleanly, ≥11.8V across it; off: stops, ~0V. Module cool | ✅ perfboard — superseded by Rev O, BP 12 |

**Mains safety for steps 4 and 5:** wire and check continuity with everything unplugged; keep the
lamp circuit physically separate; unplug the extension at the wall before touching the mains side.

## BP — Rev P PCB bring-up

The controller moves from the perfboard to the **Rev P** PCB (Rev O's circuit, re-laid-out) (DRC and schematic parity clean, netlist
checked against 2.4 and 3.3). Rev O replaced the off-board bucks, DR21A01 and D4184 modules with
on-board parts, so BP has two jobs: **prove the board** (fab, assembly, wiring) and **test the new
parts** — Q1–Q5, U8, U9, K1, D1–D7 and the 12V path. Parts carried over from the perfboard
(ESP32, mux, SD module, RTC, sensors, fan) are not re-tested; the soak's pre-flight proves their
wiring. A failure here is a board, assembly or design fault, never a firmware one. **B3 starts on
the PCB.** Same sketches as B1/B2 unless named.

**Bench unit is Rev N** (no D7, no R17–R20, no TP7): on it **keep 12V on whenever USB is connected** — 12V on before USB in, USB out before 12V off. With that, flashing on the board is fine. Any test that cuts the 12V (B3 test 12) runs with USB unplugged first (5.1) — and the D7, R17–R20, TP7, BP15-with-D7
and BP16 checks wait for Rev P. See the build log.

**Three phases, in order:** A nothing fitted · B everything fitted, no power · C power. **Nothing is
powered until every step of B passes.** U9 and the ESP32 are socketed and go in last, after the
rails have been proven without them.

Test points: **TP1** 5V rail · **TP2** 3.3V rail · **TP3** board GND · **TP4** tach (P34) ·
**TP5** fan PWM (P25) · **TP6** +12V rail (after Q1) · **TP7** GND, beside TP1/TP2/TP6 — the meter's ground on the power side; TP3 is the ground for fan-signal work. "U1 pin n" is the ESP32 socket hole with the
ESP32 out; "U9 pin n" the DIP socket hole. K1: 11 COM, 12 NC, 14 NO. Meter: resistance on the top
range unless stated; "open" means OL.

### A — Bare board, nothing fitted

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Rails not shorted | TP6–TP3, TP1–TP3, TP2–TP3, TP6–TP1. J1 pin 1 pad to TP6 | All open (Q1 not fitted, so J1 pin 1 is isolated from TP6) | ✅ |
| 2 | Continuity spot checks | J1 pin 2 → TP3. J10 pin 1 → K1 pin 12; J10 pin 3 → K1 pin 14; J10 pin 2 → Q2 drain pad; J10 pin 4 → Q3 drain pad. J11–J15 pin 1 → TP6; J12–J15 pin 2 → TP3; J11 pin 2 → Q4 drain pad. U1 pin 19 → D7 square (cathode) pad, and U1 pin 19 → TP1 open. U9 pins 1, 4, 9, 12 → pad 2 of R17, R18, R19, R20. J2 pin 2 → TP1; J3 pin 1 → TP2; J3 pin 2 → TP3; each U1 GND pad → TP3 | Every beep as listed; the two opens open | ✅ |
| 3 | Fit check | Drop J10, a 2-way XH socket, D1/D7 (DO-201AD), a TO-220 and K1 into their holes | All seat without force | ✅ |

### B — Everything fitted, no power

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 4 | Assembly | Fit per 2.4 and 3.3, lowest parts first. **Polarity:** C1 + to the square pad; every diode's band to the square (cathode) pad; U8 by its pin offset (2.4) — the wrong way puts 12V on the 5V rail; K1 and J10 in their outlines, J10 wire openings to the left edge, screws on top; the TO-220 tabs on bare board, nothing under them. XH sockets with the slot per silk. All sockets and headers fitted; **U9, the ESP32, U2 mux and U4 SD module left out** (they go in at step 11) | Every part in its silkscreen outline and orientation | ✅ |
| 5 | Inspection | Magnifier over every joint, both sides | No bridges, no dry joints, no unfilled holes; flux cleaned around the antenna keep-out; no copper visible under any TO-220 tab | ✅ |
| 6 | Rails still not shorted | TP6–TP3, TP1–TP3, TP2–TP3 | Not near zero — readings drift while the rail capacitors charge; a few kΩ or more is a pass | ✅ |
| 7 | Resistance to TP3 | U1 pins 10, 11, 7, 8 (P26, P27, P32, P33); U9 socket pins 1, 4, 9, 12; each of Q2–Q5's gate pads; Q1 gate | GPIO pins ~10kΩ; U9 inputs ~11kΩ (1kΩ + 10kΩ); each Q2–Q5 gate ~100kΩ; Q1 gate ~10kΩ (R8). Find each gate by elimination: the source beeps to TP3 (Q2–Q5) or J1 pin 1 (Q1), the drain is the middle leg | ✅ |
| 8 | Diodes | Diode mode. D7: red on TP1, black on U1 pin 19; then reversed. Q1 body diode: red on J1 pin 1, black on TP6; then reversed. D2: red on TP3, black on TP6 | D7 0.1–0.3V, reversed open. Q1 ~0.5–0.7V (body diode conducts input → rail), reversed open. D2 reads a diode drop, not a short | ✅ |
| 9 | Cross-checks | Three sensor sockets: SCL and SDA not joined to each other or to any other socket's. J10 pin 1 to pin 3 open; pin 2 to pin 4 open; J11 pin 2 to TP3 open | All open | ✅ |

### C — Power

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 10 | First power, 12V only | **U9 and ESP32 out, nothing on J2–J15.** Bench PSU 12.0V, current limit 0.5A, through the 4A fuse into J1. Meter TP6, TP1, U1 pin 19 (to TP3), J11–J15 pin 1 to TP3. Note the PSU current. Touch U8 and Q1 after a minute | TP6 ≈ 12V (≤0.1V below the PSU); **TP1 4.9–5.1V**; U1 pin 19 up to ~0.3V below TP1; J11–J15 pin 1 ≈ 12V; PSU current a few tens of mA at most; U8 and Q1 cool | ✅ |
| 11 | Fit the modules; boot state | Power off. U2 mux and U4 SD module into their headers on the back; RTC lead into J9. U9 in its socket, pin 1 to the notch. ESP32 (diode check passed) in its headers, antenna to the top edge. Power on with no firmware driving the pins (`boot_state_test.cpp`, flashed before the ESP32 goes on). Meter P26, P27, P32, P33 and each Q2–Q5 gate to TP3; J10 pin 1 and pin 3 to TP3; TP2 | All four GPIOs and all four gates low; J10 pin 1 ≈ 12V and pin 3 ≈ 0V (K1 de-energised); no load runs; TP2 3.2–3.4V. J10 pin 3 floats unloaded and may read up to ~1V on a meter; 0V with 10kΩ to TP7 | ✅ |
| 12 | Each output alone | 12V bulb on J10 pins 3–4 (KSD in series), humidifier fan on J11, SSR +A1/−A2 on J10 pins 1–2 (no mains). `bp12_outputs_test.cpp`, stepped with n: (a) P32 high/low, meter J10 pin 1 and pin 3; (b) relay energised; 1, 5, 10Hz; steady on; relay de-energised with the signal on; (c) red on +A1, black on −A2; relay de-energised, signal off/on; then energised with the signal on; (d) P33 off/on cycles, meter across the fan | (a) 12V moves from pin 1 to pin 3 and back, relay clicks. (b) Bulb follows each rate, no glow off, stays off with the relay de-energised. (c) ~0V off, ~12V on, ~0V with the relay energised. (d) Fan off at boot; starts cleanly; ≥11.8V across it on; ~0V off | ✅ |
| 13 | Interlock with real loads | 12V bulb (KSD in series) on J10 pins 3–4, mains lamp in the switched extension socket with the SSR on J10 pins 1–2. `bp13_interlock_test.cpp`: both load signals high, relay flipped both ways twice. Meter the Q2 and Q3 gates to TP7 | Energised: only the heater bulb lights. De-energised: only the mains lamp. Both gates 4.4–5V throughout — **it is K1 alone that keeps the unselected load off** | ✅ |
| 14 | Full load and the 12V rail | Four blowers on J12–J15, everything else as 13 without the mains lamp. `full_load_test.cpp`, loads added one at a time; at step 5 (everything on) meter J1 pin 1 to pin 2 (underside), TP6 and TP1 to TP7; PSU current at each step. Touch Q1, Q3, U8, D1, D7 after two minutes at full load | Currents as B1.3.15a (~2.5A total); **J1 pin 1 to TP6 drops ≤ 0.1V** at full load (Q1 and the board's tracks — the bench PSU and leads are not the board's); TP6 ≥ 11.4V; TP1 4.9–5.1V; everything warm at most | ✅ |
| 15 | 3.3V margin | 12V only, no USB. Sketch to write (`margin_test.cpp`): WiFi connected and transmitting, SD card writing continuously, fan at 100%, reset reason printed at boot and logged on every restart. Run 10 minutes. Meter TP2 and the ESP32's 5V pin during WiFi activity | **TP2 ≥ 3.1V throughout**; no reset in the log. If it fails: D7 → 1N5820 or SB520 (2.4) and repeat | ✅ Rev N (no D7) — repeat on Rev P |
| 16 | Power in any order | (a) 12V off, USB in: ESP32 boots, serial works, TP1 near 0V, no load moves. (b) 12V on, then USB in and out twice: no reset (serial banner does not reprint), TP1 unchanged. (c) USB in, then 12V on and off: no reset; with 12V off all loads off. (d) Flash a sketch with the ESP32 on the board, once with 12V on and once off | All as listed | n/a Rev N — Rev P |
| 17 | Combined soak, 10 hours | B1.2 test 12 (`soak_test.cpp`, `FAN_ENABLED = true`, all six sensors) run for **at least 10 hours with the heater chopped at 1Hz on J10 for the first hour** (adds the heater's switching to the ground planes), all four blowers on, bench PSU on J1. The sketch's pre-flight (clock, every sensor serial, fan RPM, SD) is the wiring proof for the carried-over parts. The full-length soak is B4 step 1 | Pre-flight passes with every serial matching its B1 label; one boot only; no SD or RTC failures; heap stable; no more sensor errors during the heater hour than after | ✅ |

After BP the perfboard, the two bucks, the DR21A01 and the D4184 modules are retired. The switched
extension lead stays as the bench harness for B3 and B4.

## B3 — Firmware, on the bench

Mains lamp for the fridge, 12V bulb or heater pad for the heater. Proves the **safety logic**;
control quality is tuned in B6.

**One codebase.** Every setting lives in a single config struct; the control logic reads the struct,
never a literal. B3–B5 fill it from compile-time constants, B6–B7 from NVS.

**Bench and real profiles** — a compile-time switch:

| Setting | Real | Bench |
|---|---|---|
| Compressor minimum off / on / maximum run | 5 min / 1 min / 30 min | 30 s / 10 s / 2 min |
| Heater minimum off / on / maximum run | 2 min / 1 min / 60 min | 20 s / 10 s / 2 min |
| Humidifier minimum off / maximum run | 5 min / 30 min | 20 s / 2 min |
| Crossover delay | 10 min | 30 s |
| Boot hold | 5 min | 30 s |
| Sensor recovery hold | 5 min | 30 s |
| Fan stall — kickstart retry | 5 min | 30 s |
| RH strip / humidify persistence | 30 min / 10 min | 30 s / 30 s |
| Ineffective windows — heater, cooling, humidifier | 15 / 15 / 20 min | 1 min each |
| Ineffective retry — heater, humidifier | 30 min / 2 h | 2 min / 2 min |
| Harvest backstop | 30 min | 2 min |
| Blower back-to-idle hold (Rev Q) | 10 min | 1 min |
| Ion generator runs (Rev Q) | 24/N hours apart, 2 min | Every 30 min, 1 min, replacing the spacing and length (runs or minutes at 0, or the enable off, still mean no runs; cap unchanged: 10 min in 24 h, so the bench exercises it) |

**The bench profile must never reach the cabinet.** The active profile is printed on every boot and
status line; the bench profile logs an unmissable warning banner; a bench build is identified in
its version string.

**Status line:** one every 15 s (the sensor read rate), and at once whenever a load changes state.
Each carries the profile, test build if it is one, the boot reason, uptime and the boot-hold state
(counting down, or when it ended), so a boot that happened with USB unplugged can be read later by
plugging USB in.
The `boot` command reprints the banner and boot reason.

**Version string:** `0.1.0-stage<n>` during B3, with `-BENCH` and `-TEST` added when they apply;
`1.0.0` once B3 passes (Rev N). **Rev Q's cabinet build is `2.0.0`** (new board, schema 2), with
`-BENCH` and `-TEST` as above; the OTA identity block carries the same string.

**Tests run when their parts exist.** A test is first run at the build stage that provides what it
checks, and is **logged only once every pass criterion is met** — a test whose criteria need a later
stage (a load held off) is re-run then. **"Logged" in a pass criterion is proven once, in test 24**,
which checks that every event type reaches the SD log; earlier tests pass on the serial event.

**The 12V bulb only with USB connected.** With USB out, its cold-filament switch-on surge crashes or
freezes the ESP32; use the heater pad.

**Interface:** serial only. **Build order:** drivers from the B1/B2 sketches → config struct →
non-blocking loop with watchdog → runtime guards → sensor health → control logic, stripping,
harvest, deadband, crossover → logging → serial interface. **Guards before control logic.**

**How the tests are run — no readings typed by hand:**

| Layer | What | Used for |
|---|---|---|
| Native tests | The control logic is plain code, separate from the hardware drivers, run on the Mac with a fake clock and scripted readings (`pio test -e native`). Tests the **real** profile's timers as well as the bench's, in seconds | Tests 1, 2, 5, 7–10, 17, 19–22 |
| Scenario runner | A **test build** replaces the readings with scripted ones and drives the real outputs; `run <n>` plays a test's scenario and the firmware prints PASS/FAIL itself | Every logic test above, repeated on the board, and B4 |
| Physical | A real fault, made by hand, step by step; the firmware detects and reports it | Tests 3, 4, 6, 11, 12, 15, 16, 18 |

Scripted readings exist **only in test builds**: they are compiled out of the cabinet build, and a
test build says so on its banner and every status line, like the bench profile.

**Scripted readings respond to the loads.** On the bench nothing real warms or cools, so scenarios
run a **simple cabinet model**: heating raises the scripted chamber and cabinet temperatures at a set
rate, cooling lowers them, humidifying raises cabinet RH, and **all the time** they drift towards a
set room value, as a real cabinet leaks heat — so a strong drift can outrun a load. Loads then start, stop and harvest by the real rules. A scenario can switch a
response off on purpose — heater on, chamber not rising — to test the ineffective checks.
**Rates are test-only and set per scenario.** The default set, used for B4, follows Track A (A2) for
cooling and heating; the B4 soak sets the room value and drift per phase, so every load cycles
several times a day and reaches its real limits.
Logic scenarios may use faster rates to run in minutes. Values chosen are in the build log.

**Which checks use scripted values:** the **bus checks stay real** in a test build — CRC failures,
timeouts, sensor absent, wrong serial — so unplugging a sensor or shorting SDA still works. **Value
checks use the scripted values** — implausible reading, divergence, and every control decision.

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Guard timing | Rapid on/off demands against a clock | Min-off, min-on and max-run enforced | ✅ |
| 2 | Heater guards | Trip each heater guard in turn | Heater off within one control pass of the reading reaching the guards (readings arrive with each 15 s sweep) | ✅ |
| 3 | Interlock impossibility | Test-build scenario demands both loads at once, through the guards (4.4 — no test mode bypasses them); meter J10 pins 1 and 3. The hardware half — both signals high, K1 alone keeps the other off — was proved in BP13 | Only one load has a 12V feed | ✅ |
| 4 | Sensor failure | Unplug sensors one at a time; then swap two sensor plugs and reboot | Correct exclusion; compressor and heater off at the 3-of-4 and chamber thresholds; **humidifier keeps running with the chamber sensor failed** and stops at the 3-of-4 threshold; swapped plugs: both channels excluded for wrong serial, logged | ✅ |
| 5 | Sensor divergence | Script one sensor >5°C from the others for over 10 min | Divergence exclusion; recovery | ✅ |
| 6 | Fan failure | Unplug J2 with PWM asserted (tach reads zero); replug after at least one retry | Compressor and heater off; humidifier unaffected; a kickstart at once, then one per retry interval while the tach reads zero; loads resume once it reads normally | ✅ |
| 7 | Deadband | Sweep simulated temperature 17–23°C and back | Nothing starts inside 18.5–20.5°C; heating ends at the first reading at or above 19.0°C and cooling at the first at or below 20.0°C (at a 20°C setpoint); nothing ever runs between 19.0 and 20.0°C | ✅ |
| 8 | Crossover delay | Stop one load, immediately demand the other | Blocked for the full delay | ✅ |
| 9 | RH stripping | Simulate sustained high RH; then cooling with RH still high but vapour pressure falling to target | Cooling called, reason logged; **release follows vapour pressure** | ✅ |
| 10 | RH temperature floor | High RH at the floor | Stripping stops; reason logged | ✅ |
| 11 | Watchdog | `stall` command (test builds only) locks the main loop | Reset by the watchdog, reason shown; boot hold applies — a load demanding to run is held off | ✅ |
| 12 | Power interruption | Test build, scenario demanding heat and humidification from boot. **Heater pad** (KSD in series) on J10 pins 3–4, humidifier fan on J11. **USB unplugged first** (5.1). Cut the 12V; restore it; stopwatch from the moment it returns | Heater pad and humidifier fan both stay off for the boot hold (30 s, bench), then start. Cooling shares the same boot-hold code (native tests, test 11) | ✅ |
| 13 | Relay exercise | Trigger the maintenance cycle | Three cycles logged; both loads off throughout | ✅ |
| 14 | Profile safeguard | Boot the bench build | Banner printed and logged; profile on every status line | ✅ |
| 15 | I²C bus fault recovery | Short one sensor's SDA to GND while running | Stuck bus detected, RST pulsed, channel excluded and logged; RTC and others keep working; channel recovers when the short is removed | ✅ |
| 16 | RTC lost time | Remove the RTC cell, cut power, restore with WiFi | OSF detected; warning logged and flagged; time corrected from NTP; boot hold applies | ✅ |
| 17 | Load ineffective | Each load commanded, chamber temperature simulated not responding | Heater: off, flagged, logged, retried. Cooling: flagged and logged | ✅ |
| 18 | SD card failure | Remove the card while running | Control unaffected; failure flagged and logged to serial | ✅ |
| 19 | Humidifier guards | Simulated RH stepped below and above the band; rapid demands against a clock | Min-off and max-run enforced; on below setpoint − band, off at setpoint; no minimum on | ✅ |
| 20 | Humidifier ceiling and sensors | One simulated cabinet sensor driven to 70% with the mean still low; then cabinet sensors excluded one at a time | Off immediately at the ceiling; off when fewer than 3 cabinet sensors are healthy; the recovery hold before resuming | ✅ |
| 21 | Humidifier exclusivity | Demand humidify during a stripping run; during a temperature run with the setting off, then on | Blocked during stripping always; blocked during a temperature run with the setting off; allowed with it on | ✅ |
| 22 | Humidifier ineffective | Humidifier commanded on, simulated vapour pressure held flat | Off within the window, flagged, logged, retried at intervals | ✅ |
| 23 | Plenum fan rules (4.6) | Scenario with the idle duty set to 0% (so every start is from 0%): idle, a compressor run and harvest, a heater run and harvest; then command 10% duty | Fan running whenever compressor or heater runs; 1s kickstart at every start from 0%; harvest ends on chamber convergence, not a timer; a 1–19% command is refused | ✅ |
| 24 | Logging (5.4) | Run 30 min with loads cycling, triggering each event type in 5.4 (the earlier B3 scenarios can supply them); then read the card on the Mac. Disturbance detection exempt (off until B6). CRC failure by a test-build-only command that corrupts one checksum; RTC lost time by pulling the cell, as test 16. Card made nearly full with dummy files from the Mac | **Every event type in 5.4 present**; files and folders as 5.4, named by UTC month; header lines including profile and each serial's gain and offset; readings flushed ~16 rows at a time, events at once; RH columns = gain × raw + offset per the build log's table; file dates follow the system clock, never 1980 — files written before NTP corrects a lost clock carry that clock's month, with time trusted 0; **loop time logged — maximum per log interval**; with the card made nearly full, the oldest month deleted at boot | ✅ |

## B4 — Bench soak

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Bench soak | **Test build, real profile timers**, at least 20 hours. Heater pad, SSR control input on J10 1–2 with **no mains** (the SSR switching mains was proven in B2; mains switching noise is tested with the compressor in B5), blowers and humidifier fan connected. The **soak scenario** feeds scripted readings with real timings so every load cycles: repeating cooling demand, heating demand, RH high enough to strip and low enough to humidify, and idle spells — each load several times over the day, running into its real minimum-off, maximum-run and crossover limits | No resets; heap stable; near-zero CRC failures on the real sensors (read and logged alongside the scripted values); no SD failures — retried writes logged and counted, and more than an occasional retry investigated, not accepted; SD latency not creeping; **every load ran the scenario's expected number of times and no guard was ever breached** (checked from the events file); loop time maximum steady | ✅ |

**A firmware fix found by the soak** that touches only the SD writing is proven by an **SD stress
test**, not a full re-run, provided nothing else changed: a test build writes large readings
batches back-to-back — first without the fix, to reproduce the fault and get a baseline rate, then
with it, for at least as many writes — plus a card pulled for a few minutes, with every buffered
row and event reaching the card in order.

## BP2 — Rev Q bring-up

Rev Q (3A) sits between B4 and B5, and B5 runs on it. BP2 has three jobs: **prove the board and
its new parts**, **re-run the firmware tests on it**, and **prove what Rev N couldn't** — the SD
card with WiFi always on, the heap floor at all times, and a clean recovery from a supply dip. A
failure in phases A–D is a board, assembly or design fault; the firmware is already proven by B3
and B4.

**Power order:** USB powers nothing on Rev Q, so **USB and 12V are safe in any order**; flashing
and the serial monitor need **12V on**. Bench PSU 12.0V through the 4A fuse into J1.

**Sketches:** one per test, `bp2_NN_name` (e.g. `bp2_15_boot_state`), each built and flashed on its
own. Where a test says "as BP test n", use that test's steps with Rev Q's pins and test points;
items Rev Q does not have (D7, the board's underside) are skipped.

**Five phases, in order:** A as delivered · B hand-soldered parts fitted, no power · C power and
hardware · D system · E firmware and soak. **Nothing is powered until every step of B passes.** U9
is socketed and goes in after the rails are proven without it.

Test points per 3A.12. Meter ground TP7 on the power side, TP3 for signal work. Meter: resistance
on the top range unless stated; "open" means OL.

### A — As delivered (JLCPCB SMD parts fitted, nothing hand-soldered)

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Inspection | Magnifier over the module, every SMD part, the FPC and SD sockets | No bridges, tombstones, missing or rotated parts; polarity marks (polymers, diodes) match the silk | Pending |
| 2 | Rails not shorted | TP1, TP2, TP8, TP9 each to TP3; TP1 to each of TP2, TP8, TP9. J1 pin 1 pad to TP6 | Not near zero — readings drift while the capacitors charge; a few kΩ or more is a pass. J1 pin 1 to TP6 open (Q1 not fitted) | Pending |
| 3 | USB isolation, unpowered | J19's VBUS pads (A4, A9, B4, B9 — no test point: fine probe tip under the magnifier, or a USB-C breakout plugged in) to TP1, TP2, TP3, TP6 | All open — VBUS connects to nothing | Pending |
| 4 | EN, GPIO0 and the buttons | TP10 to TP2; TP11 to TP2. Then TP10 to TP3 with SW1 pressed and released; TP11 to TP3 with SW2 pressed and released | ~10kΩ each to TP2 (TP10 may read lower: U5's output has its own path to its supply). Pressed: under 1Ω; released: well above 10kΩ (10kΩ plus 3V3_RF's own reading to ground; TP10 drifts at first while C30 charges) | Pending |
| 4a | RTC cell pads | Before BT1 is fitted. Continuity: the pad marked + to R71's cell-side pad; the other pad to TP3. Meter the cell's own tabs on DC volts and mark the positive one. Then lay the cell on its footprint | + pad beeps to R71, − pad to TP3, and not the other way round; the cell's positive tab (by meter) sits over the + pad. Any other result: stop — do not fit the cell | Pending |

### B — Hand-soldered parts fitted, no power

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 5 | Assembly | **First snap off both assembly rails and file the nibs flat** — J1 overhangs the left edge by 0.4mm and won't sit flat with the rail on. Then fit the hand-soldered parts (3A.2), lowest first: test points, the DIP socket, XH headers, J10, J1, Q1, U8, K1, BT1 last — 35 parts in all: J1, J10, six 2-way and nine 4-way XH headers, J23, K1, U8, the U9 socket, Q1, BT1, twelve test loops. **J1's ground pin joins the ground planes solidly: it needs more heat** (every other hand-soldered pin has thermal relief). **Polarity:** BT1 + to the pad proven in test 4a; U8 by its outline; Q1, J1, J10 and the XH headers per the silk. **U9 left out**. R95–R98 not fitted | Every part in its outline and orientation | Pending |
| 6 | Inspection | Magnifier over every hand-soldered joint | No bridges or dry joints; flux cleaned around the antenna keep-out | Pending |
| 7 | Rails still not shorted | As test 2 | As test 2 | Pending |
| 8 | Resistance to TP3 | U9 socket pins 1, 4, 9, 12; each AO3400A gate (Q2–Q7, Q11) | U9 inputs ~11kΩ (1kΩ + 10kΩ); Q2, Q4, Q5 gates ~100kΩ; **Q3 and Q11 gates ~50kΩ** (R14 or R33 in parallel with the other heater gate's chain through R10 and R32); Q6 gate ~10kΩ (R35 and the ion pull-down); Q7 gate to TP2 ~10kΩ (the blower pull-up) | Pending |
| 9 | Diodes | Diode mode. Q1 body diode: red on J1 pin 1, black on TP6; then reversed. D2: red on TP3, black on TP6 | Q1 ~0.5–0.7V, reversed open. D2 a diode drop, not a short | Pending |
| 10 | LCD cable direction | Cable fitted to J20 (bare contacts facing the board) and the LCD, nothing else connected. the LCD module's pin 1 (VCC) to TP1; its pin 2 (GND) to TP3 | Short the meter leads first and note their reading. Each a few ohms at most above that (a 30cm ribbon conductor is about 0.5Ω, plus FB1 and two contacts). Open or high: stop — the cable or J20 is reversed | Pending |
| 11 | Cross-checks | Each sensor connector: SCL and SDA not joined to each other or to any other connector's. J10 pin 1 to pin 3 open; pin 2 to pin 4 open | All open | Pending |

### C — Power and hardware

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 12 | First power, 12V only | **U9 out, LCD and SD card out, nothing on J2–J18.** PSU current limit 0.5A. Meter TP6, TP1, TP2, TP8, TP9. Note the PSU current. Touch U8, U2–U4 and Q1 after a minute | TP6 ≈ 12V; **TP1 4.9–5.1V; TP2, TP8, TP9 each 3.23–3.37V**; current tens of mA; nothing warm | Pending |
| 13 | EN and GPIO0, 12V only | No USB. Meter TP10, TP11 | Both above 3.0V | Pending |
| 14 | USB isolation, powered | (a) 12V off, USB in from the Mac: meter TP1, TP2, TP8, TP9, TP6. (b) 12V on, USB out: meter J19's VBUS pad to TP3 (as test 3) | (a) All under 0.1V; no device seen by the Mac. (b) Under 0.1V | Pending |
| 15 | First flash | 12V on, USB in. Flash `bp2_15_boot_state` (written for Rev Q: prints the reset reason and leaves every load pin undriven) — over USB, using BOOT and reset if the automatic entry fails | Flashes; serial banner on J23 (3.3V USB-serial cable, 3A.5) | Pending |
| 16 | Boot state | Power off. U9 in its socket, pin 1 to the notch. Power on, `bp2_15_boot_state` running. Meter GPIO4–7 and GPIO17 (at their pull-down pads) and each gate to TP3; J10 pin 1 and pin 3; TP6 to TP12 with four blowers on J12–J15 | GPIO4–7, GPIO17 and every gate except Q7 low; J10 pin 1 ≈ 12V, pin 3 ≈ 0V (K1 de-energised); **blowers at full speed, TP6 to TP12 ≥ 11.5V; ion output off**; no other load runs | Pending |
| 17 | Loads held in reset | Hold SW1. Meter each gate to TP3; TP6 to TP12 | Q2–Q6, Q11 gates under 0.1V; blowers ≥ 11.5V | Pending |
| 18 | Each output alone | As BP test 12 (heater pad on J10 3–4, humidifier fan on J11, SSR +A1/−A2 on J10 1–2, no mains), sketch ported to the Rev Q pins. Then the ion generator (TFB-YD12102) on J16: before fitting it, meter its black lead to its case and to its needles; then GPIO17 off/on | As BP test 12. Ion generator's black lead open to case and needles; runs when on, off when off | Pending |
| 19 | Interlock with real loads | As BP test 13, Rev Q pins | As BP test 13 — K1 alone keeps the unselected load off | Pending |
| 20 | Full load and the 12V rail | Four blowers on J12–J15, heater pad on J10 3–4, SSR input on J10 1–2 (no mains), humidifier fan on J11, ion generator on J16, plenum fan on J2. `bp2_20_full_load`, loads added one at a time (heater with K1 energised; the SSR off — the interlock); PSU current at each step. With everything on and WiFi connected and transmitting: meter J1's + pin to TP6 (12V), TP6 and TP1 (5V) to TP7, across R21/R22, and C12's + pad to TP7. Touch Q1, Q3, Q11, U8, D1 after two minutes | **J1 + to TP6 drops ≤ 0.1V**; TP6 ≥ 11.4V; TP1 4.9–5.1V; **R21/R22 drop ≤ 2.0V** — record it: U8's input current = drop ÷ 3.4Ω; **C12 + above 9.5V**; everything warm at most | Pending |
| 21 | 3.3V under WiFi | 12V only. Sketch: repeated WiFi connects and transmits (small packets to a host on the home network), SD writing, fan at 100%. Meter on min-hold: TP2, then TP8, 10 minutes each. If a scope is available, look at TP2 and TP8 for ringing | **TP2 ≥ 3.2V**; TP8 ≥ 3.2V; no reset; no oscillation (if seen: remove C20 or C28, bench fix) | Pending |
| 22 | Blower voltage | Four blowers on. `bp2_22_blowers`: duty stepped 100% → 0% in 10% steps, typed command per step. Meter TP6 to TP12 at each; listen | 100%: ≥ 11.5V; 50%: about 6V; no ticking or whine at any step. **Record the duty-to-voltage table** (build log) | Pending |
| 23 | Blower overshoot | Peak-hold across the blowers (diode + 1µF + meter from TP6 to TP12). Blowers at 50%; reset with SW1 ten times — after each reset they come up at full speed, and `g` sets 50% again | Peak no higher than TP6 + 0.5V | Pending |
| 24 | Hardware watchdog | Sketch toggling GPIO1 from its loop, with a `stall` command that stops the toggling. Stopwatch from `stall` | One reset 1.12–2.4s after `stall`, then a normal restart. With GPIO1 set to an input (both pulls off) instead: no reset | Pending |
| 25 | Flashing with the watchdog fitted | R26 fitted. Flash the watchdog sketch over USB, while it runs, three times | All three complete | Pending |
| 26 | SD power switch | Card in. Sketch: mount, write, set the SD lines to inputs, GPIO39 high; meter J22 VDD (C53) to TP3; then GPIO39 to an input; remount, write | Off: under 0.1V. On: 3.23–3.37V (switch-on within about 5ms: scope only); remount and write succeed; no reset; TP8 dips less than 0.1V below its idle reading through the switch-on (min-hold) | Pending |
| 27 | 12V sense | PSU at 12.0V, then 11.0V. Read GPIO8 against the meter on TP6 | Reading follows the meter; **record the scale factor** (build log); after it, within 0.1V at both | Pending |
| 28 | Sensors and spare channels | All six sensors on J3–J8. Then one sensor moved to J17, then J18 | Every serial on its mux channel (3A.7); each read with CRC good; J17 and J18 read the moved sensor on channels 6 and 7 | Pending |
| 29 | LCD and touch | LCD on J20 (test 10 passed). Sketch: test pattern at 13.3MHz, then 20MHz as a trial; touch coordinates printed; backlight dimmed 100% → 10%; GPIO48 pulsed; I²C1 scanned, with the touch chip's ID registers (0xA8, 0x9F, 0xA3) printed; the screen then left untouched for over 40s, watching the failed-touch-read count, then touched again. **Landscape check:** an UP arrow drawn and a touch in each corner; choose which landscape way is up for the enclosure. Meter I²C1 SDA and SCL at R69/R70 to TP3, idle | Pattern correct, no tearing or noise; touch coordinates follow a finger; backlight dims; GPIO48 resets both the LCD and touch (both re-initialised after it); **I²C1 idle at 3.3V, not 5V**; RTC still read on I²C1; the scan finds 0x38 (touch) and 0x68 (RTC); the touch chip's ID reads as an FT6336 (0xA8 = 0x11, 0x9F = 0x26, 0xA3 = 0x64 per the maker's code); no failed touch reads while idle, and the first touch afterwards reads normally; **the arrow up and each corner touch reported at that corner in the chosen landscape way — record the choice** (build log) | Pending |
| 30 | RTC cell | Board powered 48 hours. Meter BT1 + to TP3. Then 12V off overnight; restore | Cell 2.8–3.2V (record with the date and TP8, build log; above 3.2V: 1MΩ across C49, 3A.8); time kept across the night, oscillator-stopped flag clear | Pending |

### D — System

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 31 | No USB | 12V only, no USB, the firmware's normal build (bench profile), **loads unplugged** (sensors and plenum fan only — this build switches loads by itself). Run 1 hour; then attach the J23 monitor cable and read the status line | Boots and runs; one boot only (uptime from the status line) | Pending |
| 32 | USB and monitor never restart it | Running, 12V on. (a) USB in and out five times. (b) 3.3V USB-serial cable on J23 pins 1–3 (no VCC wire): monitor opened and closed ten times; cable plugged and unplugged five times | No restart — uptime continues, no boot banner; console text on J23 | Pending |
| 32a | Monitor cable with the board off | Cable on J23, 12V off and USB out. Meter across R94 | Under 2.8V (under 0.6mA). TP2 is not a criterion: it reads above zero in this state | Pending |
| 32b | Power-up with the cable attached | Cable still on J23; 12V on | Boots normally, once; console text appears; a key typed in the monitor is received | Pending |
| 33 | Power in any order | (a) USB in, then 12V on and off twice. (b) 12V on, USB in and out twice. (c) With 12V off and USB in: no load moves | No damage; boots normally each time 12V comes on; (c) as stated | Pending |
| 34 | Fast power cycling | 12V off and on 20 times at short, irregular intervals (from under a second to a few seconds) | Boots every time; every load quiet through boot; no hang. Afterwards R38–R40 still read 3.3Ω across the three | Pending |
| 35 | Bulb surge | **USB out.** The 21W 12V bulb on J10 3–4 (K1 energised), switched on 50 times by `bp2_35_bulb` (on 2s, off 3s, after `g`). Run twice: first with the hardware watchdog paused (the sketch's default), so a freeze shows as a freeze; then with `hwdog on`, as the firmware runs | No freeze — the failure Rev N showed (build log). Any reset is clean and the board restarts (the bulb's cold surge can outlast the hold-up; the heater pad has none) | Pending |
| 36 | Supply dip | A short dip on the 12V (PSU output switched off and straight back on, then the lead pulled and replugged) while running | Either rides through, or a clean reset ending in a normal boot — never a hang | Pending |

### E — Firmware and soak

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 37 | B3 re-run | Every B3 test on Rev Q (every pin and bus moves), with the Rev Q exceptions below | As B3 | Pending |
| 38 | Deep SD test | Test build, **WiFi connected all the time**: at least 20,000 large readings batches back to back; then the card pulled for a few minutes and replaced | **Zero first-try failures** — the stress build's own retry never power-cycles the card, so this is the card unaided; every buffered row and event written in order after the pull. With the normal build, a card pull shows one power-cycle event, then SD FAIL, then recovery when the card is back | Pending |
| 39 | Heap floor | Throughout tests 38 and 40: lowest free internal heap from the readings, WiFi connects and reconnects included (router switched off and on twice); with a month's log download and a second browser polling; and with an OTA upload on its own (5.5 allows one long transfer at a time). Also the server, long-transfer and SD task stack margins, the loop's worst pass time during a download against idle, and the "third closed" count in normal use. **Set the margin** for refusing a new transfer near the floor (5.5; build log) | **Never below 30 KB** | Pending |
| 40 | Soak | As B4 test 1, on Rev Q with WiFi always on | As B4 test 1 | Pending |
| 41 | Blower and ion rules (4.6b, 4.6c) | Test build, bench profile. Blowers on J12–J15, ion generator on J16; other loads unplugged. A cabinet-model scenario forces a cabinet spread above 1.0°C, then below 0.5°C; meter TP6 to TP12 throughout (needs test 22's idle set). Then leave it running through several ion runs, metering J16 | Boost at once on the wide spread; idle again after the 1-minute hold of low spread; full speed in a forced fault. Ion runs at its bench times, ~12V on J16 while on; after 10 minutes of on-time in 24 hours no further run starts (the cap), logged as skipped | Pending |

**Rev Q exceptions to the B3 tests:**
- **Test 11 (watchdog):** with GPIO1 toggling, `stall` is caught by the **hardware watchdog**
  first (1.12–2.4s). Run it twice: as written (one reset 1.12–2.4s after `stall`, reason POWERON —
  3A.11 — then the boot hold), and with the hardware watchdog paused (GPIO1 an input, both pulls
  off) to prove the 5s software watchdog, its own reason shown.
- **Test 16 (RTC lost time):** the cell is soldered (3A.8). A test-build command sets the DS3231's
  EOSC bit, which stops its oscillator once it is on the cell; cut the power for a minute, restore
  it with WiFi; the command clears the bit afterwards. Same pass criteria. Test 24's lost-time event
  is made the same way.
- **Test 24 (logging):** the two Rev Q events are required. **12V dip:** PSU turned down to
  10.5V for about 5s, then back to 12.0V — a dip event, then its recovery with a lowest reading
  near 10.5V and a duration near 5s. **WiFi:** the router switched off for about a minute, then
  on — a WiFi lost event, then back after about that many seconds.
- **The 12V-bulb rule (B3) does not apply:** Rev Q runs the bulb with USB out (test 35).

## B5 — Install in the cabinet

Real profile, compile-time constants, serial interface. On Rev Q. Blocked on BP2.

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Confirm the real profile | Boot and read the banner | Real profile, not bench | Pending |
| 2 | Enclosure mains | The enclosure wired per 4.10, **lead unplugged, nothing in the socket, PSU terminals covered**: B2.4's continuity, polarity cross-check and OL-resistance checks from the lead's plug to the enclosure socket, plus lead L → SSR L13, lead L → PSU L, lead N → PSU N, lead E → socket E. Then B2.5's leakage reading at the enclosure socket with the SSR off. Then the mains lamp in the socket, `ssr_lamp_test.cpp` | As B2.4 and B2.5; the 12V rail comes up from the IRM-60-12ST (TP6 ≈ 12V); socket labelled "wineador only" | Pending |
| 3 | Connect the fridge | Fridge plug into the enclosure's switched socket; mains lead into the wall. Fridge flex not modified | — | Pending |
| 3a | Cable entry | Per 4.10: inspect, drill the hump, gland, route all cables, seal; remove every cable from the door seal | Sealed entry; door gasket closes fully | Pending |
| 4 | Swap in the controller | Remove the Inkbird; connect the controller | — | Pending |
| 4a | Blowers and humidifier | Fit the four blowers per 2.2 (no opening through the panel); humidifier on the floor, tank filled to the line, water-trap tube into it; drain blocked | Blowers run; plenum sealed apart from the slots | Pending |
| 5 | Sensor positions | Fit each sensor where 2.3 (channel roles) says, **all cabinet sensors out of the plenum discharge streams and the blower streams**. Record any position that had to move in the build log | — | Pending |
| 6 | First switch | Let the controller call cooling | Compressor starts; no chatter; no display flicker when off; **never starts by itself** — if it does, see 2.5 | Pending |
| 6a | First humidify | Let the controller call the humidifier | Fan runs; cabinet RH rises; stops at setpoint | Pending |
| 7 | Guard behaviour | Force rapid demand changes | Min-off respected; no short-cycling | Pending |
| 8 | Short soak | 24 hours | At least as good as the A2 loaded baseline; **near-zero CRC failures and no resets with the compressor switching** (the mains noise test B4 leaves to B5) | Pending |

## B6 — Tuning, in the cabinet

Settings move to NVS; a rough tuning dashboard exposes every setting.

**Ground rules:** change one thing at a time; hold each change a full day; record every change in
the build log — value, when, what was observed; compare against the B5 baseline, not memory.

**Tuning order** — each step depends on the ones above it; the goal throughout is the 3–4 point
dip:

| Step | Variable | Depends on | Aim |
|---|---|---|---|
| 1 | Fan PWM during the run | Nothing | Shallowest dip while the cabinet stays mixed — the main depth lever |
| 2 | Run dosing and harvest profile | Fan | Shortest runs that still let harvest reach setpoint |
| 3 | Plenum fan at idle — slow or stopped | 1, 2 | The blowers keep the cabinet mixed; find the lowest idle duty, or a stop, that leaves the spread acceptable without scrubbing the wet plate |
| 4 | Deadband and crossover delay | Measured overshoot from 2 | Deadband wider than the worst overshoot; crossover covering settling time |
| 5 | RH stripping thresholds | A settled baseline from 1–4 | Trigger only on genuinely abnormal RH |
| 6 | Disturbance detection | Normal rates from 1–5 | Catch every door opening, never a normal cycle |
| 7 | Heater — deadband, PWM cap, harvest | Cold weather | Recover without overshooting into a cooling call |
| 8 | Humidifier — band, persistence, ineffective window | A settled baseline from 1–5. | Measure the RH rise rate with the humidifier on; hold RH within the band with the fewest runs; stripping never triggered by humidifier overshoot |
| 9 | Humidify during cooling runs — experiment | 8 | Cabinet mean dip with the setting off and on, several cycles each. Adopt only if the dip shrinks measurably; **if adopted, repeat step 2** — the dose–dip relationship changes |

If a later step disturbs an earlier one, go back to the earliest affected and work forward.
Detailed procedures are written at the start of B6 from the B5 baseline.

### The mapping test — automated, step 2

Dedicated test firmware steps through run length (e.g. 1½, 2, 3, 4, 5, 6 min) × run fan PWM
(e.g. 20, 40, 60, 100%), in **random order**, each combination **at least three times**, logging
one row per cycle: start temperature and ambient, run length and fan used, temperature drop,
harvest overshoot, cabinet mean dip, vertical spread at the trough, time to next demand. All
guards bind. ~24 combinations × 3 at ~15 cycles a day is about five days. The file is fitted to
give the dosing function and answers the short-versus-long question: try several run lengths to
find the knee below which harvest no longer reaches setpoint. Requires the ambient sensor.

### The door test — step 6

Open the door for 10s, 30s and 60s, several times each, at different points in the cycle including
shortly after a run; in a warm and a cool room if the season allows. Set the detection threshold
between the door rate and the fastest normal rate, with margin both sides.

### The fan speed comparison — step 1

Compare cabinet mean dip and vertical spread at several run fan speeds. Less air over the plate
means less stripping; the blowers now carry the mixing, so the spread should hold even at a slow
run fan — if it widens, the blower placement needs attention before the fan speed does.

### Decisions at the end of B6

| Question | Decided by |
|---|---|
| Can the 3–4 point target be met? | Steps 1 and 2 |
| Can the plenum fan stop at idle? | Step 3 |
| Does humidifying during cooling runs shrink the dip? | Step 9 |

## B7 — Production

Production dashboard per 5.5; B6 settings become NVS defaults. The control logic does not change.

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Dashboard | Exercise every display and setting | All per 5.5 | Pending |
| 2 | Bounds | Out-of-range values for every setting | Clamped; rejected | Pending |
| 3 | Defaults | NVS defaults against the B6 findings | Match | Pending |
| 4 | Persistence | Change settings, cut power, restore | Settings survive | Pending |
| 5 | Final soak | One week unattended | Comparable to the B6 baseline | Pending |
| 6 | OTA (Rev Q) | **Loads unplugged; Josh's go before each flash** (the cabinet build switches loads by itself). Allow update on the LCD, then from the dashboard: a cabinet build; a bench, a test and a Rev N image; an upload outside the window; one with the compressor running (cabinet model, or a forced state in a test build); an upload stopped part way (a curl command that sends part of an image, then waits); a corrupt image (`tools/make_bad_image.py`); the **rollback test image** — its own build only (`revq_rollback_test`, version `2.0.0-ROLLBACK-TEST`, never the default), which holds the loads off and stops toggling so it resets before it is marked good. **Sent by OTA only**: if it finds no older image to fall back to (e.g. flashed by USB by mistake), it holds every load off and says so rather than resetting over and over. Record the worst loop pass during an update | The cabinet build installs, restarts, passes the boot hold and is marked good. Every other case refused with its reason and nothing written; the stopped upload abandoned after 60s; the corrupt image refused at its check, nothing changed; the rollback test image rolls back by itself to the previous image, logged and shown on the Update page | Pending |

---

## 6.3 Still to buy

| Item | Status |
|---|---|
| Rev Q PCB, JLCPCB Standard assembly (file set B4, **parts list as of 13 Nov with U5 MAX823SEUK+T**) | At order: in the placement preview check U5's pin-1 marker at the printed triangle; recheck stock and Basic/Extended of the 40 part numbers; edge rails added by JLCPCB on the **left and right edges only**, the right rail not joined beside J22 (8–24mm from the top) — remark and drawing in `jlcpcb/ORDER-NOTES.md`; **ENIG** finish; vias plugged ("Tented" not offered; every via is covered by mask in the files); select "Confirm Parts Placement"; layer order F_Cu, In1 (ground), In2 (12V/ground), B_Cu |
| Spare LCD ribbons, 14-way 0.5mm, 30cm: one "reverse", one "same-side" | With the board order |
| Rev Q hand-soldered parts, Farnell UK (3A.2) | **K1 now — low stock**; the rest with the board order |
| USB-serial cable for J23, 3.3V logic (3A.2) | With the board order |
| Enclosure | To Rev Q's final layout (3A.12) |
| Trays | After final chamber depth is confirmed |

## 6.4 What the logging must answer

- Temperature spread across probes — target under 1°C; whether bottom-front lags
- Compressor duty cycle at real room temperature
- Humidifier duty cycle, and how often RH stripping triggers — the water loop's balance
- How often the cabinet drops below setpoint in winter, and the heater's duty cycle