# Wineador controller — Rev Q schematic: full design report (revision 15)

**Date:** 13 November 2026. Section 15 lists every change since revision 1. **Status:** schematic complete; revisions 9 and 10 swap module pins at the layout stage; revision 11 changes no pin of the module; revision 12 removes the unfitted LCD pin header J21; **revision 13 moves 18 signals to different module pins for the new board layout (section 15), approved by the spec engineer.** Revision 14 corrects one statement about the LCD socket J20 (lower contacts, not both faces) and changes no part, pin or net. **Revision 15 replaces the supervisor U5 (TPS3828-33 → MAX823SEUK+T) after an independent datasheet review; no pin, net, footprint or copper changes.** Board laid out and routed; not yet ordered.
**Purpose:** one self-contained text record of the Rev Q design for a final check before layout.
Nothing else needs to be read alongside it. It replaces the proposal (revisions 1–3) and the
schematic review notes (revisions 1–5) as the description of what is drawn.

**How this report was made.** Sections 13 and 14 (every part with its connections, and every net
with its pins) are generated from the same data the KiCad schematic is generated from, so they
cannot drift from the drawing. KiCad's electrical rules check on the schematic reports
**0 violations**; KiCad's own netlist was compared with that data: **218 parts, 134 nets, no
differences**. Sections 1–12 are written by hand from it.

**What is and isn't verified.** Connections are checked as above. Component values are design
choices. Figures marked *calculated* or *simulated* have not been measured; section 11 lists them.

---

## 1. What Rev Q is

A new board replacing Rev P. Same job, same 12V input, same heat/cool interlock. Changes:

- **ESP32-S3-WROOM-1-N8R2 module** on the board, flashed over its own USB. No DevKit, no
  USB-serial chip, no auto-reset circuit.
