#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>
#define TFT_SCLK 4
#define TFT_MOSI 5
#define TFT_RST  1
#define TFT_DC   2
#define TFT_CS   3
#define TFT_BL   10
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("Starting Adafruit ST7789 Test");
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  tft.init(240, 240);
  Serial.println("TFT Initialized");
  digitalWrite(TFT_BL, HIGH);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Hello TFT!");
}
void loop() {
  Serial.println("Loop");
  delay(1000);
}