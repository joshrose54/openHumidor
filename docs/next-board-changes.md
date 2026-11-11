# Wineador controller: the next board

The brief for the next controller board. Compared with **Rev P**, the current design (described
in `wineador-spec.md`); the bench board we tested on is Rev N, which is Rev P without D7, R17–R20 and TP7.

## How we'd like to work

Four phases, each one reviewed before the next starts:

1. **Proposal and research — no drawing yet.** A short written proposal: the parts (with JLCPCB
   part numbers and stock), a pin map, the power tree, and your answers to section 5. Please do
   whatever research you think is needed, and **push back on anything we've asked for that you'd do
   differently** — we're not PCB designers.
2. **Schematic.** We review it against sections 3 and 6.
3. **Layout.** We review it in KiCad.
4. **Order**, then the bring-up tests in section 7.

## 1. Ground rules

- **Every component on the top side. Nothing mounted on the bottom.** Through-hole parts are fine
  as long as they sit on top.
- **SMD wherever JLCPCB stocks the part**, assembled by JLCPCB. Otherwise through-hole.
- **The outline and mounting holes may change** if that gives better routing. Keep roughly the
  current size unless there's a good reason, and tell us the new dimensions and hole positions —
  the enclosure will follow.
- **Connectors may move.** Please group them sensibly — for example power and loads along one edge,
  sensors along another, USB, the LCD and the SD slot together.
- **Where parts come from.** JLCPCB supplies and solders **only the SMD parts** it assembles.
  **Every through-hole part is bought from Farnell UK and hand-soldered by us** — no JLCPCB
  through-hole assembly. Please give the **Farnell UK order code** for each through-hole part and
  check it's in stock.
- **Reuse parts where it's safe and effective**, within each group: the same Farnell part wherever
  a through-hole part can do several jobs, and the same JLCPCB part for the SMD ones, so the BOM
  has as few unique parts as possible. Prefer **JLCPCB Basic parts** where suitable (no
  Extended-part setup fee).
- No DevKit step: straight to the SMD design.

## 2. What stays as Rev P

- **12V input:** XT30 (J1), the 4A inline fuse off the board, Q1 reverse-polarity protection with
  D3 and R8, D2 surge clamp.
- **The hardware interlock, exactly as now:** K1 (Finder 34.51.7.012.5010) in the 12V feed — COM
  +12V, NC to the SSR's +A1, NO to the heater + — so the SSR and the heater can never both have a
  feed. The 74HCT08 gating, the 10k pull-downs on the load pins, the gate resistors and gate
  pull-downs, and the flyback diodes.
- **The loads:** SSR control, heater, humidifier fan and relay coil on low-side N-MOSFETs. SMD
  equivalents are fine, logic-level at the gate drive you use, rated with good margin. The heater
  draws ~1.7A.
- **Plenum fan (J2):** 5V, 25kHz PWM, tach with its 1k pull-up and 1µF filter, as now.
- **Connector types and pin-outs:** XT30 for 12V; 4-way 5.08mm screw block for J10 (1 SSR +A1,
  2 SSR −A2, 3 heater +, 4 heater −); JST XH for sensors, fans and blowers. Sensor connectors:
  1 3.3V, 2 GND, 3 SCL, 4 SDA.
- **Test points TP1–TP7** (5V, 3.3V, GND, tach, PWM, 12V, GND).

## 3. Requirements — must-haves

- **The board must run with no USB connected.** The current DevKit can't: with no USB, its
  USB-serial chip and auto-reset circuit hold EN at ~0V (the ESP32 never starts) and GPIO0 low (it
  boots into flashing mode) — two DevKits, same result, off the board. We fixed it with 2.2kΩ
  pull-ups on EN and GPIO0.
- **Plugging USB in or out, and opening or closing the serial monitor, must never restart it** —
  in the cabinet, connecting a laptop mustn't restart the controller and its lock-out timers.
- **USB and 12V safe in any order.** USB never powers the board's rails, and the board never feeds
  the laptop (Rev P does this with D7).
