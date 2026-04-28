#include <M5Unified.h>

void setup() {
  M5.begin();
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("dooard", 10, 10);
}

void loop() {
  M5.update();
  delay(50);
}