- **I²C mux, RTC and micro-SD socket on the board** as chips, not modules.
- **LCD** (lcdwiki 4.0" ST7796 MSP4031, capacitive touch) on a 14-way FPC.
- **Three separate 3.3V regulators**, a **supervisor with hardware watchdog** on EN, and
  **hold-up capacitance** behind D1.
- **Blower speed control** and an **ion generator output** (new).
- Mostly SMD, assembled by JLCPCB on the top side; a short list of through-hole parts
  hand-soldered by Josh, also on top.

## 2. Requirements (brief section 3) and how each is met

| Requirement | How it is met |
|---|---|
| Runs with no USB | The USB socket's data pins connect only to GPIO19/20 (through 22Ω) and one ESD array; its CC pins to two resistors; its VBUS pins to nothing. Nothing on the USB side touches EN or GPIO0. EN: 10kΩ pull-up. GPIO0: 10kΩ pull-up |
| USB in/out and serial monitor never restart it | Plugging USB in or out cannot reset it: no electrical path from USB to EN. A serial monitor on the **native USB** can: the chip resets whenever the host leaves RTS on with DTR off (section 6). So the in-cabinet monitor port is **J23**, the chip's plain UART on GPIO43/44, which has no path to EN or GPIO0 — a monitor there cannot reset the board whatever the laptop does. Native USB is for flashing |
| USB and 12V safe in any order | VBUS is not connected to anything on the board. USB cannot power any rail, and nothing on the board can put a voltage on VBUS. The board is always flashed with 12V on |
| Recovers from a supply dip | Hold-up capacitors behind D1; supervisor holds EN low below 2.93V and for 200ms after; hardware watchdog pulls EN if the firmware stops toggling GPIO1 |
| No load pulses at boot or reset | Section 5 |
| 3.3V with headroom | The ESP32 has its own 1.2A low-dropout regulator from 5V (1.7V available, 0.35V typical / 0.6V maximum needed). Nothing else is on that rail |
| Designed against noise | Section 8 |
| Test points | TP1–TP7 kept, EN and GPIO0 added, plus 3V3_Q, 3V3_SENS and blower − |

## 3. Power

```
J1 XT30 ── 4A fuse (off board) ── Q1 reverse protection (D3, R8) ── D2 clamp ──► +12V
  +12V ──► K1 COM, humidifier +, ion +, blower +, 12V sense divider       [22µF ceramics ×5]
  +12V ── D1 ── 3.4Ω ──► +12V_HOLD [2 × 470µF 35V] ── U8 TSR 3-2450N ──► +5V
     +5V ──► plenum fan J2, 74HCT08 U9, LCD (through ferrite bead FB1 = LCD_5V)
     +5V ── U2 ──► 3V3_RF    ESP32 module; EN, BOOT, blower, LCD chip-select and display-reset
                             pull-ups; supervisor; USB ESD array
     +5V ── U3 ──► 3V3_Q     SD card (through switch Q10), mux, RTC, I²C pull-ups, tach pull-up,
                             touch level shifter, LCD-line ESD arrays, J23 pin 5
     +5V ── U4 ──► 3V3_SENS  the eight sensor connectors, each through 220Ω
USB VBUS ──► not connected
```

- **Q1** IRF4905 (through-hole), **D3** 15V zener gate clamp, **R8** 10kΩ, **D2** SMBJ15A clamp
  (about 24V at full pulse). As Rev P in function.
- **D1** SS54 Schottky. It stops the 5V side back-feeding 12V and isolates the hold-up capacitors
  from dips on the 12V rail.
- **Inrush limiter:** two 6.8Ω 2W resistors in parallel (3.4Ω, 4W rating) between D1 and the
  hold-up capacitors. Plug-in pulse about 3.6A, falling within about 10ms (*calculated*). In
  running: about 1V drop and 0.3W at a typical load; about 2V and 1.1W at the 5V side's 1A peak. Q1 cannot soft-start,
  because its internal diode conducts as soon as power is applied.
- **Hold-up:** 2 × 470µF 35V polymer. Roughly 20ms of ride-through at about 2W of logic load,
  about 12ms at 0.5A on 5V and about 4ms at the 1A peak (*calculated*). A longer dip ends in a reset. U8's minimum input is 6.5V; it normally sits near 10–11V.
- **U8** Traco TSR 3-2450N, 12V → 5V 3A, through-hole, as Rev P.
- **U2, U3, U4** ST LDL1117S33R, 3.3V 1.2A, SOT-223. Each has 10µF in, 10µF + 100nF out; the RF
  rail has a further 22µF at the regulator and 22µF + 100nF at the module.
- **12V sense:** 100kΩ / 15kΩ divider with 100nF, to GPIO8 (an ADC1 input). 12V reads 1.57V; at D2's 24V clamp
  level the pin sees about 3.2V, inside its limit.

**Budget (estimates):** 5V about 1.0A peak (ESP32 0.5A peak, plenum fan 0.15A, LCD 0.2A, SD
0.1–0.2A, rest about 30mA). 12V about 2.8A peak (heater 1.67A, blowers 0.48A, U8 input about
0.45A at peak, humidifier 0.12A, ion 0.04A, K1 and SSR about 0.03A). The 4A fuse stays.

## 4. ESP32-S3 pin map

Module: **ESP32-S3-WROOM-1-N8R2** (8MB flash, 2MB quad PSRAM on internal pins). **It must not be
substituted with an N16R8**, whose PSRAM takes GPIO35–37, used here for I²C0 and the mux reset.

| GPIO | Net | Function | State with the pin undriven (boot, reset, flashing) |
|---|---|---|---|
| 0 | IO0 | BOOT button, TP11 | High: 10kΩ pull-up to 3V3_RF (R27) |
| 1 | WDT | Watchdog toggle | Undriven: watchdog disabled |
| 2 | SPARE1 | Spare, to J23 pin 4 | No pull |
| 3 | — | Not connected | — |
| 4 | SSR_CTL | Cooling (SSR) | Low: 10kΩ pull-down. Load off |
| 5 | HEAT_CTL | Heater | Low: 10kΩ pull-down. Load off |
| 6 | K1_CTL | Interlock relay K1 | Low: 10kΩ pull-down. De-energised = cooling selected |
| 7 | HUM_CTL | Humidifier fan | Low: 10kΩ pull-down. Load off |
| 8 | V12_SENSE | 12V rail sense, ADC1 | — |
| 9 | LCD_RS | LCD data/command | — |
| 10 | LCD_CS | LCD chip select (SPI2) | High: 10kΩ pull-up to 3V3_RF (R76) |
| 11 | LCD_MOSI | LCD data (SPI2) | — |
| 12 | LCD_SCK | LCD clock (SPI2) | — |
| 13 | LCD_BL | LCD backlight | The LCD pulls it high: backlight on |
| 14 | I2C1_SCL | I²C1 clock: RTC and touch | 2.2kΩ pull-up to 3V3_Q |
| 15 | BLW_PWM | Blower speed | **High: 10kΩ pull-up to 3V3_RF (R36). Blowers at full speed** |
| 16 | FAN_TACH | Plenum fan tach | 1kΩ pull-up to 3V3_Q, 1µF to GND |
| 17 | ION_CTL | Ion generator | Low: 10kΩ pull-down. Load off |
| 18 | FAN_PWM | Plenum fan PWM, 25kHz | Undriven = fan at full speed (as Rev P) |
| 19 / 20 | USB_DM_MCU / USB_DP_MCU | USB D− / D+ | — |
| 21 | I2C1_SDA | I²C1 data: RTC and touch | 2.2kΩ pull-up to 3V3_Q |
| 35 / 36 | I2C0_SDA / I2C0_SCL | I²C0: mux only | 2.2kΩ pull-ups to 3V3_Q |
| 37 | MUX_RST | TCA9548A reset | High: 10kΩ pull-up to 3V3_Q |
| 38 | SD_MISO | SD data out (SPI3) | 10kΩ pull-up |
| 39 | SD_PWR_OFF | SD card power switch | Card powered. This pin has an internal pull-up at reset (about 45kΩ); R91 10kΩ holds the switch fully on against it |
| 40 | SD_SCK | SD clock (SPI3) | — |
| 41 | SD_MOSI | SD data in (SPI3) | 10kΩ pull-up |
| 42 | SD_CS | SD chip select (SPI3) | High: 10kΩ pull-up to the card's supply |
| 43 / 44 | UART0_TX / UART0_RX | Serial monitor port, to J23 pins 2 and 3 through 220Ω (R93) and 4.7kΩ (R94) | 43 prints boot text at reset; 44 has a pull-up at reset |
| 45, 46 | — | Not connected | Internal pull-downs give the correct boot state |
| 47 | CTP_INT | Touch interrupt | High: 10kΩ pull-up to 3V3_Q (R82) |
| 48 | DISP_RST | LCD reset and touch reset together | High: 10kΩ pull-up to 3V3_RF (R79) |

**GPIO43/44 are the serial monitor port (J23). They remain usable as spares when no monitor cable
is attached, with the restrictions noted. GPIO2 is the one spare with no other job, on J23 pin 4.** No
LCD MISO (the screen is never read). No I²C expander. GPIO3 is not used. On the N8R2 module GPIO47
and GPIO48 run at 3.3V; only the 1.8V modules (N16R16VA) differ.

## 5. Default states — every output with no firmware running

| Output | Chain | Default |
|---|---|---|
| SSR, heater, K1, humidifier | GPIO → 10kΩ pull-down → 1kΩ → 74HCT08 gate (other input tied to +5V) → 100Ω → MOSFET gate, 100kΩ gate pull-down. **Unchanged from Rev P**; R1–R3, R7, R9–R20 keep their Rev P numbers | **Off.** Also off if U9 is unpowered or out of its socket |
| Ion generator | GPIO17 → 10kΩ pull-down (R34) → 100Ω (R35) → Q6 gate. No 74HCT08 | **Off** |
| Blowers | GPIO15 → 10kΩ pull-**up** to 3V3_RF (R36) → 100Ω (R37) → Q7 gate. No 74HCT08 | **Full speed** |
| Plenum fan | GPIO18 direct to the fan's PWM wire, as Rev P | Full speed |
| LCD backlight | GPIO13 → 100Ω → LCD LED pin; the LCD pulls it up | On |
| SD card power | GPIO39 → 220Ω (R92) → Q10 gate; 10kΩ gate pull-down (R91); 1µF gate-to-source (C55) | On |

The load pins (GPIO4–7 and GPIO17) have no internal pull-ups and only a brief *low*
glitch at power-up. One 74HCT08 (U9), all four gates used.

**Interlock, unchanged from Rev P.** K1 (Finder 34.51.7.012.5010): coil A1 to +12V, A2 to Q5.
Contact 11 (COM) to +12V; 12 (NC) to J10 pin 1, SSR +A1; 14 (NO) to J10 pin 3, heater +. J10 pin 2
(SSR −A2) to Q2; pin 4 (heater −) to Q3 and Q11. The SSR and the heater can never both have a feed.

**Heater switch:** two AO3400A (Q3, Q11) in parallel, each with its own 100Ω gate resistor and
100kΩ pull-down, about 25mW each at 1.7A (*calculated*). **Other loads:** one AO3400A each (30V,
JLCPCB Basic). A Schottky flyback diode across every output: SSR (D8), heater (D4), K1 coil (D6),
humidifier (D5), ion generator (D9).

## 6. Circuit by circuit

**EN net — exactly these and nothing else:** U1 EN pin · R25 10kΩ to 3V3_RF · C30 1µF to GND ·
SW1 reset button to GND · U5 reset output (push-pull; its pull-up is limited to 0.4mA, so SW1 can pull it low) · TP10.

**Supervisor and watchdog, U5 MAX823SEUK+T** (Maxim / Analog Devices, the 2.93V version; it replaced the
TPS3828-33 in revision 15, section 15). Supply 3V3_RF.

| | Min | Typ | Max |
|---|---|---|---|
| Reset threshold | 2.85V | 2.93V | 3.00V |
| Reset pulse / power-up delay | 140ms | 200ms | 280ms |
| Watchdog timeout | **1.12s** | 1.6s | 2.4s |

GPIO1 → R26 (0Ω link) → WDI. A rising or a falling edge on WDI clears the watchdog. An undriven WDI
disables it (Maxim datasheet: the device then drives WDI itself; the pin must see under 10µA of
leakage and under 200pF). Leave R26 out to remove the watchdog. The manual-reset pin is unused (it
has its own pull-up). The reset output both sources and sinks; its pull-up is limited to 0.4mA,
which the datasheet gives so that another device may pull the line low, here SW1.

**BOOT:** GPIO0 · R27 10kΩ to 3V3_RF · SW2 to GND · TP11.

**USB-C, J19.** D+ and D− through 22Ω each to GPIO20 / GPIO19. CC1 and CC2 each through 5.1kΩ to
GND. ESD array U11 on D+ and D−, with its supply pin on **3V3_RF**. Shield to GND. **VBUS is not
connected.** (Revision 1 had U11's supply pin on VBUS. With USB unplugged, the S3's D+ pull-up
would then have lifted VBUS to about 2.7V through the array's upper diode.) Not addressed, and
inherent to any self-powered USB device without VBUS sensing: the S3's D+ pull-up is present
whether or not a host is attached. It is 1.5kΩ to 3.3V, so under 2mA into a powered-off host's
data line.

**Serial monitor port, J23** (1×5 header, fitted). Pin 1 GND · pin 2 TX, from GPIO43 through 220Ω
(R93) · pin 3 RX, to GPIO44 through 4.7kΩ (R94) · pin 4 GPIO2 spare · pin 5 3V3_Q. A 3.3V
USB-serial cable uses pins 1–3 only: cable GND to pin 1, cable RX to pin 2, cable TX to pin 3.
**The cable's own VCC wire is never connected.**

The cable may be attached with the board on or off. With 12V off, the cable's TX (idle at 3.3V)
feeds a small current through R94 and GPIO44's protection diode onto 3V3_RF, so TP2 reads above
zero in that state; R94 limits the current to about 0.6mA. R94 is 4.7kΩ, not higher, because
GPIO44's internal pull-up is on at reset: Espressif give it as 45kΩ typical with no minimum or
maximum, and the pin's low threshold is 0.825V at most (0.25 × 3.3V). Against 4.7kΩ a low from
the cable reaches about 0.3V at the pin plus the cable's own low level, and stays under the
threshold for any pull-up down to about 20kΩ. R93 (220Ω) protects GPIO43 against a shorted pin.

**Why the native USB can reset the chip** (Espressif technical reference manual, table 33-2): the
USB-Serial-JTAG peripheral acts on the host's two control lines. RTS on with DTR off resets the
chip; DTR on with RTS off sets a flag that makes the next reset enter download mode; the other two
states do nothing harmful. Hosts change the lines one at a time, so opening or closing a port can
pass through the reset state. There is no documented run-time switch for this, only permanent
fuses, which are not to be burned.

**Blower stage.** Blower + pins on +12V. Blower − pins joined (BLW_NEG, TP12) → L1 220µH → Q7 →
GND. D10 SS54 from Q7's drain to +12V. Across the blowers: 10µF ceramic, plus 470µF 35V polymer in
series with 3.3Ω (three 10Ω in parallel). Blower voltage ≈ duty × 12V; about 11.8V at 100%.
No feedback loop. *Simulated:* no overshoot above the supply on a reset from any speed, with one
to four blowers or none, and with the ceramic anywhere from 1µF to 10µF effective. Below about
70mA total load the voltage reads higher than duty × 12V.

**Plenum fan, J2.** As Rev P: 5V, PWM direct from GPIO18, tach with 1kΩ pull-up and 1µF.

**I²C0 — mux only.** U6 TCA9548APWR at 0x70 (address pins to GND), reset on GPIO37 with 10kΩ
pull-up, 2.2kΩ pull-ups on SDA and SCL to 3V3_Q. No pull-ups on the channels.

**Sensor connectors.** Channels 0–5 on J3–J8; channels 6 and 7 on spare connectors J17, J18. Each:
pin 1 3.3V through **220Ω** from 3V3_SENS with 100nF after it; pin 2 GND; pin 3 SCL and pin 4 SDA
each through 100Ω from the mux. One ESD array per pair of connectors (U12–U15), supply pin on
3V3_SENS. A lead shorted to ground draws 15mA and affects that sensor only. **This relies on the
SHT40 heaters never being used.** The two spare connectors also have **unfitted** positions for
10kΩ pull-ups (R95–R98), from each connector's own 3.3V feed to its SCL and SDA on the connector
side of the 100Ω resistors — to be fitted only for a sensor that has no pull-ups of its own.

**I²C1 — RTC and touch.** 2.2kΩ pull-ups to 3V3_Q. 100kHz; 50kHz is the fallback if touch
misbehaves.

**RTC, U7 DS3231SN** at 0x68. No EEPROM (0x57 is gone). Backup cell BT1 Panasonic ML-2020/H1CN
(3V, 45mAh, rechargeable, solder tabs). Charger: 3V3_Q → 1N4148W (D11) → 2.2kΩ → cell. The cell
floats at about 3.0–3.15V (*calculated*; once the cell is charged only a fraction of a microamp
flows and D11 drops about 0.2V), inside its 2.8–3.2V range. If 3V3_Q sits at the top of its
tolerance the float approaches 3.2V: measured at bring-up (section 12). Backup time: comfortably over a year (*estimate, not confirmed*).

**LCD connector J20** (14-way 0.5mm FPC, **lower contacts: the ribbon goes in with its bare contacts facing the board**). There is no pin header for the LCD (J21 was removed in revision 12):

| Pin | LCD signal | Our net | Path to the ESP32 |
|---|---|---|---|
| 1 | VCC | LCD_5V | +5V through ferrite bead FB1; 2 × 47µF + 100nF at the connector |
| 2 | GND | GND | |
| 3 | LCD_CS | LCD_CS_C | 33Ω to GPIO10 |
| 4 | LCD_RST | LCD_RST_C | 100Ω to GPIO48 |
| 5 | LCD_RS | LCD_RS_C | 33Ω to GPIO9 |
| 6 | MOSI | LCD_MOSI_C | 33Ω to GPIO11 |
| 7 | SCK | LCD_SCK_C | 33Ω to GPIO12 |
| 8 | LED | LCD_BL_C | 100Ω to GPIO13 |
| 9 | MISO | not connected | |
| 10 | CTP_SCL | CTP_SCL_5V | Level shifter Q9 to GPIO14 |
| 11 | CTP_RST | CTP_RST_C | 100Ω to GPIO48 (its own resistor) |
| 12 | CTP_SDA | CTP_SDA_5V | Level shifter Q8 to GPIO21 |
| 13 | CTP_INT | CTP_INT_C | 100Ω to GPIO47 |
| 14 | SD_CS | not connected | The LCD's SD slot is not used |

- **What the LCD module pulls its pins to** (read from lcdwiki's schematic for the module, all
  of it): CTP_SDA and CTP_SCL, 10kΩ to **VCC (5V)**. LED, 10kΩ to the module's 3.3V. SD_CS, 10kΩ
  to the module's 3.3V. **CTP_INT, CTP_RST, LCD_CS, LCD_RST, LCD_RS, MOSI, SCK: no pull-up on the
  module.** CTP_INT is driven by the touch controller, which runs from the module's 3.3V, so
  GPIO47 never sees more than 3.3V. CTP_RST and the LCD control lines are inputs to a 3.3V buffer
  (74LVC245). Only the two touch I²C lines reach 5V.
- **Touch level shifter:** the LCD pulls its touch lines to **5V** through 10kΩ. Q8, Q9 (BSS138,
  gate on 3V3_Q) keep I²C1 at 3.3V. R83, R84 add 10kΩ to LCD_5V on the cable side (5kΩ per line
  with the LCD's own). Worst-case sink on a low is about 2.5mA (*calculated*).
- **ESD arrays:** U16 (CS, LCD reset, RS, MOSI) and U17 (SCK, backlight, touch reset, touch interrupt) with
  supply pin on 3V3_Q; U18 (the two 5V touch lines) with supply pin on **LCD_5V**.
- **Cable:** the supplied 30cm FPC, no longer. **SPI speed:** design for 20MHz; 40MHz is a bench
  trial only (one ground wire among 14).
- **Pin 1 and contact side of J20 (checked against the maker's drawing, revision 14).** The footprint's pad 1 is the
  drawing's "Circuit 1", and the footprint's pads and mounting tabs match the drawing's recommended pattern. On the
  board, looking at the top face with the antenna at the top, the ribbon enters J20 from below and pin 1 is the
  left-hand contact. The part number's contact code is F = lower, so the ribbon's bare contacts must face the board.
  (LCSC's listing for the part says "top and bottom contact"; the maker's drawing is taken as correct. Contacts-down
  works either way.)
- **The LCD end (corrected in revision 15).** lcdwiki's size drawing for the MSP4031 shows the module's ribbon
  socket P2 on the back, near the top edge, marked `1` at one end and `14` at the other, with the fourteen names
  printed beside it in the same order as the pin header (1 VCC … 14 SD_CS). So the socket's pin order is the
  header's, which J20 follows. Still not stated anywhere: whether P2's contacts are on its upper or lower face.
  lcdwiki's specification says the module comes with a 30cm "reverse" ribbon (bare contacts on opposite faces at
  its two ends). So whether the supplied ribbon brings LCD pin 1 to J20's pin 1 or to its pin 14 can only be
  checked with the LCD in hand. Whatever the answer, one of the two standard ribbon types (reverse, or
  same-side) fits without changing the board.
- Before first power: continuity from the LCD's header pin 1 to our 5V test point with the cable fitted.

**Micro-SD, J22**, on SPI3. 33Ω in series with the clock. 10kΩ pull-ups to the card's supply on
CS, MOSI, MISO and the two unused data lines. 47µF + 100nF at the socket. The socket's card-detect
contact (pad 9) is not connected. **Power switch Q10**
(AO3401A) from 3V3_Q: R91 10kΩ gate to GND, R92 220Ω from GPIO39, C55 1µF gate-to-source.
- GPIO39 has an internal pull-up at reset (Espressif datasheet: enabled unless a fuse is burned).
  Against R91 it leaves the gate at about 0.6V, so Q10 is fully on (*calculated*).
- Pin as an **input**: R91 and C55 turn the card on over about 10ms (soft start).
- Pin driven **high**: gate at 3.23V, card off. The pin sources at most about 15mA into C55.
- Pin driven **low**: card on at once through R92, with no soft start.

## 7. For the firmware

- **Pins:** section 4. Mux channels 0–5 for the six sensors (were 2–7).
- **Blowers (GPIO15):** high = on, duty = speed, about 100kHz. Calibrate duty against voltage with
  all four blowers fitted. Voltage is read between TP6 (+12V) and TP12.
- **Ion generator (GPIO17):** high = on.
- **Watchdog (GPIO1):** leave as an input until the control loop is running, then change its
  level every pass. The loop must never go longer than **0.9s** between changes — including
  during WiFi reconnects, SD writes and OTA updates. (U5's shortest timeout is 1.12s and either
  edge counts, so 0.9s keeps a margin.) To pause it, set the pin back to an input.
- **SD power (GPIO39):** drive **high to cut** power. **To switch the card back on, set the pin
  to an input with its internal pull-up off** — that uses R91/C55 for the soft start. Driving it
  low switches the card on hard through R92; avoid it. Before cutting power set the four SD
  lines low or to inputs, or the card is back-fed through them.
- **Display reset (GPIO48):** resets the LCD and the touch controller together.
- **Backlight (GPIO13):** on until driven. PWM dims it.
- **SD on the second SPI controller, LCD on the first; RTC and touch on the second I²C
  controller, mux on the first.**
- **I²C0 (mux and sensors) runs at 25kHz**, as on Rev P. **I²C1 (RTC and touch) at 100kHz**,
  50kHz as the fallback.
- **SHT40 heaters: never used** (the 220Ω feeds depend on it).
- **Console:** on UART0 (GPIO43 TX, GPIO44 RX) for the J23 monitor port. Whether it is also sent to
  the native USB is a firmware choice; a monitor on the native USB can reset the chip (section 6).
- **12V sense scale:** 12V reads about 1.57V at GPIO8 (100kΩ / 15kΩ).

## 8. Noise measures in the schematic (layout measures come in Phase 3)

Separate regulators so WiFi bursts do not reach the SD card or the sensors · hold-up behind D1 and
the inrush resistor, which together also filter the 12V feed to U8 · supervisor and watchdog on
EN · 100Ω series and ESD arrays on every sensor line, 220Ω in each sensor feed · series resistors
and ESD arrays on every LCD line, filtered 5V to the LCD · ESD array on USB · flyback diodes on
every switched output · 100Ω gate resistors. Planned for layout: four layers with an unbroken
ground plane; loads and power on one edge, logic and antenna on the opposite side, sensor
connectors away from J10 and the mains side.

## 9. Connectors, buttons, test points

| Ref | What | Pins |
|---|---|---|
| J1 | 12V in, XT30PW-M | 1 +12V, 2 GND |
| J2 | Plenum fan, XH 4-way | 1 GND, 2 +5V, 3 tach, 4 PWM |
| J3–J8 | Sensors, mux channels 0–5, XH 4-way | 1 3.3V, 2 GND, 3 SCL, 4 SDA |
| J17, J18 | Spare sensors, channels 6 and 7, XH 4-way | same |
| J10 | SSR and heater, 4-way 5.08mm screw block | 1 SSR +A1, 2 SSR −A2, 3 heater +, 4 heater − |
| J11 | Humidifier fan, XH 2-way | 1 +12V, 2 − |
| J12–J15 | Blowers 1–4, XH 2-way | 1 +12V, 2 − (switched) |
| J16 | Ion generator, XH 2-way | 1 +12V, 2 − |
| J19 | USB-C | data only |
| J20 | LCD, 14-way 0.5mm FPC | section 6 |
| J22 | Micro-SD socket | |
| J23 | Serial monitor and spare, 1×5 header, fitted | 1 GND, 2 TX (GPIO43), 3 RX (GPIO44), 4 GPIO2, 5 3V3_Q |
| SW1 / SW2 | Reset / BOOT buttons | |

J9 (the old RTC connector) no longer exists.

| TP | Net | | TP | Net |
|---|---|---|---|---|
| TP1 | +5V | | TP7 | GND |
| TP2 | 3V3_RF | | TP8 | 3V3_Q |
| TP3 | GND | | TP9 | 3V3_SENS |
| TP4 | Fan tach | | TP10 | EN |
| TP5 | Fan PWM | | TP11 | GPIO0 |
| TP6 | +12V | | TP12 | Blower − |

## 10. Parts and sourcing

**Assembly: JLCPCB Standard, not Economic.** JLCPCB's page for the ESP32 module (C2913204) says
"PCBA Type: Standard Only" (checked by Josh). On Standard assembly a loading fee applies to every
part type, Basic or Extended alike; the order quote will show the final fees.

**Counts:** Extended: 15 types, 28 parts · Basic: 25 types, 151 parts · Hand-soldered: 11 types, 35 parts · Not fitted: 2 types, 5 parts.

In the table, a bare value such as "10k" or "100" is a 0603 resistor in ohms; "6.8R 2W" is a 2512
resistor; "10 1206" is a 10Ω 1206 resistor; values with a voltage ("100n 50V") are ceramic
capacitors unless marked polymer. The Basic/Extended class no longer affects fees (Standard
assembly); it is shown for reference.

| Class | Part | JLCPCB no. | Qty | References |
|---|---|---|---|---|
| Extended | BZT52C15-7-F (Diodes Inc) | C177014 | 1 | D3 |
| Extended | SMBJ15A (Littelfuse) | C83846 | 1 | D2 |
| Extended | 6.8R 2W | C49196888 | 2 | R21, R22 |
| Extended | 470u 35V polymer | C46550472 | 3 | C12, C13, C33 |
| Extended | LDL1117S33R | C435835 | 3 | U2, U3, U4 |
| Extended | ESP32-S3-WROOM-1-N8R2 | C2913204 | 1 | U1 |
| Extended | MAX823SEUK+T | C239902 | 1 | U5 |
| Extended | USB-C TYPE-C-31-M-12 | C165948 | 1 | J19 |
| Extended | SRV05-4.TCT (Semtech) | C13612 | 8 | U11, U12, U13, U14, U15, U16, U17, U18 |
| Extended | 220uH 1.16A SWRB1207S-221MT | C169380 | 1 | L1 |
| Extended | TCA9548APWR | C130026 | 1 | U6 |
| Extended | DS3231SN | C9866 | 1 | U7 |
| Extended | LCD FPC 14P 0.5mm AFC24-S14FIC-00 | C262291 | 1 | J20 |
| Extended | BSS138 | C52895 | 2 | Q8, Q9 |
| Extended | microSD TF-01A | C91145 | 1 | J22 |
| Basic | 10k | C25804 | 21 | R1, R2, R3, R7, R8, R25, R27, R34, R36, R42, R76, R79, R82, R83, R84, R86, R87, R88, R89, R90, R91 |
| Basic | 22u 25V | C45783 | 8 | C10, C11, C15, C20, C28, C34, C35, C36 |
| Basic | SS54 | C22452 | 2 | D1, D10 |
| Basic | 100n 50V | C14663 | 22 | C14, C16, C19, C23, C26, C27, C29, C31, C37, C39, C40, C41, C42, C43, C44, C45, C46, C47, C48, C49, C52, C54 |
| Basic | 10u 25V | C15850 | 7 | C17, C18, C21, C22, C24, C25, C32 |
| Basic | 100k | C25803 | 6 | R13, R14, R15, R16, R23, R33 |
| Basic | 15k | C22809 | 1 | R24 |
| Basic | 1u 50V | C15849 | 3 | C30, C38, C55 |
| Basic | Tactile switch TS-1187A | C318884 | 2 | SW1 (reset), SW2 (BOOT) |
| Basic | 0 | C21189 | 1 | R26 |
| Basic | 220 | C22962 | 10 | R45, R48, R51, R54, R57, R60, R63, R66, R92, R93 |
| Basic | 1k | C21190 | 5 | R17, R18, R19, R20, R41 |
| Basic | 4.7k | C23162 | 1 | R94 |
| Basic | 5.1k | C23186 | 2 | R28, R29 |
| Basic | 22 | C23345 | 2 | R30, R31 |
| Basic | 100 | C22775 | 27 | R9, R10, R11, R12, R32, R35, R37, R46, R47, R49, R50, R52, R53, R55, R56, R58, R59, R61, R62, R64, R65, R67, R68, R77, R78, R80, R81 |
| Basic | AO3400A | C20917 | 7 | Q2, Q3, Q4, Q5, Q6, Q7, Q11 |
| Basic | SS34 | C8678 | 5 | D4, D5, D6, D8, D9 |
| Basic | 10 1206 | C17903 | 3 | R38, R39, R40 |
| Basic | 2.2k | C4190 | 5 | R43, R44, R69, R70, R71 |
| Basic | 1N4148W | C81598 | 1 | D11 |
| Basic | BLM18PG121SN1D | C14709 | 1 | FB1 |
| Basic | 47u 10V | C96123 | 3 | C50, C51, C53 |
| Basic | 33 | C23140 | 5 | R72, R73, R74, R75, R85 |
| Basic | AO3401A | C15127 | 1 | Q10 |
| Hand-soldered | XT30PW-M | - | 1 | J1 |
| Hand-soldered | IRF4905 | - | 1 | Q1 |
| Hand-soldered | Keystone through-hole test loop | - | 12 | TP1, TP2, TP3, TP4, TP5, TP6, TP7, TP8, TP9, TP10, TP11, TP12 |
| Hand-soldered | TSR 3-2450N | - | 1 | U8 |
| Hand-soldered | 1×5 2.54mm pin header | - | 1 | J23 |
| Hand-soldered | SN74HCT08N in socket | - | 1 | U9 |
| Hand-soldered | Finder 34.51.7.012.5010 | - | 1 | K1 |
| Hand-soldered | Multicomp Pro MP012012, 4-way 5.08mm screw block | - | 1 | J10 |
| Hand-soldered | JST XH B2B-XH-A 2-way | - | 6 | J11, J12, J13, J14, J15, J16 |
| Hand-soldered | JST XH B4B-XH-A 4-way | - | 9 | J2, J3, J4, J5, J6, J7, J8, J17, J18 |
| Hand-soldered | ML-2020/H1CN | - | 1 | BT1 |
| Not fitted | 10k | C25804 | 4 | R95, R96, R97, R98 |

**Hand-soldered parts and where they come from.** Farnell UK: U8 TSR 3-2450N (4644820) · K1 Finder
34.51.7.012.5010 (1169342, being ordered now — low stock) · J10 Multicomp Pro MP012012 (4179046) ·
Q1 IRF4905PBF (8648190) · U9 SN74HCT08N (3120497, 547 in stock) in a DIP-14 socket · BT1 Panasonic
ML-2020/H1CN (4167439) · J23 Multicomp Pro 2211S-05G 1×5 header (1593414, stock not checked).
Supplied by Josh: J1 XT30PW-M, the fifteen JST XH headers, the twelve
Keystone test loops, the DIP socket.

**Footprints:** section 16. Every part has one.

## 11. Calculated or simulated, not measured

- Inrush pulse (about 3.6A, 10ms) and hold-up time (about 20ms at about 2W of logic load; shorter at higher load, section 3).
- Blower filter: no overshoot on reset.
- Heater MOSFET dissipation (about 25mW each).
- RTC cell float voltage (about 3.0–3.15V; up to about 3.2V if 3V3_Q is at the top of its tolerance) and backup time (over a year).
- Q10's gate voltage against GPIO39's internal pull-up (about 0.6V), and the 10ms soft start.
- I²C1 rise time with BSS138 (about 0.2µs) and touch-cable rise time (about 0.5µs with the added
  pull-ups, on an assumed 100–150pF of cable).
- L1: Sunlord give one rated current, 1.16A, with no separate saturation figure; peak here is
  about 0.57A.
- U8 minimum input 6.5V is from a distributor listing of the TSR 3N series, not Traco's datasheet.
- Monitor port: the low level at GPIO44 (about 0.3V plus the cable's own) and the board-off current
  through R94 (about 0.6mA). The pull-up figure behind the first is typical only.
- The 5V and 12V budgets are estimates.
- U5: Maxim's datasheet describes no stuck-reset behaviour (the fault that removed the TPS3828, section 15);
  that it recovers from a supply dip with the watchdog toggling is proven only by the bring-up test.
- U2–U4: ST say the LDL1117 is designed for ceramic output capacitors; its stability chart was not readable, so
  stability with the 54µF fitted on 3V3_RF is not confirmed on paper.
- R38–R40 take about 10W each for 1–2ms at every power-up (the blower capacitors charging); no pulse rating
  was found for the part. Estimated harmless.

## 12. Bring-up checks that depend on this design

| Check | Measure | Expect |
|---|---|---|
| Rails | TP1; TP2, TP8, TP9 | 4.9–5.1V; each 3.23–3.37V |
| USB isolation | USB in, 12V off: every rail. Then 12V on, USB out: VBUS pin at the socket | All under 0.1V and no device seen by the laptop. VBUS under 0.1V (it is not connected) |
| EN and GPIO0, 12V only | TP10, TP11 | Both above 3.0V |
| Buttons | Unpowered: TP10 to GND with SW1 pressed; TP11 to GND with SW2 pressed | Under 1Ω each when pressed; well above 10kΩ when released (10kΩ plus the rail's own reading; not open) |
| Monitor port | 3.3V USB-serial cable on J23 pins 1–3 (no VCC wire), board running: open and close the monitor ten times, plug and unplug the cable five times | Console text appears; no restart |
| Monitor cable with the board off | Cable attached, 12V off: voltage across R94 | Under 2.8V, which is under 0.6mA. TP2 is not a pass criterion here: it reads above zero |
| Power-up with the cable attached | Cable still attached, then 12V on | Boots normally, once; console text appears; a key typed in the monitor is received |
| Loads held in reset | Hold SW1: each MOSFET gate; TP6 to TP12 | Gates under 0.1V; blowers at 11.5V or more |
| Watchdog | Firmware stops toggling GPIO1 | One reset within 1.12–2.4s, then a normal restart. A USB flash completes with R26 fitted, three times running |
| Inrush resistor | Voltage across R21/R22, running, WiFi on | About 1V (0.5–1.5V); U8 input above 9.5V |
| LCD cable direction | Before first power: continuity, LCD header pin 1 to TP1; pin 2 to GND | A few ohms at most each (the ribbon is about 0.5Ω a conductor). Open or high: the ribbon is the wrong way round |
| Touch bus level | I²C1 SDA and SCL at the ESP32 side, LCD connected, idle | 3.3V, not 5V |
| Blower voltage | TP6 to TP12 at 100% and 50% duty, four blowers | 11.5V or more; about 6V. No ticking |
| Blower overshoot | Peak-hold (diode + 1µF + meter) across the blowers through ten resets from half speed | Peak no higher than the 12V rail + 0.5V |
| RTC cell | Cell voltage after 48h powered; clock after a night unpowered | 2.8–3.2V; time kept, oscillator-stopped flag clear |
| 3.3V under WiFi | TP2, meter on min-hold, repeated WiFi connects | 3.2V or more |
| Bulb surge | The 21W bulb switched 50 times, USB out | No freeze. A clean reset is possible: a cold bulb's surge can outlast the hold-up (section 3) |
| Fast power cycling | 12V off and on 20 times at short random intervals | Boots every time |
| Supply dip with the watchdog running | The watchdog sketch toggling GPIO1; 12V dipped and restored repeatedly | Rides through or resets cleanly every time; EN never stays low |

---

## 13. Every part and its connections

Generated from the schematic data. Two-pin parts read "net to net"; diodes and polarised parts
name each end; other parts list pin = net.

### A. Power

**12V input: reverse protection and surge clamp (as Rev P; Q1 through-hole)**

- J1 XT30PW-M: 1 = +12V_IN; 2 = GND
- Q1 IRF4905: 1 G = Q1_G; 2 D = +12V_IN; 3 S = +12V
- R8 10k: Q1_G to GND
- D3 BZT52C15-7-F (Diodes Inc): cathode +12V, anode Q1_G
- D2 SMBJ15A (Littelfuse): cathode +12V, anode GND
- C10 22u 25V: +12V to GND
- C11 22u 25V: +12V to GND
- TP6 test point: +12V

**Hold-up branch and 5V regulator. Inrush limiter: 2 x 6.8R 2W in parallel = 3.4R, 4W**

- D1 SS54: cathode D1_K, anode +12V
- R21 6.8R 2W: D1_K to +12V_HOLD
- R22 6.8R 2W: D1_K to +12V_HOLD
- C12 470u 35V polymer: + +12V_HOLD, - GND
- C13 470u 35V polymer: + +12V_HOLD, - GND
- C14 100n 50V: +12V_HOLD to GND
- U8 TSR 3-2450N: 1 Vin = +12V_HOLD; 2 GND = GND; 3 Vout = +5V
- C15 22u 25V: +5V to GND
- C16 100n 50V: +5V to GND
- TP1 test point: +5V

**3.3V regulators: RF (ESP32 only), Q (SD, mux, RTC, pull-ups), SENS (sensor leads)**

- U2 LDL1117S33R: 1 GND = GND; 2 VO = 3V3_RF; 3 VI = +5V
- C17 10u 25V: +5V to GND
- C18 10u 25V: 3V3_RF to GND
- C19 100n 50V: 3V3_RF to GND
- C20 22u 25V: 3V3_RF to GND
- U3 LDL1117S33R: 1 GND = GND; 2 VO = 3V3_Q; 3 VI = +5V
- C21 10u 25V: +5V to GND
- C22 10u 25V: 3V3_Q to GND
- C23 100n 50V: 3V3_Q to GND
- U4 LDL1117S33R: 1 GND = GND; 2 VO = 3V3_SENS; 3 VI = +5V
- C24 10u 25V: +5V to GND
- C25 10u 25V: 3V3_SENS to GND
- C26 100n 50V: 3V3_SENS to GND
- TP2 test point: 3V3_RF
- TP8 test point: 3V3_Q
- TP9 test point: 3V3_SENS
- TP3 test point: GND
- TP7 test point: GND

**12V rail sense to ADC (12V reads 1.57V; 24V clamp level reads 3.2V)**

- R23 100k: +12V to V12_SENSE
- R24 15k: V12_SENSE to GND
- C27 100n 50V: V12_SENSE to GND

### B. ESP32-S3, reset, boot, USB

**ESP32-S3-WROOM-1-N8R2 (do not substitute N16R8: it takes GPIO35-37)**

- U1 ESP32-S3-WROOM-1-N8R2: 1/40/41 GND = GND; 2 3V3 = 3V3_RF; 3 EN = EN; 4 IO4 = SSR_CTL; 5 IO5 = HEAT_CTL; 6 IO6 = K1_CTL; 7 IO7 = HUM_CTL; 8 IO15 = BLW_PWM; 9 IO16 = FAN_TACH; 10 IO17 = ION_CTL; 11 IO18 = FAN_PWM; 12 IO8 = V12_SENSE; 13 USB_D- = USB_DM_MCU; 14 USB_D+ = USB_DP_MCU; 15 IO3 = not connected; 16 IO46 = not connected; 17 IO9 = LCD_RS; 18 IO10 = LCD_CS; 19 IO11 = LCD_MOSI; 20 IO12 = LCD_SCK; 21 IO13 = LCD_BL; 22 IO14 = I2C1_SCL; 23 IO21 = I2C1_SDA; 24 IO47 = CTP_INT; 25 IO48 = DISP_RST; 26 IO45 = not connected; 27 IO0 = IO0; 28 IO35 = I2C0_SDA; 29 IO36 = I2C0_SCL; 30 IO37 = MUX_RST; 31 IO38 = SD_MISO; 32 IO39 = SD_PWR_OFF; 33 IO40 = SD_SCK; 34 IO41 = SD_MOSI; 35 IO42 = SD_CS; 36 RXD0 = UART0_RX; 37 TXD0 = UART0_TX; 38 IO2 = SPARE1; 39 IO1 = WDT
- C28 22u 25V: 3V3_RF to GND
- C29 100n 50V: 3V3_RF to GND

**EN net: pull-up, capacitor, reset button, supervisor -- nothing else**

- R25 10k: 3V3_RF to EN
- C30 1u 50V: EN to GND
- SW1 RESET: EN to GND
- U5 MAX823SEUK+T: 1 ~{RESET} = EN; 2 GND = GND; 3 ~{MR} = not connected; 4 WDI = WDI; 5 V_{DD} = 3V3_RF
- C31 100n 50V: 3V3_RF to GND
- R26 0: WDT to WDI
- TP10 test point: EN

**BOOT (GPIO0). And J23, the serial monitor header: 1 GND, 2 TX (GPIO43), 3 RX (GPIO44), then 4 GPIO2 spare, 5 3V3_Q**

- R27 10k: 3V3_RF to IO0
- SW2 BOOT: IO0 to GND
- TP11 test point: IO0
- J23 MONITOR GND TX RX | IO2 3V3: 1 = GND; 2 = MON_TX; 3 = MON_RX; 4 = SPARE1; 5 = 3V3_Q
- R93 220: UART0_TX to MON_TX
- R94 4.7k: UART0_RX to MON_RX

**USB-C: data only. VBUS is not connected to anything**

- J19 USB-C TYPE-C-31-M-12: A1/B1/A12/B12 GND = GND; A4/A9/B4/B9 VBUS = not connected; A5 CC1 = USB_CC1; A6/B6 D+ = USB_DP; A7/B7 D- = USB_DM; A8 SBU1 = not connected; B5 CC2 = USB_CC2; B8 SBU2 = not connected; SH SHIELD = GND
- R28 5.1k: USB_CC1 to GND
- R29 5.1k: USB_CC2 to GND
- R30 22: USB_DP to USB_DP_MCU
- R31 22: USB_DM to USB_DM_MCU
- U11 SRV05-4.TCT (Semtech): 1 IO1 = USB_DM; 2 VN = GND; 3 IO2 = not connected; 4 IO3 = not connected; 5 VP = 3V3_RF; 6 IO4 = USB_DP

### C. Loads

**Cooling: SSR control, low side. Fed only when K1 is de-energised**

- R1 10k: SSR_CTL to GND
- R17 1k: SSR_CTL to SSR_IN
- U9 SN74HCT08N in socket: 1 = SSR_IN; 2 = +5V; 3 = SSR_DRV
- R9 100: SSR_DRV to SSR_G
- R13 100k: SSR_G to GND
- Q2 AO3400A: 1 G = SSR_G; 2 S = GND; 3 D = SSR_A2
- D8 SS34: cathode SSR_A1, anode SSR_A2

**Heater, low side: two AO3400A in parallel, each with its own gate resistor. Fed only when K1 is energised**

- R2 10k: HEAT_CTL to GND
- R18 1k: HEAT_CTL to HEAT_IN
- U9 SN74HCT08N in socket: 4 = HEAT_IN; 5 = +5V; 6 = HEAT_DRV
- R10 100: HEAT_DRV to HEAT_G
- R14 100k: HEAT_G to GND
- Q3 AO3400A: 1 G = HEAT_G; 2 S = GND; 3 D = HEAT_NEG
- R32 100: HEAT_DRV to HEAT_G2
- R33 100k: HEAT_G2 to GND
- Q11 AO3400A: 1 G = HEAT_G2; 2 S = GND; 3 D = HEAT_NEG
- D4 SS34: cathode HEAT_POS, anode HEAT_NEG

**Interlock relay K1: COM +12V, NC to SSR +A1, NO to heater + (unchanged from Rev P)**

- R3 10k: K1_CTL to GND
- R20 1k: K1_CTL to K1_IN
- U9 SN74HCT08N in socket: 8 = K1_DRV; 9 = K1_IN; 10 = +5V
- R12 100: K1_DRV to K1_G
- R16 100k: K1_G to GND
- Q5 AO3400A: 1 G = K1_G; 2 S = GND; 3 D = K1_COIL
- K1 Finder 34.51.7.012.5010: 11/A1 = +12V; 12 = SSR_A1; 14 = HEAT_POS; A2 = K1_COIL
- D6 SS34: cathode +12V, anode K1_COIL
- J10 MP012012 SSR+ SSR- HEAT+ HEAT-: 1 = SSR_A1; 2 = SSR_A2; 3 = HEAT_POS; 4 = HEAT_NEG

**Humidifier fan, low side**

- R7 10k: HUM_CTL to GND
- R19 1k: HUM_CTL to HUM_IN
- U9 SN74HCT08N in socket: 11 = HUM_DRV; 12 = HUM_IN; 13 = +5V
- R11 100: HUM_DRV to HUM_G
- R15 100k: HUM_G to GND
- Q4 AO3400A: 1 G = HUM_G; 2 S = GND; 3 D = HUM_NEG
- J11 HUMID: 1 = +12V; 2 = HUM_NEG
- D5 SS34: cathode +12V, anode HUM_NEG

**Ion generator, low side, driven straight from GPIO17. Pulled down: off by default**

- R34 10k: ION_CTL to GND
- R35 100: ION_CTL to ION_G
- Q6 AO3400A: 1 G = ION_G; 2 S = GND; 3 D = ION_NEG
- J16 ION: 1 = +12V; 2 = ION_NEG
- D9 SS34: cathode +12V, anode ION_NEG

**Blowers: smoothed-PWM step-down, low side, driven straight from GPIO15. Pulled UP: undriven = full speed**

- R36 10k: 3V3_RF to BLW_PWM
- R37 100: BLW_PWM to BLW_G
- Q7 AO3400A: 1 G = BLW_G; 2 S = GND; 3 D = BLW_SW
- D10 SS54: cathode +12V, anode BLW_SW
- L1 220uH 1.16A SWRB1207S-221MT: BLW_SW to BLW_NEG
- C32 10u 25V: +12V to BLW_NEG
- C33 470u 35V polymer: + +12V, - BLW_DAMP
- R38 10 1206: BLW_DAMP to BLW_NEG
- R39 10 1206: BLW_DAMP to BLW_NEG
- R40 10 1206: BLW_DAMP to BLW_NEG
- C34 22u 25V: +12V to GND
- C35 22u 25V: +12V to GND
- C36 22u 25V: +12V to GND
- J12 BLOWER 1: 1 = +12V; 2 = BLW_NEG
- J13 BLOWER 2: 1 = +12V; 2 = BLW_NEG
- J14 BLOWER 3: 1 = +12V; 2 = BLW_NEG
- J15 BLOWER 4: 1 = +12V; 2 = BLW_NEG
- TP12 test point: BLW_NEG

**74HCT08 power (one chip, all four gates used, as Rev P)**

- U9 SN74HCT08N in socket: 7 GND = GND; 14 VCC = +5V
- C37 100n 50V: +5V to GND

**Plenum fan J2: 5V, 25kHz PWM, tach (as Rev P)**

- J2 FAN: 1 = GND; 2 = +5V; 3 = FAN_TACH; 4 = FAN_PWM
- R41 1k: 3V3_Q to FAN_TACH
- C38 1u 50V: FAN_TACH to GND
- TP4 test point: FAN_TACH
- TP5 test point: FAN_PWM

### D. Sensors and RTC

**I2C mux TCA9548A at 0x70 on I2C0 (GPIO35 SDA, GPIO36 SCL). No channel pull-ups**

- U6 TCA9548APWR: 1 A0 = GND; 2 A1 = GND; 3 ~{RESET} = MUX_RST; 4 SD0 = CH0_SDA; 5 SC0 = CH0_SCL; 6 SD1 = CH1_SDA; 7 SC1 = CH1_SCL; 8 SD2 = CH2_SDA; 9 SC2 = CH2_SCL; 10 SD3 = CH3_SDA; 11 SC3 = CH3_SCL; 12 GND = GND; 13 SD4 = CH4_SDA; 14 SC4 = CH4_SCL; 15 SD5 = CH5_SDA; 16 SC5 = CH5_SCL; 17 SD6 = CH6_SDA; 18 SC6 = CH6_SCL; 19 SD7 = CH7_SDA; 20 SC7 = CH7_SCL; 21 A2 = GND; 22 SCL = I2C0_SCL; 23 SDA = I2C0_SDA; 24 VCC = 3V3_Q
- C39 100n 50V: 3V3_Q to GND
- R42 10k: 3V3_Q to MUX_RST
- R43 2.2k: 3V3_Q to I2C0_SDA
- R44 2.2k: 3V3_Q to I2C0_SCL

**Sensor channels 0 and 1: pins 1 3.3V, 2 GND, 3 SCL, 4 SDA**

- J3 SENSOR CH0: 1 = S0_3V3; 2 = GND; 3 = S0_SCL; 4 = S0_SDA
- R45 220: 3V3_SENS to S0_3V3
- C40 100n 50V: S0_3V3 to GND
- R46 100: CH0_SCL to S0_SCL
- R47 100: CH0_SDA to S0_SDA
- J4 SENSOR CH1: 1 = S1_3V3; 2 = GND; 3 = S1_SCL; 4 = S1_SDA
- R48 220: 3V3_SENS to S1_3V3
- C41 100n 50V: S1_3V3 to GND
- R49 100: CH1_SCL to S1_SCL
- R50 100: CH1_SDA to S1_SDA
- U12 SRV05-4.TCT (Semtech): 1 IO1 = S0_SCL; 2 VN = GND; 3 IO2 = S0_SDA; 4 IO3 = S1_SCL; 5 VP = 3V3_SENS; 6 IO4 = S1_SDA

**Sensor channels 2 and 3: pins 1 3.3V, 2 GND, 3 SCL, 4 SDA**

- J5 SENSOR CH2: 1 = S2_3V3; 2 = GND; 3 = S2_SCL; 4 = S2_SDA
- R51 220: 3V3_SENS to S2_3V3
- C42 100n 50V: S2_3V3 to GND
- R52 100: CH2_SCL to S2_SCL
- R53 100: CH2_SDA to S2_SDA
- J6 SENSOR CH3: 1 = S3_3V3; 2 = GND; 3 = S3_SCL; 4 = S3_SDA
- R54 220: 3V3_SENS to S3_3V3
- C43 100n 50V: S3_3V3 to GND
- R55 100: CH3_SCL to S3_SCL
- R56 100: CH3_SDA to S3_SDA
- U13 SRV05-4.TCT (Semtech): 1 IO1 = S2_SCL; 2 VN = GND; 3 IO2 = S2_SDA; 4 IO3 = S3_SCL; 5 VP = 3V3_SENS; 6 IO4 = S3_SDA

**Sensor channels 4 and 5: pins 1 3.3V, 2 GND, 3 SCL, 4 SDA**

- J7 SENSOR CH4: 1 = S4_3V3; 2 = GND; 3 = S4_SCL; 4 = S4_SDA
- R57 220: 3V3_SENS to S4_3V3
- C44 100n 50V: S4_3V3 to GND
- R58 100: CH4_SCL to S4_SCL
- R59 100: CH4_SDA to S4_SDA
- J8 SENSOR CH5: 1 = S5_3V3; 2 = GND; 3 = S5_SCL; 4 = S5_SDA
- R60 220: 3V3_SENS to S5_3V3
- C45 100n 50V: S5_3V3 to GND
- R61 100: CH5_SCL to S5_SCL
- R62 100: CH5_SDA to S5_SDA
- U14 SRV05-4.TCT (Semtech): 1 IO1 = S4_SCL; 2 VN = GND; 3 IO2 = S4_SDA; 4 IO3 = S5_SCL; 5 VP = 3V3_SENS; 6 IO4 = S5_SDA

**Sensor channels 6 and 7 (spare connectors): pins 1 3.3V, 2 GND, 3 SCL, 4 SDA**

- J17 SENSOR CH6: 1 = S6_3V3; 2 = GND; 3 = S6_SCL; 4 = S6_SDA
- R63 220: 3V3_SENS to S6_3V3
- C46 100n 50V: S6_3V3 to GND
- R64 100: CH6_SCL to S6_SCL
- R65 100: CH6_SDA to S6_SDA
- R95 10k (NOT FITTED): S6_3V3 to S6_SCL
- R96 10k (NOT FITTED): S6_3V3 to S6_SDA
- J18 SENSOR CH7: 1 = S7_3V3; 2 = GND; 3 = S7_SCL; 4 = S7_SDA
- R66 220: 3V3_SENS to S7_3V3
- C47 100n 50V: S7_3V3 to GND
- R67 100: CH7_SCL to S7_SCL
- R68 100: CH7_SDA to S7_SDA
- R97 10k (NOT FITTED): S7_3V3 to S7_SCL
- R98 10k (NOT FITTED): S7_3V3 to S7_SDA
- U15 SRV05-4.TCT (Semtech): 1 IO1 = S6_SCL; 2 VN = GND; 3 IO2 = S6_SDA; 4 IO3 = S7_SCL; 5 VP = 3V3_SENS; 6 IO4 = S7_SDA

**RTC DS3231SN on I2C1 (GPIO21 SDA, GPIO14 SCL), ML-2020 cell charged from 3V3_Q**

- U7 DS3231SN: 1 32KHZ = not connected; 2 VCC = 3V3_Q; 3 ~{INT}/SQW = not connected; 4 ~{RST} = not connected; 5/6/7/8/9/10/11/12/13 GND = GND; 14 VBAT = RTC_VBAT; 15 SDA = I2C1_SDA; 16 SCL = I2C1_SCL
- C48 100n 50V: 3V3_Q to GND
- C49 100n 50V: RTC_VBAT to GND
- R69 2.2k: 3V3_Q to I2C1_SDA
- R70 2.2k: 3V3_Q to I2C1_SCL
- D11 1N4148W: cathode RTC_CHG, anode 3V3_Q
- R71 2.2k: RTC_CHG to RTC_VBAT
- BT1 ML-2020/H1CN: + RTC_VBAT, - GND

### E. LCD and SD card

**LCD MSP4031 on SPI2: FPC socket (lower contacts: ribbon contacts face the board)**

- J20 LCD FPC 14P 0.5mm AFC24-S14FIC-00: 1 = LCD_5V; 2 = GND; 3 = LCD_CS_C; 4 = LCD_RST_C; 5 = LCD_RS_C; 6 = LCD_MOSI_C; 7 = LCD_SCK_C; 8 = LCD_BL_C; 9 = not connected; 10 = CTP_SCL_5V; 11 = CTP_RST_C; 12 = CTP_SDA_5V; 13 = CTP_INT_C; 14 = not connected; MP MountPin = GND
- FB1 BLM18PG121SN1D: +5V to LCD_5V
- C50 47u 10V: LCD_5V to GND
- C51 47u 10V: LCD_5V to GND
- C52 100n 50V: LCD_5V to GND

**LCD signals: series resistors at the source; one GPIO resets both LCD and touch**

- R72 33: LCD_CS to LCD_CS_C
- R73 33: LCD_RS to LCD_RS_C
- R74 33: LCD_MOSI to LCD_MOSI_C
- R75 33: LCD_SCK to LCD_SCK_C
- R76 10k: 3V3_RF to LCD_CS
- R77 100: DISP_RST to LCD_RST_C
- R78 100: DISP_RST to CTP_RST_C
- R79 10k: 3V3_RF to DISP_RST
- R80 100: LCD_BL to LCD_BL_C
- R81 100: CTP_INT to CTP_INT_C
- R82 10k: 3V3_Q to CTP_INT

**Touch I2C level shifter: the LCD pulls its side to 5V; I2C1 stays at 3.3V. Extra 10k pull-ups to 5V on the cable side**

- Q8 BSS138: 1 G = 3V3_Q; 2 S = I2C1_SDA; 3 D = CTP_SDA_5V
- Q9 BSS138: 1 G = 3V3_Q; 2 S = I2C1_SCL; 3 D = CTP_SCL_5V
- R83 10k: LCD_5V to CTP_SDA_5V
- R84 10k: LCD_5V to CTP_SCL_5V

**ESD arrays on the LCD cable lines**

- U16 SRV05-4.TCT (Semtech): 1 IO1 = LCD_RS_C; 2 VN = GND; 3 IO2 = LCD_RST_C; 4 IO3 = LCD_CS_C; 5 VP = 3V3_Q; 6 IO4 = LCD_MOSI_C
- U17 SRV05-4.TCT (Semtech): 1 IO1 = CTP_RST_C; 2 VN = GND; 3 IO2 = LCD_BL_C; 4 IO3 = LCD_SCK_C; 5 VP = 3V3_Q; 6 IO4 = CTP_INT_C
- U18 SRV05-4.TCT (Semtech): 1 IO1 = CTP_SDA_5V; 2 VN = GND; 3 IO2 = CTP_SCL_5V; 4 IO3 = not connected; 5 VP = LCD_5V; 6 IO4 = not connected

**Micro-SD on SPI3 (GPIO38 MISO, 40 SCK, 41 MOSI, 42 CS), power switched by GPIO39 (undriven = on)**

- J22 microSD TF-01A: 1 DAT2 = SD_DAT2; 2 DAT3/CD = SD_CS; 3 CMD = SD_MOSI; 4 VDD = SD_3V3; 5 CLK = SD_CLK_C; 6 VSS = GND; 7 DAT0 = SD_MISO; 8 DAT1 = SD_DAT1; 9 DET = not connected; SH SHIELD = GND
- R85 33: SD_SCK to SD_CLK_C
- R86 10k: SD_3V3 to SD_CS
- R87 10k: SD_3V3 to SD_MOSI
- R88 10k: SD_3V3 to SD_MISO
- R89 10k: SD_3V3 to SD_DAT1
- R90 10k: SD_3V3 to SD_DAT2
- C53 47u 10V: SD_3V3 to GND
- C54 100n 50V: SD_3V3 to GND
- Q10 AO3401A: 1 G = SD_PWR_G; 2 S = 3V3_Q; 3 D = SD_3V3
- R91 10k: SD_PWR_G to GND
- R92 220: SD_PWR_OFF to SD_PWR_G
- C55 1u 50V: 3V3_Q to SD_PWR_G


## 14. Every net and the pins on it

Pin notation: reference.pin-number. 134 nets. Pins marked "not connected" in section 13 are not listed.

| Net | Pins on it |
|---|---|
| +12V | Q1.3, D3.1, D2.1, C10.1, C11.1, TP6.1, D1.2, R23.1, K1.11, K1.A1, D6.1, J11.1, D5.1, J16.1, D9.1, D10.1, C32.1, C33.1, C34.1, C35.1, C36.1, J12.1, J13.1, J14.1, J15.1 |
| +12V_HOLD | R21.2, R22.2, C12.1, C13.1, C14.1, U8.1 |
| +12V_IN | J1.1, Q1.2 |
| +5V | U8.3, C15.1, C16.1, TP1.1, U2.3, C17.1, U3.3, C21.1, U4.3, C24.1, U9.2, U9.5, U9.10, U9.13, U9.14, C37.1, J2.2, FB1.1 |
| 3V3_Q | U3.2, C22.1, C23.1, TP8.1, J23.5, R41.1, U6.24, C39.1, R42.1, R43.1, R44.1, U7.2, C48.1, R69.1, R70.1, D11.2, R82.1, Q8.1, Q9.1, U16.5, U17.5, Q10.2, C55.1 |
| 3V3_RF | U2.2, C18.1, C19.1, C20.1, TP2.1, U1.2, C28.1, C29.1, R25.1, U5.5, C31.1, R27.1, U11.5, R36.1, R76.1, R79.1 |
| 3V3_SENS | U4.2, C25.1, C26.1, TP9.1, R45.1, R48.1, U12.5, R51.1, R54.1, U13.5, R57.1, R60.1, U14.5, R63.1, R66.1, U15.5 |
| BLW_DAMP | C33.2, R38.1, R39.1, R40.1 |
| BLW_G | R37.2, Q7.1 |
| BLW_NEG | L1.2, C32.2, R38.2, R39.2, R40.2, J12.2, J13.2, J14.2, J15.2, TP12.1 |
| BLW_PWM | U1.8, R36.2, R37.1 |
| BLW_SW | Q7.3, D10.2, L1.1 |
| CH0_SCL | U6.5, R46.1 |
| CH0_SDA | U6.4, R47.1 |
| CH1_SCL | U6.7, R49.1 |
| CH1_SDA | U6.6, R50.1 |
| CH2_SCL | U6.9, R52.1 |
| CH2_SDA | U6.8, R53.1 |
| CH3_SCL | U6.11, R55.1 |
| CH3_SDA | U6.10, R56.1 |
| CH4_SCL | U6.14, R58.1 |
| CH4_SDA | U6.13, R59.1 |
| CH5_SCL | U6.16, R61.1 |
| CH5_SDA | U6.15, R62.1 |
| CH6_SCL | U6.18, R64.1 |
| CH6_SDA | U6.17, R65.1 |
| CH7_SCL | U6.20, R67.1 |
| CH7_SDA | U6.19, R68.1 |
| CTP_INT | U1.24, R81.1, R82.2 |
| CTP_INT_C | J20.13, R81.2, U17.6 |
| CTP_RST_C | J20.11, R78.2, U17.1 |
| CTP_SCL_5V | J20.10, Q9.3, R84.2, U18.3 |
| CTP_SDA_5V | J20.12, Q8.3, R83.2, U18.1 |
| D1_K | D1.1, R21.1, R22.1 |
| DISP_RST | U1.25, R77.1, R78.1, R79.2 |
| EN | U1.3, R25.2, C30.1, SW1.1, U5.1, TP10.1 |
| FAN_PWM | U1.11, J2.4, TP5.1 |
| FAN_TACH | U1.9, J2.3, R41.2, C38.1, TP4.1 |
| GND | J1.2, R8.2, D2.2, C10.2, C11.2, C12.2, C13.2, C14.2, U8.2, C15.2, C16.2, U2.1, C17.2, C18.2, C19.2, C20.2, U3.1, C21.2, C22.2, C23.2, U4.1, C24.2, C25.2, C26.2, TP3.1, TP7.1, R24.2, C27.2, U1.1, U1.40, U1.41, C28.2, C29.2, C30.2, SW1.2, U5.2, C31.2, SW2.2, J23.1, J19.A1, J19.A12, J19.B1, J19.B12, J19.SH, R28.2, R29.2, U11.2, R1.2, R13.2, Q2.2, R2.2, R14.2, Q3.2, R33.2, Q11.2, R3.2, R16.2, Q5.2, R7.2, R15.2, Q4.2, R34.2, Q6.2, Q7.2, C34.2, C35.2, C36.2, U9.7, C37.2, J2.1, C38.2, U6.1, U6.2, U6.12, U6.21, C39.2, J3.2, C40.2, J4.2, C41.2, U12.2, J5.2, C42.2, J6.2, C43.2, U13.2, J7.2, C44.2, J8.2, C45.2, U14.2, J17.2, C46.2, J18.2, C47.2, U15.2, U7.5, U7.6, U7.7, U7.8, U7.9, U7.10, U7.11, U7.12, U7.13, C48.2, C49.2, BT1.2, J20.2, J20.MP, C50.2, C51.2, C52.2, U16.2, U17.2, U18.2, J22.6, J22.SH, C53.2, C54.2, R91.2 |
| HEAT_CTL | U1.5, R2.1, R18.1 |
| HEAT_DRV | U9.6, R10.1, R32.1 |
| HEAT_G | R10.2, R14.1, Q3.1 |
| HEAT_G2 | R32.2, R33.1, Q11.1 |
| HEAT_IN | R18.2, U9.4 |
| HEAT_NEG | Q3.3, Q11.3, D4.2, J10.4 |
| HEAT_POS | D4.1, K1.14, J10.3 |
| HUM_CTL | U1.7, R7.1, R19.1 |
| HUM_DRV | U9.11, R11.1 |
| HUM_G | R11.2, R15.1, Q4.1 |
| HUM_IN | R19.2, U9.12 |
| HUM_NEG | Q4.3, J11.2, D5.2 |
| I2C0_SCL | U1.29, U6.22, R44.2 |
| I2C0_SDA | U1.28, U6.23, R43.2 |
| I2C1_SCL | U1.22, U7.16, R70.2, Q9.2 |
| I2C1_SDA | U1.23, U7.15, R69.2, Q8.2 |
| IO0 | U1.27, R27.2, SW2.1, TP11.1 |
| ION_CTL | U1.10, R34.1, R35.1 |
| ION_G | R35.2, Q6.1 |
| ION_NEG | Q6.3, J16.2, D9.2 |
| K1_COIL | Q5.3, K1.A2, D6.2 |
| K1_CTL | U1.6, R3.1, R20.1 |
| K1_DRV | U9.8, R12.1 |
| K1_G | R12.2, R16.1, Q5.1 |
| K1_IN | R20.2, U9.9 |
| LCD_5V | J20.1, FB1.2, C50.1, C51.1, C52.1, R83.1, R84.1, U18.5 |
| LCD_BL | U1.21, R80.1 |
| LCD_BL_C | J20.8, R80.2, U17.3 |
| LCD_CS | U1.18, R72.1, R76.2 |
| LCD_CS_C | J20.3, R72.2, U16.4 |
| LCD_MOSI | U1.19, R74.1 |
| LCD_MOSI_C | J20.6, R74.2, U16.6 |
| LCD_RS | U1.17, R73.1 |
| LCD_RST_C | J20.4, R77.2, U16.3 |
| LCD_RS_C | J20.5, R73.2, U16.1 |
| LCD_SCK | U1.20, R75.1 |
| LCD_SCK_C | J20.7, R75.2, U17.4 |
| MON_RX | J23.3, R94.2 |
| MON_TX | J23.2, R93.2 |
| MUX_RST | U1.30, U6.3, R42.2 |
| Q1_G | Q1.1, R8.1, D3.2 |
| RTC_CHG | D11.1, R71.1 |
| RTC_VBAT | U7.14, C49.1, R71.2, BT1.1 |
| S0_3V3 | J3.1, R45.2, C40.1 |
| S0_SCL | J3.3, R46.2, U12.1 |
| S0_SDA | J3.4, R47.2, U12.3 |
| S1_3V3 | J4.1, R48.2, C41.1 |
| S1_SCL | J4.3, R49.2, U12.4 |
| S1_SDA | J4.4, R50.2, U12.6 |
| S2_3V3 | J5.1, R51.2, C42.1 |
| S2_SCL | J5.3, R52.2, U13.1 |
| S2_SDA | J5.4, R53.2, U13.3 |
| S3_3V3 | J6.1, R54.2, C43.1 |
| S3_SCL | J6.3, R55.2, U13.4 |
| S3_SDA | J6.4, R56.2, U13.6 |
| S4_3V3 | J7.1, R57.2, C44.1 |
| S4_SCL | J7.3, R58.2, U14.1 |
| S4_SDA | J7.4, R59.2, U14.3 |
| S5_3V3 | J8.1, R60.2, C45.1 |
| S5_SCL | J8.3, R61.2, U14.4 |
| S5_SDA | J8.4, R62.2, U14.6 |
| S6_3V3 | J17.1, R63.2, C46.1, R95.1, R96.1 |
| S6_SCL | J17.3, R64.2, R95.2, U15.1 |
| S6_SDA | J17.4, R65.2, R96.2, U15.3 |
| S7_3V3 | J18.1, R66.2, C47.1, R97.1, R98.1 |
| S7_SCL | J18.3, R67.2, R97.2, U15.4 |
| S7_SDA | J18.4, R68.2, R98.2, U15.6 |
| SD_3V3 | J22.4, R86.1, R87.1, R88.1, R89.1, R90.1, C53.1, C54.1, Q10.3 |
| SD_CLK_C | J22.5, R85.2 |
| SD_CS | U1.35, J22.2, R86.2 |
| SD_DAT1 | J22.8, R89.2 |
| SD_DAT2 | J22.1, R90.2 |
| SD_MISO | U1.31, J22.7, R88.2 |
| SD_MOSI | U1.34, J22.3, R87.2 |
| SD_PWR_G | Q10.1, R91.1, R92.2, C55.2 |
| SD_PWR_OFF | U1.32, R92.1 |
| SD_SCK | U1.33, R85.1 |
| SPARE1 | U1.38, J23.4 |
| SSR_A1 | D8.1, K1.12, J10.1 |
| SSR_A2 | Q2.3, D8.2, J10.2 |
| SSR_CTL | U1.4, R1.1, R17.1 |
| SSR_DRV | U9.3, R9.1 |
| SSR_G | R9.2, R13.1, Q2.1 |
| SSR_IN | R17.2, U9.1 |
| UART0_RX | U1.36, R94.1 |
| UART0_TX | U1.37, R93.1 |
| USB_CC1 | J19.A5, R28.1 |
| USB_CC2 | J19.B5, R29.1 |
| USB_DM | J19.A7, J19.B7, R31.1, U11.1 |
| USB_DM_MCU | U1.13, R31.2 |
| USB_DP | J19.A6, J19.B6, R30.1, U11.6 |
| USB_DP_MCU | U1.14, R30.2 |
| V12_SENSE | R23.2, R24.1, C27.1, U1.12 |
| WDI | U5.4, R26.2 |
| WDT | U1.39, R26.1 |

## 15. Changes since revision 1 of this report

| Point raised | Finding | Change |
|---|---|---|
| 1. VBUS back-feed through U11 | Correct. The S3's D+ pull-up would have lifted VBUS to about 2.7V | U11's supply pin moved from VBUS to 3V3_RF. VBUS now connects to nothing. The VBUS net no longer exists (132 nets, was 133) |
| 2. GPIO39 pull-up at reset | Correct. Espressif's datasheet gives GPIO39 (MTCK) an internal pull-up by default. Against 100kΩ it would have left Q10 half on | R91 100kΩ → 10kΩ. R92 1kΩ → 220Ω, so that "high" still switches fully off (3.23V at the gate) with at most about 15mA from the pin. (Revision 2 had 100Ω; raised at the spec engineer's suggestion.) C55 100nF → 1µF, to keep the 10ms soft start. §6 and §7 now say how to switch the card on |
| 3. LCD module pull-ups | Only CTP_SDA and CTP_SCL go to 5V. LED and SD_CS go to the module's 3.3V. CTP_INT and CTP_RST have none | No change. §6 now lists them |
| 4. Wording | | §4 three spares on J23 · §12 RTC cell 2.8–3.2V · §7 I²C0 at 25kHz |

Other pins checked against the same datasheet table while answering point 2: GPIO21 (the watchdog pin until revision 9) has
no pull at reset; GPIO35–38, 40–42, 47, 48 have none; GPIO43 and GPIO44 have pull-ups (already
noted for the spares). GPIO40 carries the mux reset, which has its own 10kΩ pull-up.

**Revision 4 (footprints only; no circuit change, still 213 parts and 132 nets):** footprints
assigned for the LCD socket J20, the micro-SD socket J22, the switches SW1/SW2 and the inductor L1,
all taken from JLCPCB's own library for those part numbers. J20's two mounting tabs are now tied to
GND. J22's symbol now shows its card-detect contact, left unconnected. See section 16.

**Revision 7 (11 Nov 2026) — three circuit changes, agreed at review. Now 219 parts, 134 nets.**
No existing reference number moved; the six new resistors are R93–R98.

| Change | Detail |
|---|---|
| 12V sense | R24 27kΩ → **15kΩ**. 12V reads 1.57V (was 2.55V); at the 24V clamp level the pin sees about 3.2V, not 5V |
| J23 is the serial monitor port | Header now fitted. Pins reordered so the cable's three wires sit together: 1 GND, 2 TX (GPIO43), 3 RX (GPIO44), then 4 GPIO13, 5 3V3_Q. R93 220Ω in the TX line, R94 1kΩ in the RX line. Nets SPARE2_TXD0 / SPARE3_RXD0 renamed UART0_TX / UART0_RX; new nets MON_TX, MON_RX |
| Spare sensor pull-ups | Four unfitted 10kΩ positions: R95 S6_3V3–S6_SCL, R96 S6_3V3–S6_SDA, R97 S7_3V3–S7_SCL, R98 S7_3V3–S7_SDA |

Also in revision 7: the USB reset behaviour is stated from Espressif's manual (sections 2 and 6);
GPIO47/48 confirmed at 3.3V on the N8R2 (section 4); three bring-up checks added (section 12).
The schematic's LCD_SD page is now A2 size.

**Revision 8 (11 Nov 2026) — one value change, agreed at review. Still 219 parts, 134 nets.**

| Change | Detail |
|---|---|
| R94, monitor RX series resistor | 1kΩ → **4.7kΩ** (JLCPCB C23162, Basic, 0603). Cuts the current a monitor cable can feed into the unpowered board from about 2.7mA to about 0.6mA, while a low from the cable still reads as low against GPIO44's internal pull-up (section 6) |
| Bring-up checks, section 12 | "Monitor cable with the board off: TP2 under 0.3V" was wrong — a good board would fail it. Replaced by a current check (under 2.8V across R94) and a power-up test with the cable attached |

**Revision 9 (12 November 2026; four module pins swapped for layout; no part, value or footprint changed, still 219 parts and 134 nets):**
agreed with the spec engineer. Two pairs trade places so each signal leaves the module on the side it is going to.

| Signal | Was | Now | Reset state of the new pin (Espressif ESP32-S3 datasheet, Tables 2-1 and 2-2) |
|---|---|---|---|
| BLW_PWM | GPIO9 (module pin 17) | **GPIO15** (pin 8) | No pull-up or pull-down. Driven low for about 60µs at power-up, as GPIO9 also is |
| DISP_RST | GPIO15 (pin 8) | **GPIO9** (pin 17) | No pull. Same 60µs low at power-up |
| WDT | GPIO21 (pin 23) | **GPIO17** (pin 10) | No pull. **Driven low for about 60µs at power-up; GPIO21 had no such glitch.** Accepted by the spec engineer: one pulse on WDI counts as a kick, U5 is holding EN low at that moment, and WDI floats afterwards |
| CTP_INT | GPIO17 (pin 10) | **GPIO21** (pin 23) | No pull, no glitch listed |

The datasheet does not give pin states for the ROM bootloader's download mode, for GPIO17 or for GPIO21. Flashing with
R26 fitted is proven on the board by the watchdog bring-up check in section 12; leaving R26 out is the fallback.
Firmware: only the pin numbers change. External pull-ups, series resistors and every other pin are as before.

**Revision 10 (12 November 2026; one more pin swap for layout, agreed with the spec engineer; no part, value or footprint changed, still 219 parts and 134 nets):**
the module now lies on its side on the board (antenna out of the right edge), and the four SD lines left it in the reverse of the SD socket's pin order.

| Signal | Was | Now | Notes |
|---|---|---|---|
| SD_CS | GPIO35 (module pin 28) | **GPIO38** (pin 31) | No pull at reset (checked against the datasheet earlier, see above). R86, its 10kΩ pull-up, stays on SD_CS |
| SD_MISO | GPIO38 (pin 31) | **GPIO35** (pin 28) | No pull at reset. SPI3 has no dedicated pins on the S3; both go through the GPIO matrix as before |

SD_SCK (GPIO36) and SD_MOSI (GPIO37) are unchanged. The module must still be the N8R2.
Revision 9's four swaps were checked against the new orientation and stand: BLW_PWM and WDT are on the row of pins facing the top edge (loads and the watchdog chip), DISP_RST and CTP_INT on the column facing the display. No other pin needs to move.

Also in revision 10, with no effect on firmware: the four display lines use different channels of the protection chip U16 (four identical channels), so each joins its header pin on the top layer: pin 1 LCD_RS_C, pin 3 LCD_CS_C, pin 4 LCD_SCK_C, pin 6 LCD_MOSI_C (was CS, RS, MOSI, SCK).

**Revision 11 (12 November 2026; layout only, nothing for firmware; no part, value or footprint changed, still 219 parts and 134 nets):**
the USB protection chip U11 uses two different channels of its four identical ones: USB_DM on pin 1 and USB_DP on pin 6 (was USB_DP on pin 1, USB_DM on pin 3). The two pins face each other across the chip, so each USB line passes its own pin on the top layer and neither needs a via. Pins 3 and 4 are now the unused pair.

**Revision 12 (12 November 2026; agreed with Josh, who uses the ribbon cable only; nothing for firmware; now 218 parts, still 134 nets):**
- **J21 removed.** It was the unfitted 1×14 pin header carrying the same 12 LCD lines as the ribbon socket J20. Its row of 14 holes sat between the socket and the LCD's protection chips and blocked the routing.
- **U16 and U17 channels reassigned** (identical channels, so no electrical change), so each line reaches its chip on the top layer in the socket's pin order. U16: pin 1 LCD_RS_C, pin 3 LCD_RST_C, pin 4 LCD_CS_C, pin 6 LCD_MOSI_C. U17: pin 1 CTP_RST_C, pin 3 LCD_BL_C, pin 4 LCD_SCK_C, pin 6 CTP_INT_C.

**Revision 13 (12 November 2026; 18 signals move to different module pins for the new board layout; approved by the spec engineer; no part, value or footprint changed, still 218 parts and 134 nets):**
the board has been laid out again from the start. The module now stands upright on the top edge with its antenna in a cut-out, and each side of it faces the block it serves: left pins to the loads, reset and USB; bottom pins to the display; right pins to the SD card, the sensor bus and the serial port. The signals were moved so that each leaves the module on the side where it is used, in the order its connector needs.

| Signal | Was | Now |
|---|---|---|
| V12_SENSE | GPIO1 (module pin 39) | **GPIO8** (pin 12) |
| FAN_TACH | GPIO2 (pin 38) | **GPIO16** (pin 9) |
| ION_CTL | GPIO8 (pin 12) | **GPIO17** (pin 10) |
| DISP_RST | GPIO9 (pin 17) | **GPIO48** (pin 25) |
| SPARE1 (J23 pin 4) | GPIO13 (pin 21) | **GPIO2** (pin 38) |
| LCD_RS | GPIO14 (pin 22) | **GPIO9** (pin 17) |
| LCD_BL | GPIO16 (pin 9) | **GPIO13** (pin 21) |
| WDT | GPIO17 (pin 10) | **GPIO1** (pin 39) |
| CTP_INT | GPIO21 (pin 23) | **GPIO47** (pin 24) |
| SD_MISO | GPIO35 (pin 28) | **GPIO38** (pin 31) |
| SD_SCK | GPIO36 (pin 29) | **GPIO40** (pin 33) |
| SD_MOSI | GPIO37 (pin 30) | **GPIO41** (pin 34) |
| SD_CS | GPIO38 (pin 31) | **GPIO42** (pin 35) |
| MUX_RST | GPIO40 (pin 33) | **GPIO37** (pin 30) |
| I2C1_SDA | GPIO41 (pin 34) | **GPIO21** (pin 23) |
| I2C1_SCL | GPIO42 (pin 35) | **GPIO14** (pin 22) |
| I2C0_SDA | GPIO47 (pin 24) | **GPIO35** (pin 28) |
| I2C0_SCL | GPIO48 (pin 25) | **GPIO36** (pin 29) |

Unchanged: GPIO0 (BOOT), GPIO4–7 (SSR, heater, K1, humidifier), GPIO10–12 (LCD CS, MOSI, SCK), GPIO15 (blowers), GPIO18 (fan PWM), GPIO19/20 (USB), GPIO39 (SD power) and GPIO43/44 (serial monitor). GPIO3, 45 and 46 stay unconnected.

Reset states of the new pins were checked by the spec engineer against Espressif's ESP32-S3 datasheet v2.2, Tables 2-1 and 2-2: GPIO1 is an input at and after reset with no pull, and is driven low for about 60µs at power-up, as GPIO17 was — accepted for the watchdog input on the same grounds as in revision 9. GPIO21, 35–38, 40–42, 47 and 48 have no pulls. The power-up glitches on GPIO2, 8, 9, 13, 14, 16 and 17 are harmless for their new jobs. Nothing breaks a test or a boot requirement.

Every external pull-up, pull-down and series resistor stays on its signal. The module must still be the N8R2: GPIO35–37 now carry I²C0 and the mux reset. J23's value text on the schematic now reads "IO2" for pin 4. Firmware: only the pin numbers change (section 4).

**Revision 14 (12 November 2026; one statement corrected; no part, value, pin, net or footprint changed, still 218 parts and 134 nets):**
the LCD socket J20 (JUSHUO AFC24-S14FIC-00) was described as having contacts on both faces, taken from LCSC's listing.
The maker's drawing decodes the part number as 14 ways, contact type **F = lower**, gold-flash plating, tube packing.
The report and the note on the schematic sheet now say lower contacts: the ribbon's bare contacts face the board.
Pin 1 was confirmed at the same time (section 6). Nothing for firmware.

**Revision 15 (13 November 2026; one part changed; no pin, net, footprint or copper changed, still 218 parts and
134 nets).** Four independent reviewers, given only the spec, the parts list and the pin-by-pin connection list,
checked the design against the makers' datasheets (`independent-review/` in the project folder). Every pin check
came out right. One design fault was found:

- **U5 TPS3828-33 → MAX823SEUK+T (JLCPCB C239902).** TI's datasheet (SLVS165O, section 7.3.4) says that if the
  watchdog input is toggling while a TPS3820, TPS3823, TPS3824 or TPS3828 is asserting reset, the reset output is
  "stuck at a logic low" after the supply recovers. Here the firmware toggles GPIO1 every loop pass, and after U5
  pulls EN low the ESP32 runs on for up to about 0.5ms while C30 discharges, so a supply dip could leave the board
  held in reset until the 12V is cycled. This also overturns the reasoning recorded under revisions 9 and 13, that
  GPIO1's power-up glitch was safe because "U5 is holding EN low at that moment". TI's own fixed part, the
  TPS3823A-33, had no JLCPCB stock on 13 November. The MAX823S is the part the TI family copies: the same
  SOT-23-5 pins (1 RESET, 2 GND, 3 MR, 4 WDI, 5 VCC), the same 2.93V threshold, reset delay 140–280ms, watchdog
  1.12–2.4s cleared by either edge, floating WDI disables the watchdog, and a reset pull-up limited to 0.4mA so
  SW1 still works. Its datasheet (19-0487 Rev 5) describes no stuck-reset behaviour; that is the absence of a
  warning, not a proof, so a bring-up check is added (section 12). R25 stays and is harmless. Josh's decision.
- **Board:** U5's value and part number only. The Gerber and drill files regenerate identical to file set B4
  apart from their date stamps, and the placement file is identical (JLCPCB's footprint for C239902 lands on
  U5's pads within 0.08mm at the same rotation). Only one line of the parts list changes.
- **Firmware:** nothing changes. The 0.9s rule stands (the shortest timeout is now 1.12s).

Corrected in the same revision, with no design change: the hold-up figures by load and the 1.1W in R21/R22 at
the 1A peak (section 3); the RTC cell's float voltage, about 3.0–3.15V, not 2.9–3.0V (section 6); the LCD
module's ribbon socket does have its pin-1 end and pin order documented, in lcdwiki's size drawing (section 6;
found by Josh); four bring-up criteria in section 12 (buttons released, watchdog time, LCD cable resistance,
bulb surge) and one new check (supply dip with the watchdog running); three items added to section 11.

## 16. Footprints

Every schematic symbol pin was compared by script with every footprint pad: **all 65 part types
match exactly** (re-run after revision 7; revision 8 changed one value, no footprint). "Proven on Rev N" means the same footprint is on the
board that was built and tested.

| Part | Footprint | Basis |
|---|---|---|
| XT30PW-M | XT30PW-M_J1_Pin1Plus | Rev P project footprint, proven on Rev N |
| IRF4905 | TO-220-3_Horizontal_TabDown_NoHole | Rev P project footprint, proven on Rev N |
| 11 values | R_0603_1608Metric | Standard size |
| 1N4148W, BZT52C15-7-F | D_SOD-123 | Standard package, pad 1 = cathode, cathode bar on the silkscreen. Orientation compared with JLCPCB's own footprint for each part number: the cathode is at the same end in both |
| SMBJ15A | D_SMB | Standard package, pad 1 = cathode, cathode bar on the silkscreen. Orientation compared with JLCPCB's own footprint for each part number: the cathode is at the same end in both |
| 10u 25V, 22u 25V | C_0805_2012Metric | Standard size |
| TP | TestPoint_Keystone_5000-5004_Miniature | KiCad library, proven on Rev N |
| SS34, SS54 | D_SMA | Standard package, pad 1 = cathode, cathode bar on the silkscreen. Orientation compared with JLCPCB's own footprint for each part number: the cathode is at the same end in both |
| 6.8R 2W | R_2512_6332Metric | Standard size |
| 470u 35V polymer | CP_Elec_10x12.6 | KiCad 10mm can; pads compared with JLCPCB's footprint for C46550472 — equivalent (within 0.15mm) |
| 100n 50V, 1u 50V | C_0603_1608Metric | Standard size |
| TSR 3-2450N | Converter_DCDC_TRACO_TSR2-24xxN_TSR2-24xxxN_THT | KiCad library, proven on Rev N (U8) |
| LDL1117S33R | SOT-223-3_TabPin2 | Standard package; pin-out compared with JLCPCB's symbol for the part number |
| ESP32-S3-WROOM-1-N8R2 | ESP32-S3-WROOM-1 | KiCad library footprint for this exact module; pin numbers compared with JLCPCB's symbol for C2913204 |
| BOOT, RESET | SW_XKB_TS-1187A_JLC | JLCPCB's own footprint for C318884. Wired on two diagonal pads, which are on opposite contacts whichever way the switch is built inside; the other two pads are soldered but unconnected |
| MAX823SEUK+T | SOT-23-5 | Standard package. Pin numbers from Maxim's datasheet (1 RESET, 2 GND, 3 MR, 4 WDI, 5 VCC), the same as the TPS3828-33 it replaces; JLCPCB's footprint for C239902 lands on the pads within 0.08mm |
| J23 monitor header | PinHeader_1x05_P2.54mm_Vertical | KiCad library, standard 2.54mm header |
| USB-C TYPE-C-31-M-12 | USB_C_Receptacle_HRO_TYPE-C-31-M-12 | KiCad library footprint for this exact part |
| SRV05-4.TCT (Semtech) | SOT-23-6 | Standard package. Pin functions confirmed from Semtech's datasheet: pins 1, 3, 4, 6 data lines; pin 2 ground; pin 5 positive reference |
| SN74HCT08N in socket | DIP-14_W7.62mm_Socket | KiCad library, proven on Rev N |
| AO3400A, AO3401A, BSS138 | SOT-23 | Standard package; pin-out compared with JLCPCB's symbol for the part number |
| Finder 34.51.7.012.5010 | Relay_SPDT_Finder_34.51_Vertical_SilkClear | Rev P project footprint, proven on Rev N |
| MP012012 SSR+ SSR- HEAT+ HEAT- | Multicomp_MP012012_1x04_P5.08mm_Horizontal | Rev P project footprint, proven on Rev N |
| BLOWER 1, BLOWER 2, BLOWER 3, BLOWER 4, HUMID, ION | JST_XH_B2B-XH-A_1x02_P2.50mm_Vertical | KiCad library, proven on Rev N |
| 220uH 1.16A SWRB1207S-221MT | L_Sunlord_SWRB1207S_JLC | JLCPCB's own footprint for C169380 (the generic KiCad one had smaller pads) |
| 10 1206 | R_1206_3216Metric | Standard size |
| 9 values | JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical | KiCad library, proven on Rev N |
| TCA9548APWR | TSSOP-24_4.4x7.8mm_P0.65mm | Standard package; pin-out compared with JLCPCB's symbol for the part number |
| DS3231SN | SOIC-16W_7.5x10.3mm_P1.27mm | Standard package; pin-out compared with JLCPCB's symbol for the part number |
| ML-2020/H1CN | BAT_Panasonic_ML-2020_H1CN | SnapEDA's footprint for this part number (supplied by Josh): cell lying flat, two tabs into slots 20.5mm apart, + tab the wider one. Slots widened from 0.4mm to 0.65mm and pads enlarged so JLCPCB can make them. Panasonic's own tab drawing was not available. **Josh's decision (11 Nov 2026): the SnapEDA footprint is taken as correct; no further check before ordering** |
| LCD FPC 14P 0.5mm AFC24-S14FIC-00 | FPC_14P_P0.5mm_AFC24-S14FIC_JLC | JLCPCB's own footprint for C262291. Mounting tabs tied to GND |
| BLM18PG121SN1D | L_0603_1608Metric | Standard size (0603 ferrite bead) |
| 47u 10V | C_1206_3216Metric | Standard size |
| microSD TF-01A | microSD_HRO_TF-01A_JLC | JLCPCB's own footprint for C91145. Shell pads to GND; card-detect pad 9 unconnected |

**Revision 5:** footprint assigned for the RTC cell BT1. Every part now has a footprint. No circuit
change.

**Revision 6 (parts only; no circuit change):** three protection parts changed from clone brands to
the original makers, all in JLCPCB stock: ESD arrays U11–U18 **Semtech SRV05-4.TCT** (C13612), surge
clamp D2 **Littelfuse SMBJ15A** (C83846), gate zener D3 **Diodes Inc BZT52C15-7-F** (C177014). The
clones had been chosen only because they carried no Extended fee, which no longer applies.

**Wording, 11 Nov 2026 (still revision 6):** section 3 now lists every load on 3V3_RF; section 4
names the rail for each pull-up.