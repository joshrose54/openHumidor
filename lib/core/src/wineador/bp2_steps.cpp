#include "wineador/bp2_steps.h"
#include <string.h>

namespace wineador {

StepSequencer::Event StepSequencer::handle(const char* w) {
  if (!w || !w[0]) return Event::None;
  if (strcmp(w, "x") == 0) {                 // abort, always
    state_ = State::Waiting;
    step_ = -1;
    return Event::Abort;
  }
  if (state_ == State::Waiting) {
    if (strcmp(w, "g") != 0 || steps_ <= 0) return Event::Unknown;
    state_ = State::Running;
    step_ = 0;
    return Event::Start;
  }
  if (strcmp(w, "n") != 0) return Event::Unknown;
  if (step_ + 1 < steps_) {
    step_++;
    return Event::Step;
  }
  state_ = State::Waiting;                    // past the last step: finished, everything off
  step_ = -1;
  return Event::Finish;
}

}  // namespace wineador
