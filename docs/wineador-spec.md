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
| Speed | 450 RPM @ 20% PWM to 2,200 RPM |
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
the firmware.
Sensors sit out of their streams (B5).

## 2.3 Sensors

**SHT40 ×6** — sealed probe, 2m cable, bare wire ends. Five in the cabinet on mux channels 2–6, one
ambient on channel 7.

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

**TCA9548A** — 8-channel I²C mux, 0x70, board HW-617. **Power VIN from 3.3V, never 5V** — a
5V-powered mux would reference channel logic to 5V and destroy all six sensors. Module's own 10kΩ
pull-up on RST retained.

### I²C pull-ups

| Bus segment | Existing | Action |
|---|---|---|
| Master (ESP32 ↔ mux + RTC) | DS3231 carries 4.7kΩ; the mux board carries 10kΩ (≈3.2kΩ together) | Fit nothing |
| Each mux channel (2–7) | 10kΩ inside each SHT40 probe; none on the mux board | Fit nothing |

Bus **25kHz**: 40µs bit period against ~1.7µs rise time on a 2m run. Fallback if a channel proves
unreliable: 4.7kΩ in parallel on that channel only. Do not fit pre-emptively.

### Channel roles

| Channel | Position | Where | Role |
|---|---|---|---|
| 2 | Top front | Top level, front half — nearer the door | Control |
| 3 | Bottom front | Bottom level, front half | Control |
| 4 | Mid front | Middle level, front half | Control |
| 5 | Mid rear | Middle level, rear half — nearer the plenum wall | Control |
| 6 | Chamber | In the plenum, halfway down, near but not touching the cooling plate | Safety limit and harvest termination — never control |
| 7 | Ambient | Outside the cabinet, away from the fridge's warm parts and the controller enclosure — see below | Dosing feedforward, analysis, disturbance direction. Never control |

Cabinet sensors (ch 2–5) sit **out of the plenum discharge and blower streams**, at least ~10cm from
the door and from the plenum wall, about 20mm off the side liner in their printed mounts, each with a baffled shroud to deflect
air streams off the probe.

Channels 0 and 1 unused.

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

| Item | Spec |
|---|---|
| ESP32-WROOM-32 DevKit ×3 | 38-pin, USB-C, 4MB flash, WiFi + BT. **Must have a diode between USB and its 5V pin** — check each unit before use (3.3). **Every unit modified before use: two pull-ups (3.3)** |
| Terminal adapter | 38-pin narrow-hole, no mounting holes |
| SD module | 3.3V SPI |
| SD card | 64GB high-endurance, FAT32, **MBR partition table** |
| DS3231 RTC | HW-084. I²C 0x68 clock, 0x57 EEPROM. LIR2032 cell |
| PSU | **Mean Well IRM-60-12ST** — 12V 5A encapsulated, screw terminals, inside the enclosure. Mains design pending (4.10) |
| On-board 5V | Traco **TSR 3-2450N** switching regulator, 12V → 5V 3A, on the PCB (U8) |
| Controller PCB | **Rev P**, 108.7 × 96.0mm, 4-layer. All 12V loads switched on the board; full parts list in the board notes |
| Enclosure, glands | Order after panel and controller finalised. Minimum ~200 × 150 × 80mm. Carries a BS 1363 socket for the fridge (Pro Elec 9143) and an M16 gland (Multicomp PP001702) for the mains lead |
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
| Sensor, ch2 to ch7 — six, XH | J3–J8 | 4 | 3.3V, GND, SCL, SDA |
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
| 2 | Top front | 3.3V rail | Board GND | Mux SC2 | Mux SD2 |
| 3 | Bottom front | 3.3V rail | Board GND | Mux SC3 | Mux SD3 |
| 4 | Mid front | 3.3V rail | Board GND | Mux SC4 | Mux SD4 |
| 5 | Mid rear | 3.3V rail | Board GND | Mux SC5 | Mux SD5 |
| 6 | Chamber | 3.3V rail | Board GND | Mux SC6 | Mux SD6 |
| 7 | Ambient | 3.3V rail | Board GND | Mux SC7 | Mux SD7 |

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

**The dip** is the fall in the mean RH of the healthy cabinet sensors (ch 2–5) from its pre-cycle
baseline to the trough. The sensors sit out of the plenum and blower streams (B5 step 5), so no
weighting is applied.

The fridge's own thermostat never acts once the controller is in charge.

## 4.2 Control source

The controller averages the four cabinet sensors, channels 2–5. The chamber sensor is excluded: it
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
| Divergence — cabinet sensors (ch 2–5) only, and only while ≥3 are healthy | >5°C from the median of the healthy cabinet sensors for 10 min |
| I²C bus fault | The channel selected when the main bus locked — see 5.6 |

