#pragma once
#include "WadaPlatform.h"
#include "helpers/ui/DisplayDriver.h"
#include <Adafruit_ST7789.h>
#include <SPI.h>
class ST7789LCDDisplay : public DisplayDriver
{
    Adafruit_ST7789 panel{&SPI, 12, 11, -1};
    bool on = true;
    uint16_t color = 0xffff;
    struct Guard {
        Guard() { wadaSpiLock(); }
        ~Guard() { wadaSpiUnlock(); }
    };

  public:
    ST7789LCDDisplay() : DisplayDriver(240, 320) {}
    bool begin()
    {
        Guard g;
        panel.init(240, 320);
        panel.setRotation(0);
        pinMode(42, OUTPUT);
        digitalWrite(42, HIGH);
        return true;
    }
    bool isOn() override { return on; }
    void turnOn() override
    {
        on = true;
        digitalWrite(42, HIGH);
    }
    void turnOff() override
    {
        on = false;
        digitalWrite(42, LOW);
    }
    void clear() override
    {
        Guard g;
        panel.fillScreen(0);
    }
    void startFrame(ColorVal c = 0) override
    {
        Guard g;
        panel.fillScreen(c);
    }
    void setTextSize(int n) override { panel.setTextSize(n); }
    void setColor(ColorVal c) override
    {
        color = c;
        panel.setTextColor(c);
    }
    void setCursor(int x, int y) override { panel.setCursor(x, y); }
    void print(const char *t) override
    {
        Guard g;
        panel.print(t);
    }
    void fillRect(int x, int y, int w, int h) override
    {
        Guard g;
        panel.fillRect(x, y, w, h, color);
    }
    void drawRect(int x, int y, int w, int h) override
    {
        Guard g;
        panel.drawRect(x, y, w, h, color);
    }
    void drawXbm(int x, int y, const uint8_t *data, int w, int h) override
    {
        Guard g;
        panel.drawBitmap(x, y, data, w, h, color);
    }
    uint16_t getTextWidth(const char *t) override
    {
        int16_t x, y;
        uint16_t w, h;
        panel.getTextBounds(t, 0, 0, &x, &y, &w, &h);
        return w;
    }
    void endFrame() override {}
    void setDisplayRotation(uint8_t r)
    {
        Guard g;
        panel.setRotation(r);
        setLogicalSize(panel.width(), panel.height());
    }
    void writePixelsRGB565(int x, int y, int w, int h, const uint16_t *pixels)
    {
        Guard g;
        panel.startWrite();
        panel.setAddrWindow(x, y, w, h);
        panel.writePixels(const_cast<uint16_t *>(pixels), w * h);
        panel.endWrite();
    }
};