- **It must recover by itself from a supply dip:** a clean reset, never a hang. On the current
  board a sharp disturbance (a 21W bulb switching on, with USB out) once froze the ESP32 in a state
  its own watchdogs couldn't catch; only an external EN reset recovered it.
- **No load output may pulse at boot or reset.** Every load is off until the firmware drives it —
  except the blowers, which run at full speed until the firmware drives them (4.5).
- **A 3.3V supply with good headroom** for WiFi current bursts.
- **Designed against electrical noise from the start** (4.7). The board switches a relay, a heater,
  fans and a mains SSR, carries WiFi, and runs sensor leads into the cabinet — the logic, sensors and
  SD card must stay reliable through all of it.
- **Test points:** keep TP1–TP7 and add **EN** and **GPIO0**.

## 4. Changes

### 4.1 ESP32-S3 module with its built-in USB

- **ESP32-S3-WROOM-1, soldered on by JLCPCB** (chip, flash and antenna built in). Not the bare
  chip. Variant: we suggest **N8R2** — 8MB flash leaves room for wireless (OTA) updates, and its
  quad PSRAM leaves GPIO 35–37 free (the octal R8 versions use them). Confirm or recommend otherwise.
- **USB-C wired to the S3's built-in USB** (GPIO19 D−, GPIO20 D+). **No USB-to-serial chip (CP2102,
  CH340 or similar) and no auto-reset transistors.** Flashing then needs no buttons and no
  auto-reset circuit, and nothing on the USB side touches EN — the main fix for the first two
  requirements. Please confirm nothing on the USB side can hold EN or GPIO0 low when USB is absent.
- **USB-C sits inside the enclosure** — it's for flashing and debugging with the lid open; in
  service, updates will be wireless. Place it wherever routes best.
- **Antenna** at the board edge with nothing under it. The enclosure is 3D-printed plastic.
- **A supply supervisor (reset chip) on EN**, holding EN low cleanly whenever 3.3V is out of range
  and releasing it only once 3.3V is good.
- **Support circuit — please confirm each:**
  - 3.3V regulator with headroom, input and output capacitors
  - decoupling at the module's power pins
  - EN: pull-up, capacitor, reset button and the supervisor — **and nothing else on EN**
  - BOOT button on GPIO0 (buttons can stay inside the enclosure)
  - USB-C with **5.1kΩ on CC1 and CC2**, so a C-to-C cable works
  - ESD protection on the USB data lines
  - the USB power path (replacing D7)
  - strapping pins held in their correct boot states
- **Pin map: please propose one.** Avoid for general I/O: 0, 3, 45, 46 (strapping; 45 must stay
  low at boot); 19, 20 (USB); 43, 44 (serial). Put the load outputs on pins that stay quiet through
  boot and reset. We'll check it against the firmware.

### 4.2 The I²C mux on the board

- Replace the TCA9548A module (HW-617) with the chip. The module provides **10kΩ pull-ups on the
  main SDA and SCL**, a **10kΩ pull-up on RST** and the address pins; the board needs its own, plus
  decoupling. **Address 0x70.** RST on a GPIO, for bus-fault recovery.
- Sensor channels **without** pull-ups: each SHT40 probe has its own 10kΩ inside.
- **Six sensors on channels 0–5** (on Rev P they're on 2–7, which was only how the module happened
  to be wired). **Bring channels 6 and 7 out to two spare XH sensor connectors**, same pin-out, for
  possible additions later (e.g. a Sensirion SGP41 VOC sensor) with no board change.

### 4.3 RTC on the board, with a backup cell that charges itself

- The current DS3231 module (HW-084) can't fully charge its LIR2032 from 3.3V; the cell went flat
  and the clock lost its time.
- Please propose an on-board RTC with a backup cell and **a charge circuit that works from 3.3V**.
  State the **cell, the charger and the expected backup time** with the board unpowered.