Recovery: 10 consecutive good plausible reads — and, after a divergence exclusion, all 10 within
5°C of the median. Both logged. The chamber and ambient sensors get every check except divergence:
the chamber reads plenum air by design (4.2), the ambient the room. **At boot** a sensor is healthy
from its first good, plausible read with the right serial; the boot hold covers the first minutes.

| Requirement | Threshold |
|---|---|
| Chamber sensor (ch 6) | Must be healthy |
| Ambient sensor (ch 7) | Not required — failure degrades dosing to feedback only |
| Cabinet sensors (ch 2–5) | At least 3 of 4 healthy |

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
show the count on the dashboard.

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
and the room–cabinet gap (channel 7), delivers it, compares the result against target, and nudges a
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
| Idle | Minimum PWM, or stopped — B6 step 3 |
| Compressor on | Set in B6 step 1 — likely slow, to reduce stripping, limited by mixing |
| Cooling harvest | Profile — fast at first, slowing towards setpoint |
| Heater on | Must be running |
| Heating harvest | Elevated, until the chamber sensor approaches cabinet air |
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
| 20% | Minimum reliable, ~450 RPM |
| 20–100% | Normal operating range |

**Kickstart:** any transition from 0% to running ramps to 100% for 1 second, then drops to the
target duty. Required at boot before any other startup step, on recovery from a tach-stall fault,
and on any commanded transition from 0% — including every restart from an idle stop, if B6 adopts
one.

## 4.7 RH stripping

Running the compressor condenses moisture on the plate, which the water trap returns to the
humidifier tank. The **upper half of RH control**; the humidifier (4.7b) is the lower half. The
humidifier's off point sits below the stripping trigger, so the two are never called together.

| Condition | Value |
|---|---|
| Trigger | Mean cabinet **RH** ≥ setpoint + offset (default 5%), **sustained** — persistence set in B6 step 5 |
| Release | Mean cabinet **vapour pressure** ≤ that of setpoint RH at setpoint temperature — 62% at 20°C is 1.45 kPa |
| Temperature floor | Stop if any healthy cabinet sensor falls below setpoint − 2°C |

RH from the healthy cabinet sensors, channels 2–5; never the chamber sensor.

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

