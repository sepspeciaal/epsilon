#include <escher/wallpaper.h>
#include <kandinsky/font.h>
#include <omg/utf8_decoder.h>

#include <algorithm>

#include "wallpaper_data.h"

namespace Escher {
namespace Wallpaper {

static const KDColor* Pixels() {
  static_assert(sizeof(KDColor) == sizeof(uint16_t));
  return reinterpret_cast<const KDColor*>(k_data);
}

KDColor PixelAt(KDCoordinate absoluteX, KDCoordinate absoluteY) {
  if (absoluteX < 0 || absoluteX >= k_width || absoluteY < 0 ||
      absoluteY >= k_height) {
    return KDBlack;
  }
  return Pixels()[absoluteY * k_width + absoluteX];
}

void Draw(KDContext* ctx, KDRect rect) {
  KDPoint origin = ctx->origin();
  KDRect absoluteRect = rect.translatedBy(origin)
                            .intersectedWith(ctx->clippingRect())
                            .intersectedWith(KDRect(0, 0, k_width, k_height));
  if (absoluteRect.isEmpty()) {
    return;
  }
  const KDColor* pixels = Pixels();
  for (KDCoordinate j = 0; j < absoluteRect.height(); j++) {
    KDCoordinate y = absoluteRect.y() + j;
    KDRect row = KDRect(absoluteRect.x() - origin.x(), y - origin.y(),
                        absoluteRect.width(), 1);
    ctx->fillRectWithPixels(row, pixels + y * k_width + absoluteRect.x(),
                            nullptr);
  }
}

static uint8_t GlyphCoverage(const uint8_t* grayscales, int pixelIndex) {
  constexpr int bits = k_grayscaleBitsPerPixel;
  constexpr int pixelsPerByte = 8 / bits;
  constexpr uint8_t mask = 0xFF >> (8 - bits);
  uint8_t byte = grayscales[pixelIndex / pixelsPerByte];
  int shift = (pixelsPerByte - 1 - pixelIndex % pixelsPerByte) * bits;
  uint8_t value = (byte >> shift) & mask;
  return (value * 255) / mask;
}

void DrawText(KDContext* ctx, const char* text, KDPoint textOrigin,
              KDColor textColor, KDColor outlineColor) {
  if (text == nullptr) {
    return;
  }
  constexpr KDFont::Size fontSize = KDFont::Size::Small;
  constexpr int glyphWidth = KDFont::GlyphMaxWidth(fontSize);
  constexpr int glyphHeight = KDFont::GlyphHeight(fontSize);
  // 1 pixel of outline on each side
  constexpr int k_maxWidth = 120;
  constexpr int k_height = glyphHeight + 2;
  uint8_t coverage[k_maxWidth * k_height] = {};

  const KDFont* font = KDFont::Font(fontSize);
  KDFont::GlyphBuffer glyphBuffer;
  int x = 1;
  int lastGlyphX = 1;
  UTF8Decoder decoder(text);
  CodePoint codePoint = decoder.nextCodePoint();
  while (codePoint != UCodePointNull) {
    bool combining = codePoint.isCombining();
    int glyphX = combining ? lastGlyphX : x;
    if (glyphX + glyphWidth + 1 > k_maxWidth) {
      break;
    }
    font->setGlyphGrayscalesForCodePoint(codePoint, &glyphBuffer);
    const uint8_t* grayscales = glyphBuffer.grayscaleBuffer();
    for (int j = 0; j < glyphHeight; j++) {
      for (int i = 0; i < glyphWidth; i++) {
        uint8_t value = GlyphCoverage(grayscales, j * glyphWidth + i);
        uint8_t* target = &coverage[(j + 1) * k_maxWidth + glyphX + i];
        *target = std::max(*target, value);
      }
    }
    if (!combining) {
      lastGlyphX = x;
      x += KDFont::GlyphWidth(fontSize, codePoint);
    }
    codePoint = decoder.nextCodePoint();
  }
  int width = std::min(x + 1, k_maxWidth);

  KDColor pixels[k_maxWidth * k_height];
  KDPoint origin = ctx->origin();
  KDCoordinate left = textOrigin.x() - 1;
  KDCoordinate top = textOrigin.y() - 1;
  for (int j = 0; j < k_height; j++) {
    for (int i = 0; i < width; i++) {
      uint8_t outline = 0;
      for (int dj = -1; dj <= 1; dj++) {
        for (int di = -1; di <= 1; di++) {
          int ni = i + di;
          int nj = j + dj;
          if (ni >= 0 && ni < width && nj >= 0 && nj < k_height) {
            outline = std::max(outline, coverage[nj * k_maxWidth + ni]);
          }
        }
      }
      KDColor color =
          PixelAt(origin.x() + left + i, origin.y() + top + j);
      color = KDColor::Blend(outlineColor, color, outline);
      color = KDColor::Blend(textColor, color, coverage[j * k_maxWidth + i]);
      pixels[j * width + i] = color;
    }
  }
  ctx->fillRectWithPixels(KDRect(left, top, width, k_height), pixels, nullptr);
}

}  // namespace Wallpaper
}  // namespace Escher
