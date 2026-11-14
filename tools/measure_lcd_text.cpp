#include "lcdgfx/canvas.h"
#include <stdio.h>
#include <string.h>
using namespace lcdgfx;
// stdin lines: S|text  or M|text  or L|text ; prints width<TAB>missing-glyphs<TAB>text
int main() {
  char line[512];
  while (fgets(line, sizeof line, stdin)) {
    line[strcspn(line, "\n")] = 0;
    if (!line[0] || line[1] != '|') continue;
    const Font& f = line[0]=='S' ? kFontSmall : line[0]=='M' ? kFontMedium : line[0]=='L' ? kFontLarge : kFontHuge;
    const char* t = line + 2;
    // check glyphs (UTF-8 decode)
    int miss = 0;
    for (const unsigned char* p=(const unsigned char*)t; *p;) {
      uint32_t cp; int k;
      if (*p<0x80){cp=*p;k=1;} else if((*p&0xE0)==0xC0){cp=((p[0]&31)<<6)|(p[1]&63);k=2;} else {cp=((p[0]&15)<<12)|((p[1]&63)<<6)|(p[2]&63);k=3;}
      if (!findGlyph(f, cp)) miss++;
      p+=k;
    }
    printf("%d\t%d\t%s\n", Canvas::measure(f, t), miss, line);
  }
}
