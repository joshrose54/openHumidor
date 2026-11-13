// TEST BUILDS ONLY — the stage Q6 scenario.
//
//   run blowers  BP2 test 41 (Rev Q): the blower rule (4.6b) on the cabinet model. Nothing is asked
//                of the loads. Josh meters 12V (TP6) to BLW- (TP12) throughout:
//     1. wait for idle (after boot the rule is in boost until its hold of low spread has passed);
//     2. ch0 scripted 1.5 C above the rest: BOOST at the first sweep after it;
//     3. ch0 back: the spread is under 0.5 C; IDLE once the hold has passed (1 min on the bench),
//        not before;
//     4. a forced Fault - the chamber sensor (ch4) scripted 40 C high, so implausible and excluded:
//        FULL SPEED at once;
//     5. ch4 back: it recovers (10 good reads, then the 4.3 recovery hold), the rule starts in
//        boost, and IDLE after the hold again. PASS.
#include "wineador/scenario.h"

#if defined(WINEADOR_TEST_BUILD)

#include <stdio.h>

namespace wineador {

namespace {
const char* mmss(char* out, size_t n, uint32_t ms) {
  const uint32_t s = (ms + 500) / 1000;
  snprintf(out, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return out;
}
}  // namespace

void ScenarioRunner::runBlowers(const ScenarioView& v, CabinetModel& model) {
  const uint32_t now = v.nowMs;
  const BlowerRule* b = v.blowers;
  if (!b) { finish(now, model, Result::Fail, "no blowers on this board (Rev Q only)"); return; }
  const uint32_t hold = s_.tuning.blowerIdleHoldMs;
  const uint32_t read = s_.tuning.sensorReadMs;
  char a[16], c[16];
  switch (phase_) {
    case 0:
      say(now, "TEST 41 - the blower rule (4.6b). No load is asked for. Meter 12V (TP6) to BLW- (TP12) throughout.");
      say(now, "Idle duty %u%%%s; boost above %.1f C or %.0f points; idle after %s below %.1f C and %.0f points.",
          (unsigned)s_.tuning.blowerIdlePct, s_.tuning.blowerIdlePct >= 100 ? " (full speed: set it from test 22 first)" : "",
          (double)s_.tuning.blowerBoostAboveC, (double)s_.tuning.blowerBoostAboveRhPct, mmss(a, sizeof(a), hold),
          (double)s_.tuning.blowerIdleBelowC, (double)s_.tuning.blowerIdleBelowRhPct);
      deadlineMs_ = hold + 4 * read + 60000;
      phase(1, now);
      break;
    case 1:   // 1. idle first
      if (b->mode() == BlowerMode::Idle) {
        say(now, "1. IDLE: blowers at %u%% - record TP6 to TP12 now.", (unsigned)b->pct());
        say(now, "2. ch0 (top front) scripted 1.5 C above the rest: the blowers go to BOOST at the next sweep.");
        model.setOffset(0, 1.5f, 0);
        phase(2, now);
      } else if (elapsedMs(now, phaseAtMs_) > deadlineMs_) {
        finish(now, model, Result::Fail, "the blowers never reached idle (is a sensor out, or a fault active?)");
      }
      break;
    case 2:   // 2. boost at once
      if (b->mode() == BlowerMode::Boost) {
        say(now, "BOOST after %s (spread %.2f C, %.1f points - the warm sensor reads drier too): full speed - record TP6 "
            "to TP12 (11.5 V or more).", mmss(a, sizeof(a), elapsedMs(now, phaseAtMs_)), (double)b->lastSpreadC(),
            (double)b->lastSpreadRhPct());
        if (elapsedMs(now, phaseAtMs_) > 2 * read + 2000) { finish(now, model, Result::Fail, "boost came later than the next sweep"); break; }
        say(now, "3. ch0 back with the rest: spread under 0.5 C. IDLE after %s of low spread, not before.", mmss(a, sizeof(a), hold));
        model.setOffset(0, 0, 0);
        markMs_ = 0;
        phase(3, now);
      } else if (b->mode() != BlowerMode::Idle) {
        finish(now, model, Result::Fail, "the blowers went to full speed, not boost (a fault?)");
      } else if (elapsedMs(now, phaseAtMs_) > 2 * read + 2000) {
        finish(now, model, Result::Fail, "no boost at the next sweep after a 1.5 C spread");
      }
      break;
    case 3:   // 3. back to idle after the hold, not before
      if (b->holding() && markMs_ == 0) {
        markMs_ = now ? now : 1;
        say(now, "spread %.2f C, %.1f points: low; the %s hold starts", (double)b->lastSpreadC(), (double)b->lastSpreadRhPct(),
            mmss(a, sizeof(a), hold));
      }
      if (b->mode() == BlowerMode::Idle) {
        const uint32_t held = markMs_ ? elapsedMs(now, markMs_) : 0;
        if (markMs_ == 0 || held + 50 < hold) { finish(now, model, Result::Fail, "idle before the hold of low spread had passed"); break; }
        say(now, "IDLE after %s of low spread - correct. Blowers %u%%: record TP6 to TP12.", mmss(a, sizeof(a), held), (unsigned)b->pct());
        say(now, "4. A forced FAULT: the chamber sensor (ch4) scripted 40 C high (implausible, excluded). FULL SPEED at once.");
        model.setOffset(kChamberIndex, 40.0f, 0);
        phase(4, now);
      } else if (elapsedMs(now, phaseAtMs_) > hold + 4 * read + 30000) {
        finish(now, model, Result::Fail, "the blowers did not go back to idle");
      }
      break;
    case 4:   // 4. full speed in the fault
      if (b->mode() == BlowerMode::Full && b->why() == BlowerWhy::Fault) {
        say(now, "FULL SPEED after %s: fault - record TP6 to TP12 (11.5 V or more).", mmss(a, sizeof(a), elapsedMs(now, phaseAtMs_)));
        say(now, "5. ch4 back: it recovers after 10 good reads, then the %s recovery hold; then boost, and idle after %s.",
            mmss(a, sizeof(a), s_.safety.sensorRecoveryHoldMs), mmss(c, sizeof(c), hold));
        model.setOffset(kChamberIndex, 0, 0);
        flag_ = false;
        phase(5, now);
      } else if (elapsedMs(now, phaseAtMs_) > 2 * read + 2000) {
        finish(now, model, Result::Fail, "not full speed at the next sweep in a fault");
      }
      break;
    case 5:   // 5. the fault over: boost first, then idle
      if (!flag_ && b->mode() == BlowerMode::Boost) {
        flag_ = true;
        say(now, "fault over after %s: BOOST first (after a spell at full speed) - correct", mmss(a, sizeof(a), elapsedMs(now, phaseAtMs_)));
      }
      if (b->mode() == BlowerMode::Idle) {
        if (!flag_) { finish(now, model, Result::Fail, "went to idle after the fault without boost first"); break; }
        say(now, "IDLE again - blowers %u%%.", (unsigned)b->pct());
        finish(now, model, Result::Pass, "boost at once on a wide spread; idle after the hold; full speed in a fault; boost, then idle, after it");
      } else if (elapsedMs(now, phaseAtMs_) > 12 * read + s_.safety.sensorRecoveryHoldMs + hold + 90000) {
        finish(now, model, Result::Fail, "the blowers did not get back to idle after the fault");
      }
      break;
    default: break;
  }
}

}  // namespace wineador

#endif  // WINEADOR_TEST_BUILD
