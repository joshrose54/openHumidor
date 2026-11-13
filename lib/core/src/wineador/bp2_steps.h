#pragma once
// The BP2 sketches' go-ahead and steps (CLAUDE.md, test sketches): plain logic, tested on the Mac.
//   - Nothing happens until "g" (Josh meters first). g applies step 1.
//   - Each "n" applies the next step; "n" after the last step finishes (everything off).
//   - "x" at any time aborts: everything off.
//   - Anything else is ignored (the sketch says so). A finished or aborted run starts again with "g".
// The sketch does what each event says; this only decides which event a typed word is.

#include <stdint.h>

namespace wineador {

class StepSequencer {
 public:
  enum class Event : uint8_t { None, Start, Step, Finish, Abort, Unknown };

  void begin(int steps) { steps_ = steps; step_ = -1; state_ = State::Waiting; }
  // One typed word (already lower case). Returns what to do; step() is then the step to apply.
  Event handle(const char* word);

  int step() const { return step_; }           // 0-based; -1 before the first
  int steps() const { return steps_; }
  bool waiting() const { return state_ == State::Waiting; }
  bool running() const { return state_ == State::Running; }

 private:
  enum class State : uint8_t { Waiting, Running };
  State state_ = State::Waiting;
  int steps_ = 0, step_ = -1;
};

}  // namespace wineador
