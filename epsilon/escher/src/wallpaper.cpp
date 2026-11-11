#include <escher/wallpaper.h>

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

}  // namespace Wallpaper
}  // namespace Escher
