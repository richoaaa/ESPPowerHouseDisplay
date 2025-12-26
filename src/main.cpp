/*
  This project uses an ESP32-C3 SuperMini to connect to an InfluxDB
  database and display battery health metrics on a TFT display.
  note that the displays in the house and Bremner share the same
  pinout.
  Note that this has been adapter to update via WIFI OTA
*/
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
// #include <Fonts/FreeSans9pt7b.h>  // Include font with degrees symbol
#include <Fonts/TomThumb.h>
#include <InfluxDbClient.h>
#include <InfluxDbCloud.h>
#include <SPI.h>
#include <WiFi.h>

// WiFi credentials
#define WIFI_SSID "STARLINK_24GHz"
#define WIFI_PASSWORD ""  // Replace with actual password

// InfluxDB settings
#define INFLUXDB_URL "https://us-east-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_TOKEN                                                   \
  "1fkHBo_LQvxuoW9cWJmzeqnw9p0C-gfHole7ZyxGfPbM8og-FqpAjcf4kuoGnI_" \
  "fp5t5YYYheKeeSHoEc_xbkg=="
#define INFLUXDB_ORG "53ddd0e07447bab8"
#define INFLUXDB_BUCKET "power"

// Time zone info
#define TZ_INFO "AWST-8"

// InfluxDB client instance
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET,
                      INFLUXDB_TOKEN, InfluxDbCloud2CACert);

// Display pins
#define TFT_SCLK 4  // SCL (SPI clock)
#define TFT_MOSI 5  // SDA (MOSI)
#define TFT_RST 1   // Reset
#define TFT_DC 2    // Data/Command
#define TFT_CS 3    // Chip Select
#define TFT_BL 10   // Backlight

// These pins prob suit the unit inside Terrano
// #define TFT_SCLK 4  // SCL (SPI clock)
// #define TFT_MOSI 6  // SDA (MOSI)5
// #define TFT_RST 10  // Reset1
// #define TFT_DC 8    // Data/Command2
// #define TFT_CS 7    // Chip Select3
// #define TFT_BL 5    // Backlight10

// Helper: pulse display reset and run a quick self-test
void pulseDisplayReset() {
  pinMode(TFT_RST, OUTPUT);
  digitalWrite(TFT_RST, HIGH);
  delay(10);
  digitalWrite(TFT_RST, LOW);
  delay(20);
  digitalWrite(TFT_RST, HIGH);
  delay(50);
}

// Display pins for the kitchen display
// #define TFT_SCLK 4  // SCL (SPI clock)
// #define TFT_MOSI 5  // SDA (MOSI)
// #define TFT_RST 1   // Reset
// #define TFT_DC 2    // Data/Command
// #define TFT_CS 3    // Chip Select
// #define TFT_BL 10   // Backlight

// LED pin
// #define LED_PIN 8  // Onboard blue LED
bool ledState = LOW;
bool eraseDisplay = false;
unsigned long VoidMillis;
unsigned long previousMillis = 0;  // for LED timing
int step = 0;  // keeps track of which part of the sequence we're in
bool otaInProgress = false;

// TFT display instance
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// Time interval for querying (30 seconds)
#define QUERY_INTERVAL 5000

float soc = NAN, voltage = NAN, powerIn = NAN, powerOut = NAN,
      ambientTemp = NAN, ChargeRate = NAN;
int DesignVoltage = 24;
String Message = "";

