#pragma once
#include <SDL.h>
#include <string>

// A tiny self-contained 5x7 bitmap font renderer. No external font files are
// required, which keeps the whole game asset-free and trivially portable.
class Font {
public:
    static constexpr int GLYPH_W = 5;
    static constexpr int GLYPH_H = 7;

    // Draw a string at (x, y). `scale` is the pixel size of each font dot.
    static void draw(SDL_Renderer* r, const std::string& text, int x, int y,
                     int scale, SDL_Color color);

    // Draw centered horizontally around `cx`.
    static void drawCentered(SDL_Renderer* r, const std::string& text, int cx,
                             int y, int scale, SDL_Color color);

    static int textWidth(const std::string& text, int scale);
    static int textHeight(int scale) { return GLYPH_H * scale; }
};
