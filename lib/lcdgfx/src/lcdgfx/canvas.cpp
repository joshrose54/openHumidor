#include "lcdgfx/canvas.h"
#include <string.h>

namespace lcdgfx {

uint16_t blend565(uint16_t bg, uint16_t fg, uint8_t a) {
  if (a >= 15) return fg;
  if (a == 0) return bg;
  const uint32_t ia = 15 - a;
  const uint32_t r = (((fg >> 11) & 31) * a + ((bg >> 11) & 31) * ia + 7) / 15;
  const uint32_t g = (((fg >> 5) & 63) * a + ((bg >> 5) & 63) * ia + 7) / 15;
  const uint32_t b = ((fg & 31) * a + (bg & 31) * ia + 7) / 15;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

static Rect intersect(Rect a, Rect b) {
  const int16_t x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
  const int16_t x1 = (a.x + a.w) < (b.x + b.w) ? (int16_t)(a.x + a.w) : (int16_t)(b.x + b.w);
  const int16_t y1 = (a.y + a.h) < (b.y + b.h) ? (int16_t)(a.y + a.h) : (int16_t)(b.y + b.h);
  if (x1 <= x0 || y1 <= y0) return Rect(0, 0, 0, 0);
  return Rect(x0, y0, (int16_t)(x1 - x0), (int16_t)(y1 - y0));
}

void Canvas::setClip(Rect r) { clip_ = intersect(r, Rect(0, 0, w_, h_)); }

void Canvas::fill(Rect r, uint16_t c) {
  const Rect k = intersect(r, clip_);
  for (int16_t y = k.y; y < k.y + k.h; y++) {
    uint16_t* p = px_ + (int32_t)y * w_ + k.x;
    for (int16_t x = 0; x < k.w; x++) p[x] = c;
  }
  writes_ += (uint32_t)k.area();
}

// Coverage of corner pixel (dx, dy) by the rounded corner of radius r, from 4 x 4 samples at
// each sub-pixel's centre (in eighths of a pixel): 0..15, 15 = fully inside.
static uint8_t cornerCover(int dx, int dy, int r) {
  const int c = r * 8, r2 = c * c;
  int inside = 0;
  for (int sy = 0; sy < 4; sy++)
    for (int sx = 0; sx < 4; sx++) {
      const int ex = c - (dx * 8 + sx * 2 + 1), ey = c - (dy * 8 + sy * 2 + 1);
      if (ex * ex + ey * ey <= r2) inside++;
    }
  return (uint8_t)((inside * 15 + 8) / 16);
}

void Canvas::roundRect(Rect r, int16_t radius, uint16_t c) {
  if (r.empty()) return;
  if (radius * 2 > r.w) radius = (int16_t)(r.w / 2);
  if (radius * 2 > r.h) radius = (int16_t)(r.h / 2);
  if (radius <= 0) { fill(r, c); return; }
  // The middle band and the two side bands are plain fills.
  fill(Rect(r.x, (int16_t)(r.y + radius), r.w, (int16_t)(r.h - 2 * radius)), c);
  fill(Rect((int16_t)(r.x + radius), r.y, (int16_t)(r.w - 2 * radius), radius), c);
  fill(Rect((int16_t)(r.x + radius), (int16_t)(r.y + r.h - radius), (int16_t)(r.w - 2 * radius), radius), c);
  // The four corners, blended pixel by pixel.
  for (int dy = 0; dy < radius; dy++)
    for (int dx = 0; dx < radius; dx++) {
      const uint8_t a = cornerCover(dx, dy, radius);
      if (!a) continue;
      const int16_t xs[2] = {(int16_t)(r.x + dx), (int16_t)(r.x + r.w - 1 - dx)};
      const int16_t ys[2] = {(int16_t)(r.y + dy), (int16_t)(r.y + r.h - 1 - dy)};
      for (int16_t yy : ys)
        for (int16_t xx : xs) {
          if (!clip_.contains(xx, yy)) continue;
          uint16_t& p = px_[(int32_t)yy * w_ + xx];
          p = blend565(p, c, a);
          writes_++;
        }
    }
}

void Canvas::mask4(int16_t x, int16_t y, uint16_t w, uint16_t h, const uint8_t* bits, uint16_t c) {
  const int stride = (w + 1) / 2;
  for (int row = 0; row < h; row++) {
    const int16_t py = (int16_t)(y + row);
    if (py < clip_.y || py >= clip_.y + clip_.h) continue;
    const uint8_t* src = bits + row * stride;
    for (int col = 0; col < w; col++) {
      const int16_t pxl = (int16_t)(x + col);
      if (pxl < clip_.x || pxl >= clip_.x + clip_.w) continue;
      const uint8_t b = src[col >> 1];
      const uint8_t a = (col & 1) ? (b & 0x0F) : (b >> 4);
      if (!a) continue;
      uint16_t& p = px_[(int32_t)py * w_ + pxl];
      p = blend565(p, c, a);
      writes_++;
    }
  }
}

void Canvas::icon(const Icon& ic, int16_t x, int16_t y, uint16_t c) { mask4(x, y, ic.w, ic.h, ic.bits, c); }
void Canvas::bitmap(const Bitmap& b, int16_t x, int16_t y, uint16_t c) { mask4(x, y, b.w, b.h, b.bits, c); }

uint32_t nextCodepoint(const char** sp) {
  const unsigned char* s = (const unsigned char*)*sp;
  if (!*s) return 0;
  uint32_t cp;
  int extra;
  if (s[0] < 0x80) { cp = s[0]; extra = 0; }
  else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; extra = 1; }
  else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; extra = 2; }
  else { cp = s[0] & 0x07; extra = 3; }
  int i = 1;
  for (; i <= extra && s[i]; i++) cp = (cp << 6) | (s[i] & 0x3F);
  *sp = (const char*)(s + i);
  return cp;
}

const Glyph* findGlyph(const Font& f, uint32_t cp) {
  int lo = 0, hi = f.count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (f.glyphs[mid].codepoint == cp) return &f.glyphs[mid];
    if (f.glyphs[mid].codepoint < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return nullptr;
}

int16_t Canvas::text(const Font& f, int16_t x, int16_t baseline, const char* s, uint16_t c) {
  const int16_t x0 = x;
  for (uint32_t cp; (cp = nextCodepoint(&s)) != 0;) {
    const Glyph* g = findGlyph(f, cp);
    if (!g) continue;
    if (g->w) mask4((int16_t)(x + g->xoff), (int16_t)(baseline + g->yoff), g->w, g->h, f.bits + g->offset, c);
    x = (int16_t)(x + g->advance);
  }
  return (int16_t)(x - x0);
}

int16_t Canvas::measure(const Font& f, const char* s) {
  int16_t w = 0;
  for (uint32_t cp; (cp = nextCodepoint(&s)) != 0;) {
    const Glyph* g = findGlyph(f, cp);
    if (g) w = (int16_t)(w + g->advance);
  }
  return w;
}

void fitText(char* out, size_t n, const Font& f, const char* s, int16_t maxW) {
  if (n == 0) return;
  strncpy(out, s, n - 1);
  out[n - 1] = 0;
  if (Canvas::measure(f, out) <= maxW) return;
  size_t len = strlen(out);
  while (len > 0) {
    len--;
    while (len > 0 && (out[len] & 0xC0) == 0x80) len--;      // never cut inside a UTF-8 character
    while (len > 0 && out[len - 1] == ' ') len--;
    if (len + 4 > n) continue;
    memcpy(out + len, "...", 4);
    if (Canvas::measure(f, out) <= maxW) return;
  }
  out[0] = 0;
}

}  // namespace lcdgfx
