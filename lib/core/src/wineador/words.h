#pragma once
// The words the LCD and the dashboard show for the codes the logs and the API carry (spec 5.5
// "Words on screen"). The codes stay as they are (tools read them); only what a person reads is a
// word. Defined once here:
//   - the board's own sentences (event_sentence.cpp) use these functions;
//   - the dashboard uses web/words.js, which is made from these tables by
//     `python3 tools/make_web_words.py`; the native test test_web_words fails if it is out of date.
// The sensor positions' names come from the sensor table (sensor_table.cpp), their one source.

#include <stddef.h>

namespace wineador {

struct CodeWord { const char* code; const char* word; };

struct WordList {
  const char* name;        // its name in web/words.js
  const CodeWord* items;
  int count;
};

// Every list the page needs, in the order web/words.js gives them. "positions" is made from the
// sensor table and isn't one of these.
extern const WordList kWordLists[];
extern const int kWordListCount;

// The word for a code in one list; nullptr if the code isn't in it.
const char* wordFor(const char* list, const char* code);

// The word for a guard's code ("min-off" -> "rest"), or the code itself if unknown.
const char* guardWords(const char* code);
// The word for an exclusion reason ("crc" -> "bad reads"), or the code itself.
const char* exclusionWords(const char* code);
// The word for a restart reason ("POWERON" -> "Power on or reset"), or the code itself.
const char* restartWords(const char* code);
// The word for the ion generator's why ("boot hold" -> "starting"), or the text itself.
const char* ionWhyWords(const char* text);
// The word for the blowers' why ("no reading yet since boot" -> "no reading yet"), or the text.
const char* blowerWhyWords(const char* text);
// A sensor position's name ("mid_rear" -> "Middle rear"), or the code itself.
const char* positionName(const char* code);

// web/words.js, as the tool writes it and the test compares it.
int formatWebWordsJs(char* out, size_t n);

}  // namespace wineador
