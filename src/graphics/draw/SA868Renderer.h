// src/graphics/draw/SA868Renderer.h
#pragma once

#if HAS_SCREEN
#include <OLEDDisplay.h>
#include <OLEDDisplayUi.h>

namespace graphics
{

class SA868Renderer
{
  public:
    /**
     * @brief Draws the SA868-specific status frame.
     *
     * Shows frequency, TX/RX status, callsign, and basic node info.
     */
    static void drawSA868Frame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y);
};

} // namespace graphics
#endif
