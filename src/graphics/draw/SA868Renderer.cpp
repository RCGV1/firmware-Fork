// src/graphics/draw/SA868Renderer.cpp
#include "SA868Renderer.h"
#include "configuration.h"

#if HAS_SCREEN
#include "MeshService.h"
#include "NodeDB.h"
#include "Router.h"
#include "UIRenderer.h"
#include "graphics/Screen.h"
#include "graphics/ScreenFonts.h"
#include "graphics/SharedUIDisplay.h"
#include <Arduino.h>

namespace graphics
{

void SA868Renderer::drawSA868Frame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->clear();

    // 1. Draw Common Header (Battery, Time, etc.)
    drawCommonHeader(display, x, y, "SA868 AFSK", false, true);

    // 2. Frequency (Large and Prominent)
    char freqStr[16];
    float freq = SA868_DEFAULT_FREQUENCY_MHZ;
    if (config.lora.override_frequency > 0) {
        freq = config.lora.override_frequency;
    }
    snprintf(freqStr, sizeof(freqStr), "%.3f", freq);

    display->setFont(FONT_LARGE);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(x + (SCREEN_WIDTH / 2), y + 20, freqStr);

    display->setFont(FONT_SMALL);
    display->drawString(x + (SCREEN_WIDTH / 2), y + 44, "MHz");

    // 3. TX / RX Status Indicators
    display->setTextAlignment(TEXT_ALIGN_LEFT); // Draw TX/RX status
    bool isTX = (router && router->isSending());

    // For RX, we check the busy pin defined in variant.h
    // Note: SA868_BUSY_PIN is active HIGH when signal is present.
    bool isRX = (digitalRead(SA868_BUSY_PIN) == HIGH);

    if (isTX) {
        display->fillRect(x + 2, y + 22, 24, 12);
        display->setColor(BLACK);
        display->drawString(x + 5, y + 22, "TX");
        display->setColor(WHITE);
    } else if (isRX) {
        display->fillRect(x + 2, y + 22, 24, 12);
        display->setColor(BLACK);
        display->drawString(x + 5, y + 22, "RX");
        display->setColor(WHITE);
    }

    // 4. Callsign & Ham Mode
    const char *longName = devicestate.owner.long_name;
    if (longName && longName[0] != '\0') {
        display->setTextAlignment(TEXT_ALIGN_LEFT);
        display->drawString(x + 5, y + 50, longName);
    }

    if (devicestate.owner.is_licensed) {
        display->setTextAlignment(TEXT_ALIGN_RIGHT);
        display->drawString(x + SCREEN_WIDTH - 5, y + 50, "HAM");
    }

    // 5. Node Info (Standard Meshtastic info)
    char nodeInfo[32];
    int nodesOnline = nodeStatus->getNumOnline();
    snprintf(nodeInfo, sizeof(nodeInfo), "Nodes: %d", nodesOnline);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->drawString(x + 5, y + 10, nodeInfo);

    drawCommonFooter(display, x, y);
}

} // namespace graphics
#endif
