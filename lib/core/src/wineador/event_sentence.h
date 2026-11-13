#pragma once
// Events as plain sentences for the dashboard (spec 5.5): "Humidifier called: RH 59.8%, below
// 60.0%", with the logged text shown beneath it. The card keeps the logged text (tools read it);
// the sentence is made only for the page, from the event's type and its logged detail, using the
// shared wording in event_text.h. Every event type has a sentence maker: test_event_text fails if
// a type is added to event_text.h without one.

#include <stddef.h>

namespace wineador {

// The sentence for an event. Always writes something (out is NUL-terminated); returns its length.
int eventSentence(char* out, size_t n, const char* type, const char* detail);

// True if this type has its own sentence maker (every type in kEventTypes must).
bool hasEventSentence(const char* type);

}  // namespace wineador
