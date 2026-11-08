# Wineador controller: changes for the next board

Draft, to send after B4. Compared with **Rev P**.

## Approach

- **Everything that can be SMD, and that JLCPCB stocks, becomes SMD.** Anything JLCPCB can't
  supply as SMD, or that has to stay through-hole, stays through-hole. JLCPCB assembles the SMD
  parts.
- **SMD parts on one side of the board only.**
- **Mounting holes stay exactly where they are:** they fit the standoffs in the enclosure. Change
  the board's outline only as a last resort.
- No S3 DevKit step. Go straight to the SMD design.

## 1. The ESP32 as an SMD module, with USB

- Replace the plug-in ESP32 DevKit with an **ESP32-S3-WROOM-1 module soldered directly onto the
  board by JLCPCB** (chip, flash and antenna built in). Nothing plugs in. Not the bare chip.
  Variant: we suggest **N8R2** (8MB flash, 2MB PSRAM) for the LCD and web dashboard — confirm or
  recommend otherwise.
- **USB must use the S3's built-in USB** (its USB D− and D+ pins, GPIO19 and GPIO20) wired to the
  USB-C socket. **No USB-to-serial chip (CP2102, CH340 or similar) and no auto-reset transistors.**
  Flashing then needs no buttons and no auto-reset circuit, and nothing on the USB side touches EN.
  This is the main fix for the first two problems below.
- **A USB-C socket for flashing and the serial monitor.** USB and 12V must still be safe to plug in
  and pull out in any order, protecting the board and the laptop, as Rev P does with D7.
- **Plugging USB into a running board must not restart it,** and nor must opening or closing the
  serial monitor: in the cabinet, connecting a laptop must not restart the controller and its
  lock-out timers.
- **The board must run with no USB connected.** The unmodified DevKit can't: powered from its 5V
  pin with no USB, its USB-serial chip and auto-reset circuit hold EN at ~0V (the ESP32 never
  starts) and GPIO0 low (it boots into flashing mode) — two DevKits, same result, off the board. We
  fixed it on the current boards with 2.2kΩ pull-ups on EN and GPIO0. With the S3's built-in USB
  (above) there is no such circuit; please confirm nothing on the USB side can hold EN or GPIO0
  low when USB is absent.
- **The board must recover by itself from a supply dip:** a clean reset, never a hang. On the
  current board, a sharp disturbance (a 21W bulb switching on, with USB out) once froze the ESP32
  in a state its own watchdogs couldn't catch; only an external EN reset recovered it.
- **A supply supervisor (reset chip) on EN**, holding EN low cleanly whenever 3.3V is out of range
  and releasing it only once 3.3V is good. Please choose the part and threshold for the module,
  and confirm it works with the EN capacitor and the USB flashing reset.
- This removes the DevKit's AMS1117 regulator and D7's voltage drop. Please give the board a proper
  3.3V regulator with good headroom.
- Keep the module's antenna end at the board edge with nothing under it.
- **Support circuit checklist.** Please confirm each is covered:
  - 3.3V regulator with good headroom for WiFi current bursts, with input and output capacitors
  - Decoupling capacitors at the module's power pins
  - Reset (EN) circuit: pull-up and capacitor, plus a reset button
  - BOOT button on GPIO0
  - USB-C socket, with **5.1kΩ resistors on CC1 and CC2** so a USB-C-to-C cable works
  - ESD protection on the USB data lines
  - USB power-path diode or ideal diode, so USB never powers the board's rails and the board never
    feeds the laptop
  - Strapping pins held in their correct boot states
  - **EN connects only to its pull-up, its capacitor, the reset button and the supervisor (below)**
    — nothing else
- **Question:** we'd like the **RTC on its own I²C bus**, separate from the mux and sensors, so a
  faulty sensor holding the bus low can't take the clock down with it. Both the ESP32 and the
  ESP32-S3 have two I²C controllers. Is that worth doing?

## 2. The I²C mux on the board, SMD

- Replace the plug-in TCA9548A module (HW-617) with the chip on the board.
- The module currently provides **10kΩ pull-ups on the main SDA and SCL**, a **10kΩ pull-up on
  RST**, and the address pins. The board needs its own versions, plus decoupling. **Address stays
  0x70.** RST stays on a GPIO (currently P16) for bus-fault recovery.
- The sensor channels stay **without** pull-ups: each SHT40 probe has its own 10kΩ inside.

## 3. An RTC on the board, with a backup cell that charges itself

- The current DS3231 module (HW-084) runs on an LIR2032 rechargeable cell, but its charge circuit (a
  resistor and diode) doesn't fully charge the cell from 3.3V. The cell went flat and the clock
  lost its time.
- Please propose an on-board RTC with a backup cell and **a charge circuit that works from 3.3V**.
  State the **cell, the charge circuit and the expected backup time** with the board unpowered.
- The firmware must be able to read an **"oscillator stopped" flag**, to know the time was lost,
  as it does on the DS3231.
- **Pull-ups:** the DS3231 module also carries **4.7kΩ pull-ups on the main I²C bus**. With it and
  the mux module both gone, **the board needs its own main-bus pull-ups.** 4.7kΩ is our suggestion,
  but choose the value for the bus as designed. If the RTC gets its own bus (item 1), that bus needs
  pull-ups too.

## 4. LCD with its own SD card, replacing the SD module

- Add the **lcdwiki 4.0" ST7796 capacitive-touch SPI display**. It has its own SD card slot.
- **Remove the SD card module (U4).** Logging moves to the LCD's SD card.
- The LCD mounts on the enclosure and connects to the board through **a single JST XH connector**.
  Please confirm the pin count it needs: display, touch, SD and backlight.

## 5. D1 and D7 become SB540

Same DO-201AD footprint. (If D7 is replaced by an ideal-diode part in the SMD design, this applies
to D1 only.)

## Still to add

- Anything B3 or B4 turns up.