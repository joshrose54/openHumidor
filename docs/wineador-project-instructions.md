# Wineador — how we work

## Two documents, two jobs

- **`wineador-spec.md` — the design.** Parts and their details, wiring and build, the rules and
  constraints the hardware and firmware must follow, and the tests (procedure, pass criteria,
  status). No results, no history, no "considered and not adopted", no narrative. A reason only
  where a rule would otherwise look arbitrary — one line, enough to write firmware from, no more.
- **`wineador-build-log.md` — the test record.** In spec test order: measured results from the
  units actually built, and anything a test needed before it passed. Also as-built facts (serials,
  calibration values, measured board values), Track A data, open issues, and gotchas not covered
  by the spec.

## Logging rules

- Log only tests that passed. Failed attempts and superseded runs are not recorded
- No dates on test results. Dates only where they date a reading — RTC drift, a battery voltage
- If a fix needed a hardware change — a new part, a different value — update the spec and rewrite
  the test there, then log the change against the test
- If a fix was in software and the firmware needs to know it, log the problem and its solution once
  the test has been re-run and passed
- Each figure lives in one place. The spec points to the log for measured values and never repeats
  them

## Before changing either document

- Discuss first when asked, or when a change affects the design. Don't amend the spec without
  agreement
- After any change, send the updated file. Never leave my copy out of date
- When restructuring, write new files and move the old ones to `archive/`. Never overwrite without
  a copy

## Working style

- If unsure, ask. Don't answer with false confidence
- Bench instructions are step by step and plain, saying what to record and in what form
- Test sketches wait for a go-ahead before switching anything — e.g. "type g to start" — so meter
  checks can be done first
- Bench instructions state the power-up order. On the Rev N board: keep 12V on whenever USB is
  connected — 12V on before USB in, USB out before 12V off; flashing on the board is then fine. Any
  test that cuts the 12V is run with USB unplugged first. On Rev P (with D7), any order is safe.
  On Rev Q, USB powers nothing: any order is safe, and flashing and the monitor need 12V on
- Check facts against the current documents before stating them, and say so when something was
  wrong