- The firmware needs an **"oscillator stopped" flag**, as on the DS3231.
- The DS3231 module also carries **4.7kΩ pull-ups on the main I²C bus**; with it and the mux
  module gone, the board needs its own main-bus pull-ups — your choice of value.

### 4.4 LCD display, and an SD card slot on the main board

**LCD — display, touch and backlight only:**
- **lcdwiki 4.0" ST7796 capacitive-touch SPI display (MSP4031).** Docs, schematic and datasheets:
  https://www.lcdwiki.com/4.0inch_Capacitive_SPI_Module_ST7796
- **Interface:** 14 pins, as a 2.54mm header and a **0.5mm-pitch, 14-position FPC socket** with the
  same signals: VCC, GND, LCD_CS, LCD_RST, LCD_RS, MOSI, SCK, LED, MISO, CTP_SCL, CTP_RST, CTP_SDA,
  CTP_INT, SD_CS. A 30cm 0.5mm FPC cable comes with it.
- **Power from 5V** (recommended; at 3.3V the backlight is dim). It has its own small 3.3V
  regulator (XC6206) for its logic. Backlight 103mA; please budget the total and add bulk
  capacitance at the connector.
- **Touch:** FT6336U on I²C, plus GPIOs for **CTP_RST** and **CTP_INT**.
- **Backlight:** the LED pin is transistor-driven and takes on/off or PWM — put it on a GPIO so the
  firmware can dim it or switch it off.
- **Its SD slot is not used** (SD_CS left unconnected). On its schematic the card hangs off the
  XC6206 with only 100nF and through BSS138 level shifters, at the end of the cable — too weak for
  reliable logging.
- The LCD mounts on the enclosure lid. **How it connects is your call** — the FPC is likely more
  secure than Dupont wires onto the header for a lid that opens. Please tell us the safe **cable
  length and SPI speed**.

**SD card slot on the main board (replaces U4):**
- A **micro-SD slot on the board**, its own SPI chip select, designed for reliability: short traces
  to the S3, pull-ups on the data lines, series resistors where right, and **local bulk and
  ceramic decoupling at the card's 3.3V** from a supply with headroom for its write peaks
  (~100mA+). Place it so the card can be removed with the lid open; logs will also be downloadable
  over WiFi.
- **Why:** on the current board, the SD module runs straight off the shared 3.3V rail with only
  small ceramic capacitors. About **1 in 250–670 large (~7 KB) writes failed**: the card accepted the
  data, then reported it had just reset. Not the card (a new one did the same), not the SPI clock
  (1 MHz no better than 4 MHz). Errors fell about **twentyfold with WiFi off** — WiFi's current bursts
  most likely dip the shared rail. The card's supply must not dip when WiFi transmits.

### 4.5 Blower speed control (new)

- The four **GDSTIME GDB5015 12V** blowers (J12–J15): **2-wire, no tach**, ~0.12A each, ~0.5A total.
  They run from 6 to 12V.
- **Voltage control, not plain PWM on their supply.** 2-wire fans have their own controller inside,
  and chopping the supply causes ticking and "singing"; the usual practice is a steady lower DC
  voltage, **set by the ESP32** — PWM smoothed to DC, or a regulator whose output the ESP32 sets.
  No manual adjustment. Your choice of circuit.
- **Full speed by default:** with no signal from the ESP32 (boot, reset, crash) the blowers get the
  full 12V; the firmware only ever slows them. Not through the 74HCT08 gate.
- A flyback diode across the output if your circuit needs one.

### 4.6 Ion generator output (new)

- A 12V negative ion generator, **0.5W (~40mA)**, on a **2-pin JST XH**, + marked.
- Its own GPIO and low-side MOSFET, **gate pulled down so it's off at boot**, and a small diode
  across the output.

### 4.7 Electrical noise — plan for it and prevent it (new)

On the current board we've seen: an ESP32 crash and a freeze when a 21W bulb switched on (USB out),
and about 1 in 250–670 large SD writes failing (cause still under investigation). Please design the
next board to prevent this kind of problem, and **tell us your approach**. Points we'd expect you to
consider — your judgement on all of them:

