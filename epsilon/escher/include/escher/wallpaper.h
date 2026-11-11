#pragma once

#include <kandinsky/context.h>

/* Custom wallpaper for the home screen.
 * The wallpaper is a full-screen (320x240) RGB565 image, anchored to the
 * absolute screen coordinates, so it stays still while the app grid scrolls. */

namespace Escher {
namespace Wallpaper {

constexpr KDCoordinate k_width = 320;
constexpr KDCoordinate k_height = 240;

// Color of the wallpaper at an absolute screen position
KDColor PixelAt(KDCoordinate absoluteX, KDCoordinate absoluteY);

// Fill `rect` (in the coordinates of the view being drawn) with the wallpaper
void Draw(KDContext* ctx, KDRect rect);

}  // namespace Wallpaper
}  // namespace Escher