RH from the healthy cabinet sensors, channels 2–5; never the chamber sensor. All 4.4 humidifier
guards apply unchanged.

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
| Board | `esp32dev` |
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
```

`monitor_rts` and `monitor_dtr` at **1** stop the monitor resetting the board when opened or closed —
essential for long runs. On the DevKit, EN is pulled low only when RTS is on and DTR off; at 0 the
monitor turns DTR off first, leaving RTS on alone for a moment, which resets the board. A custom
serial script must likewise clear RTS before DTR. Pin the platform to a release tag so a rebuild years later matches. Use a
data-capable USB cable. If auto-reset fails: hold BOOT, tap EN, release BOOT, flash. Close the
monitor before uploading. On Rev P, flash and monitor with the ESP32 on the board, USB and 12V in any
order (2.4). **Rev N** has no D7: **keep 12V on whenever USB is connected** — 12V on before USB in, USB out before 12V off. With that, flashing on the board is fine. Any test that cuts the 12V (B3 test 12) runs with USB unplugged first.

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
- Watchdog enabled, **5 s** timeout
- **Heap monitoring:** take the baseline after all initialisation completes, and never sample
  during a file write
- **Weekly maintenance task:** exercise the interlock relay, three full cycles with both loads off —
  each cycle 1 s energised, 1 s released, ending on the cooling side.
  Log it as an event

**Minimum job each pass, regardless of any other failure:** evaluate compressor, heater and
humidifier guards — the 70% RH ceiling included — and turn off any load whose guards are
unsatisfied. Turning a load off must never depend
on a sensor read, a log write or the network.

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
| Hourly | Min, max and mean per hour, each channel and derived value | Forever — what the dashboard graphs from |

**Readings row:** timestamp · per-channel temperature and RH · ambient temperature and RH · cabinet
mean, min and max temperature · cabinet mean RH · active control value · fan
PWM · fan RPM · compressor state · heater duty · humidifier state · interlock position ·
per-channel error flags. Cabinet aggregates cover channels 2–5 only.

**Events row:** timestamp · type · detail. Types: compressor on/off with reason · heater on/off ·
interlock change · harvest start and end · guard blocks · CRC failures and timeouts per channel ·
sensor excluded or recovered · fan tach stall · settings changed with old and new values · boot,
with reason · SD flush failure · I²C bus fault, with the channel excluded and the mux reset · RTC
lost time at boot and the NTP correction · crossover · RH stripping start and stop with reason ·
disturbance detected with direction, magnitude, hold-off and cap · relay exercise · humidifier
on/off with reason · heater, cooling or humidifier ineffective.

**Layout:** `/logs/2026/2026-09-readings.csv`, `/logs/2026/2026-09-events.csv`, `/logs/hourly.csv`.
Monthly files in year folders; a month of readings is ~12 MB.

**Self-describing headers** — comment lines then a column header:

```
# wineador readings
# schema 1
# firmware 1.0.0
# profile real
# ch2 serial 0x0AD409ED top_front    rh_gain <from build log> rh_offset <from build log>
# ...
timestamp,ch2_t,ch2_rh,...
```

Bump the schema number whenever columns change; a new schema starts a new file.

**Wear:** every open-write-close rewrites the directory entry and FAT. **Buffer rows in RAM and
flush ~16 at a time** — ~130 writes a day instead of ~2,100. A power cut loses at most 16 rows.
**Events flush immediately.** High-endurance card.

**Retention:** ~140 MB of readings a year against a 60 GB card. Safety net only: on boot, if free
space is below 10%, delete the oldest month of readings until above 20%. Events and hourly are
never deleted. The Mac is the real archive — pull the card or download every few months.

**Clock:** timestamps from the DS3231 in **UTC**, ISO 8601 with a trailing Z; files named by UTC
month. **At boot, set the ESP32 system clock from the DS3231 with `settimeofday`, TZ UTC** — the SD
library dates files from the system clock, and left unset every file is dated 1 January 1980.
**NTP** (pool.ntp.org, time.google.com) at boot and every 24 h, in the background — WiFi stays
connected and reconnects by itself; control never waits for it. Each sync sets the system clock and
writes the DS3231 if it is more than 2 s out; the correction is logged. Until NTP has corrected a
clock that lost time (OSF, 5.6), timestamps are marked untrusted. Local time only on the dashboard.

Flushes non-blocking and timeout-protected. An SD failure never stalls control; it is flagged.

## 5.5 Dashboard

Production dashboard, delivered in B7. B3–B5 use serial only; B6 a rough tuning dashboard. Served
by the ESP32 over home WiFi, LAN only.

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

**User settings** — persisted to NVS; firmware clamps to bounds and rejects out-of-range input:

| Setting | Default | Min | Max |
|---|---|---|---|
| Target temperature | 20°C | 15°C | 22°C |
| RH setpoint | 62% | 55% | 70% |
| Heater enable | On | — | — |
| Humidifier enable | On | — | — |
| Manual sensor override — force-excludes a channel for maintenance | None | — | — |

**Tuning settings** — exposed on the B6 dashboard, then fixed; read-only at most in production. Starting
values run B3–B5 unchanged and are chosen to be safe, not good; **B6 replaces them**:

| Setting | Starting value | Range for B6 |
|---|---|---|
| Cooling hysteresis | 0.5°C | 0.3–1.5°C |
| Heating deadband | 1.5°C | 0.5–3.0°C |
| Crossover delay | 10 min | 5–60 min |
| Fan PWM, idle | 20% | 20–50% |
| Fan PWM, during compressor run | 40% | 20–100%, B6 step 1 |
| Fan PWM, cooling harvest start / end; profile shape | 100% / 40%, falling evenly | B6 step 2 |
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
| Humidifier ineffective | Cabinet mean RH rise ≥0.5 points within 20 min; retry after 2 h | B6 step 8 |
| Disturbance detection | **Off** (4.7a) | B6 |
| Disturbance rate threshold, exclusion after load switch, hold-off, hold-off cap | — (detection off) | B6 |
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
| Sensor recovery hold | 5 min |

These are the **real profile**. The bench profile in B3 shortens the timers and must never reach
the cabinet.

**Implementation:** static page plus JSON endpoints; the browser graphs. Serve pre-aggregated data
for wide ranges.

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
| **Humidifier ineffective** — commanded on, cabinet RH not rising | Empty tank, dry wick, failed fan or Q4. Humidifier off, flag, log; retry at intervals. Window and expected rise from B6 step 8 |
| **RTC lost time** — OSF set at boot | Warn, flag, resync from NTP. Control unaffected: the boot hold applies regardless |

**On every boot, check the DS3231's oscillator stop flag** — status 0x0F bit 7. If set, the clock
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
| B3 | Bench, PCB | Firmware — safety logic, bench profile, serial interface | In progress |
| B4 | Bench | Soak, real profile, ~24 hours | Pending |
| BP2 | Bench, next board | Bring-up of the next board (SMD) and repeat of the B3 tests it affects — defined once the board is in the spec | Pending |
| B5 | Cabinet | Install, on the next board. Fixed settings, serial interface | Pending |
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

**The bench profile must never reach the cabinet.** The active profile is printed on every boot and
status line; the bench profile logs an unmissable warning banner; a bench build is identified in
its version string.

**Status line:** one every 15 s (the sensor read rate), and at once whenever a load changes state.
Each carries the profile, test build if it is one, the boot reason, uptime and the boot-hold state
(counting down, or when it ended), so a boot that happened with USB unplugged can be read later by
plugging USB in.
The `boot` command reprints the banner and boot reason.

**Version string:** `0.1.0-stage<n>` during B3, with `-BENCH` and `-TEST` added when they apply;
`1.0.0` once B3 passes.

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
rate, cooling lowers them, humidifying raises cabinet RH, and otherwise they drift back towards a
set room value. Loads then start, stop and harvest by the real rules. A scenario can switch a
response off on purpose — heater on, chamber not rising — to test the ineffective checks.
**Rates are test-only and set per scenario.** The default set, used for B4, follows Track A (A2).
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
| 7 | Deadband | Sweep simulated temperature 17–23°C | Neither load runs 18.5–20.5°C | Pending |
| 8 | Crossover delay | Stop one load, immediately demand the other | Blocked for the full delay | Pending |
| 9 | RH stripping | Simulate sustained high RH; then cooling with RH still high but vapour pressure falling to target | Cooling called, reason logged; **release follows vapour pressure** | Pending |
| 10 | RH temperature floor | High RH at the floor | Stripping stops; reason logged | Pending |
| 11 | Watchdog | `stall` command (test builds only) locks the main loop | Reset by the watchdog, reason shown; boot hold applies — a load demanding to run is held off | ✅ |
| 12 | Power interruption | Test build, scenario demanding heat and humidification from boot. **Heater pad** (KSD in series) on J10 pins 3–4, humidifier fan on J11. **USB unplugged first** (5.1). Cut the 12V; restore it; stopwatch from the moment it returns | Heater pad and humidifier fan both stay off for the boot hold (30 s, bench), then start. Cooling shares the same boot-hold code (native tests, test 11) | ✅ |
| 13 | Relay exercise | Trigger the maintenance cycle | Three cycles logged; both loads off throughout | ✅ |
| 14 | Profile safeguard | Boot the bench build | Banner printed and logged; profile on every status line | ✅ |
| 15 | I²C bus fault recovery | Short one sensor's SDA to GND while running | Stuck bus detected, RST pulsed, channel excluded and logged; RTC and others keep working; channel recovers when the short is removed | ✅ |
| 16 | RTC lost time | Remove the RTC cell, cut power, restore with WiFi | OSF detected; warning logged and flagged; time corrected from NTP; boot hold applies | ✅ |
| 17 | Load ineffective | Each load commanded, chamber temperature simulated not responding | Heater: off, flagged, logged, retried. Cooling: flagged and logged | Pending |
| 18 | SD card failure | Remove the card while running | Control unaffected; failure flagged and logged to serial | Pending |
| 19 | Humidifier guards | Simulated RH stepped below and above the band; rapid demands against a clock | Min-off and max-run enforced; on below setpoint − band, off at setpoint; no minimum on | Pending |
| 20 | Humidifier ceiling and sensors | One simulated cabinet sensor driven to 70% with the mean still low; then a second cabinet sensor excluded | Off immediately at the ceiling; off when fewer than 3 cabinet sensors are healthy; 5 min hold before resuming | Pending |
| 21 | Humidifier exclusivity | Demand humidify during a stripping run; during a temperature run with the setting off, then on | Blocked during stripping always; blocked during a temperature run with the setting off; allowed with it on | Pending |
| 22 | Humidifier ineffective | Humidifier commanded on, simulated RH held flat | Off within the window, flagged, logged, retried at intervals | Pending |
| 23 | Plenum fan rules (4.6) | Scenario: idle, a compressor run and harvest, a heater run and harvest; then command 10% duty | Fan running whenever compressor or heater runs; 1s kickstart at every start from 0%; harvest ends on chamber convergence, not a timer; a 1–19% command is refused | Pending |
| 24 | Logging (5.4) | Run 30 min with loads cycling, triggering each event type in 5.4 (the earlier B3 scenarios can supply them); then read the card on the Mac | **Every event type in 5.4 present**; files and folders as 5.4, named by UTC month; header lines including profile and each serial's gain and offset; readings flushed ~16 rows at a time, events at once; RH columns = gain × raw + offset per the build log's table; system clock set from the RTC at boot (file dates correct); **loop time logged — maximum per log interval**; with the card made nearly full, the oldest month deleted at boot | Pending |

## B4 — Bench soak

| # | Test | Procedure | Pass criteria | Status |
|---|---|---|---|---|
| 1 | Bench soak | **Test build, real profile timers**, ~24 hours. Heater pad, mains lamp on the SSR, blowers and humidifier fan connected. The **soak scenario** feeds scripted readings with real timings so every load cycles: repeating cooling demand, heating demand, RH high enough to strip and low enough to humidify, and idle spells — each load several times over the day, running into its real minimum-off, maximum-run and crossover limits | No resets; heap stable; near-zero CRC failures on the real sensors (read and logged alongside the scripted values); no SD failures, latency not creeping; **every load ran the scenario's expected number of times and no guard was ever breached** (checked from the events file); loop time maximum steady | Pending |

## BP2 — Next board bring-up

The next board (SMD; `next-board-changes.md`) sits between B4 and B5. Its tests are written when
the board is added to the spec: a bring-up like BP for its new parts, then every B3 test its changes
affect, and a short soak. B5 runs on it.

## B5 — Install in the cabinet

Real profile, compile-time constants, serial interface. On the next board. Blocked on BP2.

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
| 8 | Short soak | 24 hours | At least as good as the A2 loaded baseline | Pending |

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
| 8 | Humidifier — band, persistence, ineffective window | A settled baseline from 1–5. If RH overshoots the band with the shortest useful run time, try the fan at part speed through Q4 (25kHz PWM, kickstart at 100%; find the stall floor on the bench first, with margin, since there is no tach). If adopted, 2.6a, 4.7b and the dashboard change | Measure the RH rise rate with the humidifier on; hold RH within the band with the fewest runs; stripping never triggered by humidifier overshoot |
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

---

## 6.3 Still to buy

| Item | Status |
|---|---|
| Mean Well IRM-60-12ST PSU, BS 1363 socket (Pro Elec 9143), M16 gland (Multicomp PP001702), 13A plug and flex | Enclosure build (4.10) |
| Enclosure | Layout is final (Rev P, 108.7 × 96mm, 20mm standoffs, K1 15mm tall, J10 14mm) |
| Trays | After final chamber depth is confirmed |

## 6.4 What the logging must answer

- Temperature spread across probes — target under 1°C; whether bottom-front lags
- Compressor duty cycle at real room temperature
- Humidifier duty cycle, and how often RH stripping triggers — the water loop's balance
- How often the cabinet drops below setpoint in winter, and the heater's duty cycle

## 6.5 V2 notes

Not part of the MVP. Nothing here changes the current design or its tests. Revisit once B7 is
complete.

**Controller**
- ESP32-S3-DevKitC-1 **N8R2** in place of the ESP32-WROOM-32 DevKit. It has more pins, and every one
  can be an output. N8R2 rather than N16R8: octal PSRAM (R8) uses GPIO 35–37.
  - D7 replaced by an ideal-diode controller (e.g. LM66100) in an SMD revision — restores the ESP32's 5V headroom.
  - Avoid for general I/O: 0, 3, 45, 46 (boot strapping; 45 must stay low at boot); 19/20 (native
    USB; 20 is pulled up at boot); 43/44 (serial); 38/48 (RGB LED, depending on board version).
  - Diode check on **both** USB ports before fitting (3.3).
  - Retest: BP 6–11 on the new board.
- Header pins on every spare GPIO, with 3.3V, 5V and GND alongside, so the board doubles as an
  expandable test board. Strapping pins labelled and kept off the main header. A spare mux channel
  on its own XH socket. The SPI bus plus a spare chip-select on a header. ADC1 pins (GPIO 1–10)
  grouped and labelled.

**Board**
- Switching for new loads: more IRLZ44N channels like Q2–Q5, PWM-capable. Relays only for mains or AC loads and for the heat/cool
  interlock. UVC door switch in series with the lamp supply.

**Additions**
- VOC sensors on I²C, using the mux's spare channels (SC0, SC1).
- UVC lamp: one output, plus a door-switch input so it runs only with the door shut, interlocked
  in hardware.
- Plasma ion generator: one output.
- Tank-level sensing for the humidifier: a float switch on a spare input.
- Choose UVC and ioniser parts first: 12V DC or mains decides the switching.
- Before buying either: choose ozone-free parts (185nm UVC lamps and most ionisers produce ozone),
  and keep UVC off labels and wrappers.
- Check UVC and ioniser power against the PSU, fuse and switching.