- **Grounding:** a solid ground plane under the logic; the high-current returns (heater, relay,
  fans, blowers) kept off the path the logic, sensors and SD use. Is **4 layers** worth it over 2?
- **Power:** keep the S3's WiFi current bursts off the SD card's supply (see question 2 on
  separate regulators); decoupling at every IC; bulk capacitance where loads switch and at the module and SD
  card; the 12V input filtered so load surges don't reach the 3.3V side.
- **Switching parts:** any regulators (including the blower circuit) and the load MOSFETs kept away
  from the antenna, the sensor lines and the SD and LCD lines; flyback or snubbing on every
  inductive load.
- **Leads that leave the board:** the sensor leads run into the cabinet, the LCD cable to the lid —
  ESD and noise protection on them (TVS, series resistors, filtering where right), without spoiling
  the I²C and SPI signals.
- **The enclosure:** the SSR and PSU switch mains a few centimetres away. Anything in the board's
  layout or connector placement that keeps mains wiring away from the logic and sensor connectors.

### 4.8 Diodes

- D1 becomes **SB540** (same DO-201AD footprint). D7 too, if it survives the new USB power path.

## 5. Questions for your view and research

1. **Module variant** — N8R2, or something else? Which pins does its PSRAM use?
2. **Power tree** — where 3.3V comes from (from 5V, or straight from 12V), the regulator, and the
   USB power path (ideal diode, power-path IC, or a diode). **Separate regulators?** On the current
   board the ESP32's WiFi bursts share one 3.3V regulator with the SD card, mux, RTC and sensors,
   and they reset the SD card. Would you give the S3 its own regulator sized for WiFi bursts, and a
   separate quiet one for the SD card (and perhaps the sensors, mux and RTC)? Or another way — your
   judgement.
3. **Supervisor** — the part and threshold, and how it works with the EN capacitor and USB
   flashing.
4. **I²C buses** — we'd like the **RTC on its own bus**, so a faulty sensor holding the bus low
   can't take the clock down with it (the S3 has two I²C controllers). Is that worth it, and where
   does the touch controller go? Pull-up values for each bus.
5. **RTC** — part, cell, charger, backup time.
6. **LCD** — connector, cable, length and SPI speed (4.4).
7. **Blowers** — the voltage-control circuit (4.5).
8. **Electrical noise** — your approach (4.7), including 2 vs 4 layers.
9. **Anything else** you'd do differently, or think we've missed.

## 6. What we'll check at each review

- **Proposal:** every requirement in section 3 answered; the pin map against boot behaviour and
  the firmware; parts in JLCPCB stock.
- **Schematic:** the EN net (only its four parts); every load pin's default state; the interlock
  unchanged from Rev P; the USB power path; strapping pins; blowers full speed by default; ion
  generator off by default.
- **Layout:** components on top only; the antenna keep-out; connectors grouped; the outline and
  holes (we'll update the enclosure); test points reachable; the noise measures in 4.7 — ground
  plane, load return paths, decoupling placement.

## 7. First bring-up tests, for your awareness

These are the tests that depend on your design — not the full plan. Before the board goes in the
cabinet we'll run a full bring-up of every part, re-run our firmware tests on it and soak it. We'll
run these first, so please design so they can pass:

- Bare board: no shorts between rails.
- 12V only, no USB: it boots and runs.
- USB in and out while running, and the serial monitor opened and closed: no restart.
- USB and 12V in any order: safe.
- A supply dip: a clean reset.
- Every load quiet through boot; blowers at full speed from power-up; ion generator off.
- **A deep SD test with WiFi on all the time:** tens of thousands of large writes back to back,
  with **zero first-try failures**, plus a card pull. The current board failed about 1 in 316.
- **Free heap above 30 KB at all times,** including while WiFi connects (firmware, using the
  module's PSRAM).

**Tests you'd add.** Please suggest any bring-up checks your design needs — only ones that would
catch a real fault, each with what to measure and the expected value. Not tests for their own sake.