void setup() {
  // pinMode(LED_PIN, OUTPUT);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);  // Backlight off to stabilize power
  Serial.begin(9600);
  delay(1000);  // Allow Serial to initialize
  Serial.println("Starting ESP32-C3 SuperMini InfluxDB SOC Reader");

  // Initialize TFT display
  Serial.println("Initializing TFT...");
  // Pulse reset first (helps displays stuck in reset)
  pulseDisplayReset();
  tft.init(240, 240, SPI_MODE3);  // 240x240 resolution
  Serial.println("TFT Initialized");
  SPI.setFrequency(40000000);  // Set SPI to 40 MHz (adjust based on testing)
// quick visual self-test: turn backlight on, show a few colours then clear
  digitalWrite(TFT_BL, HIGH);  // Backlight on
  tft.setRotation(2);          // Adjust rotation (0-3) as needed
  tft.fillScreen(ST77XX_RED);
  delay(60);
  tft.fillScreen(ST77XX_GREEN);
  delay(60);
  tft.fillScreen(ST77XX_BLUE);
  delay(60);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.setCursor(0, 0);
  tft.println("Initializing...");

  // Connect to WiFi
  Serial.println("Connecting to WiFi...");
  tft.println("Connecting WiFi...");
  WiFi.setHostname("HomeDisplay");  // Set before or after connect
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Set device hostname for OTA and mDNS
  WiFi.setHostname("HomeDisplay");
  ArduinoOTA.setHostname("HomeDisplay");

  // OTA event handlers (optional, for debug)
  ArduinoOTA.onStart([]() { Serial.println("OTA Update Start"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA Update End"); });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("OTA Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("OTA Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });

  // Start mDNS responder
  if (!MDNS.begin("HomeDisplay")) {
    Serial.println("Error starting mDNS responder!");
  } else {
    Serial.println("mDNS responder started as HomeDisplay.local");
  }

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 10000) {
    Serial.print(".");
    tft.print(".");
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    tft.println("\nWiFi OK");
    tft.print("IP: ");
    tft.println(WiFi.localIP());
    // Start OTA now that WiFi is connected
    ArduinoOTA.begin();
    ArduinoOTA.onStart([]() {
      Serial.println("OTA Update Start");
      otaInProgress = true;  // Set flag to pause readings
    });
    ArduinoOTA.onEnd([]() {
      Serial.println("OTA Update End");
      otaInProgress = false;  // Resume readings
    });
  } else {
    Serial.println("\nWiFi connection failed");
    tft.println("\nWiFi Failed");
    tft.setFont(&TomThumb);
  }

  // Connect to InfluxDB
  Serial.println("Connecting to InfluxDB...");
  tft.println("Connecting InfluxDB...");
  if (client.validateConnection()) {
    Serial.println("Connected to InfluxDB");
    tft.println("InfluxDB OK");
  } else {
    Serial.print("InfluxDB connection failed: ");
    Serial.println(client.getLastErrorMessage());
    tft.print("InfluxDB Failed: ");
    tft.println(client.getLastErrorMessage());
  }

  VoidMillis = millis();
  delay(1000);  // Show init messages
  tft.setTextSize(3);
  tft.fillScreen(ST77XX_BLACK);
}

void loop() {
  ArduinoOTA.handle();  // Handle OTA updates
  if (otaInProgress) {
    return;
  }

  String query = "from(bucket:\"" + String(INFLUXDB_BUCKET) +
                 "\")"
                 " |> range(start: -5m)"
                 " |> filter(fn: (r) => r._measurement == \"Power Metrics\" "
                 "and (r._field == \"SOC\" or r._field == \"Voltage\" or "
                 "r._field == \"PowerIn\" or r._field == \"PowerOut\" or "
                 "r._field == \"ShedTemp\" or r._field == \"Charge Rate\") and "
                 "exists r._value)"
                 " |> last()";
  FluxQueryResult result = client.query(query);
  if (eraseDisplay == true) {
    tft.fillScreen(ST77XX_BLACK);
    eraseDisplay = false;
  }

  // Query InfluxDB
  tft.setCursor(0, 0);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.println(" BATT HEALTH");

  if (result.getError() != "") {
    tft.fillScreen(ST77XX_BLACK);
    // Serial.print("Query error: ");
    // Serial.println(result.getError());
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.print("Error: ");
    tft.println(result.getError());
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    eraseDisplay = true;
    return;
  } else {
    eraseDisplay = false;
    // Variables to hold latest values
    soc = NAN, voltage = NAN, powerIn = NAN, powerOut = NAN, ambientTemp = NAN,
    ChargeRate = NAN;
    String time = "";
    bool found = false;
    while (result.next()) {
      String field = result.getValueByName("_field").getString();
      float value = result.getValueByName("_value").getDouble();
      time = result.getValueByName("_time").getString();
      found = true;
      if (field == "SOC")
        soc = value;
      else if (field == "Voltage")
        voltage = value;
      else if (field == "PowerIn")
        powerIn = value;
      else if (field == "PowerOut")
        powerOut = value;
      else if (field == "ShedTemp")
        ambientTemp = value;
      else if (field == "Charge Rate")
        ChargeRate = value;
    }
    if (found) {
      tft.setCursor(tft.getCursorX(), tft.getCursorY() + 10);
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print("SOC:     ");
      tft.setCursor(100, tft.getCursorY());
      if (soc <= 20) {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      } else {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
      }
      tft.print((int)soc);
      tft.println(" %");

      tft.setCursor(tft.getCursorX(), tft.getCursorY() + 10);

      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print("Volt:    ");
      tft.setCursor(100, tft.getCursorY());
      if (voltage >= 26.5) {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
      } else if (voltage >= 25.8 && voltage < 26.5) {
        tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
      } else {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      }
      tft.println(voltage, 1);

      tft.setCursor(0, tft.getCursorY() + 10);
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print("In:         ");  // Overwrite previous line with spaces
      tft.setCursor(100, tft.getCursorY());
      // tft.print("In: ");
      if (powerIn >= 25 * 24) {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
      } else if (powerIn < 25 * DesignVoltage && powerIn > 0) {
        tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
      } else {
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      }
      if (powerIn >= 1000) {
        tft.print(powerIn / 1000.0, 1);
        tft.println(" kW");
      } else {
        tft.print((int)powerIn);
        tft.println(" W");
      }

      tft.setCursor(tft.getCursorX(), tft.getCursorY() + 10);
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print("Out:         ");  // Overwrite previous line with spaces
      // Move cursor back to start of line after label
      tft.setCursor(100, tft.getCursorY());
      if (powerOut >= 90 * DesignVoltage) {
        tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
      } else if (powerOut >= 40 * DesignVoltage &&
                 powerOut < 90 * DesignVoltage) {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
      } else if (powerOut >= 20 * DesignVoltage &&
                 powerOut < 40 * DesignVoltage) {
        tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
      } else {
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      }
      if (powerOut >= 1000) {
        tft.print(powerOut / 1000.0, 1);
        tft.println(" kW");
      } else {
        tft.print((int)powerOut);
        tft.println(" W");
      }

      tft.setCursor(tft.getCursorX(), tft.getCursorY() + 10);
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print("Temp:      ");
      tft.setCursor(100, tft.getCursorY());
      uint16_t
          tempColour;  // variable to hold the current text (and circle) color
      if (ambientTemp >= 40) {
        tft.setTextColor(ST77XX_MAGENTA, ST77XX_BLACK);
        tempColour = ST77XX_MAGENTA;
      } else if (ambientTemp >= 35 && ambientTemp < 40) {
        tft.setTextColor(ST77XX_ORANGE, ST77XX_BLACK);
        tempColour = ST77XX_ORANGE;
      } else if (ambientTemp >= 15 && ambientTemp < 35) {
        tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
        tempColour = ST77XX_GREEN;
      } else {
        tft.setTextColor(ST77XX_BLUE, ST77XX_BLACK);
        tempColour = ST77XX_BLUE;
      }
      tft.print((int)ambientTemp);
      tft.drawCircle(tft.getCursorX() + 16, tft.getCursorY() - 3, 3,
                     tempColour);
      tft.setCursor(tft.getCursorX() + 2, tft.getCursorY());
      tft.println(" C");

      tft.setCursor(tft.getCursorX(), tft.getCursorY() + 10);
      tft.setTextSize(3);
      tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
      tft.print("Rate:        ");
      tft.setCursor(100, tft.getCursorY());
      tft.print((int)ChargeRate);
      tft.println(" %/hr");
      tft.setTextSize(3);

    } else {
      tft.println("No data found.");
    }
  }
  result.close();

  delay(QUERY_INTERVAL);
}