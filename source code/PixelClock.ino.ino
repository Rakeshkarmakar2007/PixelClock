#include <WiFi.h>
#include <Wire.h>
#include <RTClib.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <Adafruit_GFX.h>
#include <Adafruit_NeoMatrix.h>
#include <SevenSegmentPixelDisplay.h>

// --- USER CONFIGURATION ---
const String firmware_version_url = "https://raw.githubusercontent.com/Rakeshkarmakar2007/PixelClock/refs/heads/main/main/version.txt";
const String firmware_bin_url = "https://raw.githubusercontent.com/Rakeshkarmakar2007/PixelClock/refs/heads/main/main/firmware.bin";
const float CURRENT_VERSION = 1.01; 

const char* ssid = "Vivo";
const char* pass = "12345678";
const char* GOOGLE_API_KEY = "AIzaSyDAYGOg0JUN7LyNpdmsxzGgkxiAMvTUAqc"; 
// --------------------------

#define PIN_7SEG     25
#define PIN_MATRIX   26
#define NUM_7SEG_PIX 30

const int timeoffset         = 19800; 
static const float LATITUDE  = 23.4000f;
static const float LONGITUDE = 88.5000f;

const unsigned long WEATHER_TIMEOUT = 30 * 60 * 1000UL; 
const unsigned long WIFI_TIMEOUT    = 15000UL;          

SemaphoreHandle_t dataMutex;

String matrixText = "Clock V1.00 ";
int temperature   = 0;
int humidity      = 0;
bool showWeather  = false;
unsigned long lastWeatherSuccess = 0;

WiFiUDP udp;
RTC_DS1307 rtc;
NTPClient ntp(udp, "pool.ntp.org", timeoffset);

SevenSegmentPixelDisplay display(PIN_7SEG, NUM_7SEG_PIX); 
Adafruit_NeoMatrix matrix = Adafruit_NeoMatrix(30, 9, PIN_MATRIX, 
  NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_ROWS + NEO_MATRIX_ZIGZAG, 
  NEO_GRB + NEO_KHZ800);

// Task Handles
TaskHandle_t MatrixTaskHandle = NULL;
TaskHandle_t SevenSegTaskHandle = NULL;
TaskHandle_t NetworkTaskHandle = NULL;

// Task Prototypes
void MatrixCoreTask(void * pvParameters);
void SevenSegCoreTask(void * pvParameters);
void NetworkCoreTask(void * pvParameters);

void setOfflineMode() {
  if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
    showWeather = false;
    DateTime now = rtc.now();
    matrixText = "Date:- " + String(now.day()) + "/" + String(now.month()) + "/" + String(now.year());
    xSemaphoreGive(dataMutex);
  }
}

bool checkForUpdates() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.begin(client, firmware_version_url);

  int httpCode = http.GET();
  bool updateInitiated = false;

  if (httpCode == HTTP_CODE_OK) {
    String newVersionStr = http.getString();
    float newVersion = newVersionStr.toFloat();

    if (newVersion > CURRENT_VERSION) {
      updateInitiated = true;

      // 1. Turn off the display tasks immediately 
      if (MatrixTaskHandle != NULL) vTaskDelete(MatrixTaskHandle);
      if (SevenSegTaskHandle != NULL) vTaskDelete(SevenSegTaskHandle);

      // 2. Prepare scrolling text
      String updateText = "new version available: " + String(newVersion, 2) + ". updating....";
      int textWidth = updateText.length() * 6;
      int xPos = matrix.width();
      
      // 3. Scroll for 10 seconds (10000 milliseconds)
      unsigned long scrollStart = millis();
      while (millis() - scrollStart < 10000) {
        matrix.fillScreen(0);
        matrix.setCursor(xPos, 1);
        matrix.setTextColor(matrix.Color(0, 255, 255)); // Cyan text
        matrix.print(updateText); 
        matrix.show();

        if (--xPos < -textWidth) {
          xPos = matrix.width(); 
        }
        vTaskDelay(80 / portTICK_PERIOD_MS); 
      }

      // Close the HTTP connection for the version check before starting the bin download
      http.end(); 
      
      // Give the ESP32 a brief moment to settle
      vTaskDelay(1000 / portTICK_PERIOD_MS);

      // 4. Execute OTA Update (Wi-Fi is still fully connected here)
      httpUpdate.update(client, firmware_bin_url);
    }
  } 
  
  if (!updateInitiated) {
    http.end();
  }
  
  return updateInitiated;
}

void fetchTimeAndWeather() {
  if(WiFi.status() != WL_CONNECTED) {
      setOfflineMode();
      return;
  }
  
  IPAddress ip;
  unsigned long pingStart = millis();
  int dnsStatus = WiFi.hostByName("google.com", ip);
  unsigned long pingTime = millis() - pingStart;

  if (dnsStatus != 1 || pingTime > 3500) {
      setOfflineMode();
      return; 
  }

  char url[512];
  snprintf(url, sizeof(url),
      "https://weather.googleapis.com/v1/forecast/hours:lookup?key=%s&location.latitude=%.4f&location.longitude=%.4f&hours=2",
      GOOGLE_API_KEY, LATITUDE, LONGITUDE);
  
  WiFiClientSecure client;
  client.setInsecure(); 
  
  HTTPClient http;
  http.begin(client, url);
  http.setTimeout(10000); 
  http.setUserAgent("ESP32-Weather-Matrix/2.0"); 

  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      DynamicJsonDocument doc(4096);
      DeserializationError err = deserializeJson(doc, payload);
      
      if (!err) {
          JsonObject currentHour = doc["forecastHours"][1];
          
          if (xSemaphoreTake(dataMutex, portMAX_DELAY)) {
            temperature = (int)currentHour["temperature"]["degrees"].as<float>();
            humidity    = currentHour["relativeHumidity"].as<int>();
            int precipProb = currentHour["precipitation"]["probability"]["percent"].as<int>();
            float precipMm = currentHour["precipitation"]["qpf"]["quantity"].as<float>();
            
            matrixText = currentHour["weatherCondition"]["description"]["text"].as<String>();
            
            if (precipProb > 60) {
              matrixText += " ( Prob: " + String(precipProb) + "%)";
            }
            
            showWeather = true;
            lastWeatherSuccess = millis();
            xSemaphoreGive(dataMutex);
          }
      } else {
          setOfflineMode();
      }
  } else {
      setOfflineMode();
  }
  http.end();
  
  ntp.begin();
  if (ntp.update()) { 
    unsigned long time = ntp.getEpochTime();
    if(time > 1000) {
      rtc.adjust(DateTime(time));
      struct timeval tv;
      tv.tv_sec = time;
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
    }
  }
}

void setup() {
  Wire.begin();
  display.begin();
  display.setBrightness(255);
  matrix.begin();
  matrix.setTextWrap(false); 
  matrix.setBrightness(52);
  
  dataMutex = xSemaphoreCreateMutex();

  uint32_t red  = display.color(255, 0, 0);
  if(!rtc.begin()){ 
    while(1){
      display.printHr(0, red); display.printMin(0, red); display.show();
      delay(1000); ESP.restart();
    }
  }
  
  xTaskCreatePinnedToCore(MatrixCoreTask, "MatrixTask", 4096, NULL, 1, &MatrixTaskHandle, 0);
  xTaskCreatePinnedToCore(SevenSegCoreTask, "SevenSegTask", 3072, NULL, 2, &SevenSegTaskHandle, 1);
  xTaskCreatePinnedToCore(NetworkCoreTask, "NetworkTask", 8192, NULL, 1, &NetworkTaskHandle, 1);
}

void loop() {
  vTaskDelay(portMAX_DELAY); 
}

void MatrixCoreTask(void * pvParameters) {
  int x = matrix.width(); 
  bool showingStaticHumidity = false;
  String localText = "";
  bool localShowWeather = false;
  int localHumidity = 0;

  for(;;) { 
    matrix.fillScreen(0);
    
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10))) {
      localText = matrixText;
      localShowWeather = showWeather;
      localHumidity = humidity;
      xSemaphoreGive(dataMutex);
    }

    if (showingStaticHumidity && localShowWeather) {
        matrix.setCursor(1, 1);
        int pulse = (sin(millis() / 200.0) * 55) + 200; 
        matrix.setTextColor(matrix.Color(0, pulse, pulse)); 
        matrix.printf("H:%d%%", localHumidity);
        matrix.show();
        vTaskDelay(3000 / portTICK_PERIOD_MS); 
        showingStaticHumidity = false;
        x = matrix.width(); 
    } 
    else {
        matrix.setCursor(x, 1);
        int len = localText.length();
        for(int i = 0; i < len; i++) {
            matrix.setTextColor(matrix.ColorHSV((i+1) * 1000));
            matrix.print(localText[i]);
        }
        matrix.show();
        int textWidth = len * 6;
        if (--x < -textWidth) {
            showingStaticHumidity = true; 
            if(!localShowWeather) { x = matrix.width(); } 
        }
        vTaskDelay(80 / portTICK_PERIOD_MS); 
    }
  }
}

void SevenSegCoreTask(void * pvParameters) {
  int displayCounter = 0;
  bool localShowWeather = false;
  int localTemp = 0;
  unsigned long localWeatherSuccess = 0;

  for(;;) {
    unsigned long currentMillis = millis();

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(10))) {
      localShowWeather = showWeather;
      localTemp = temperature;
      localWeatherSuccess = lastWeatherSuccess;
      xSemaphoreGive(dataMutex);
    }

    if (localShowWeather && (currentMillis - localWeatherSuccess >= WEATHER_TIMEOUT)) {
      setOfflineMode();
    }

    if (displayCounter < 2 && localShowWeather) {
      display.printTemp(localTemp, C, display.colorHSV(45000 - (localTemp * 1000)));
    } else {
      DateTime now = rtc.now();
      int currentHour = now.hour();
      if (currentHour > 12) currentHour -= 12;
      else if (currentHour == 0) currentHour = 12;
      
      display.printHr(currentHour, display.colorHSV(now.hour() * 400));
      display.printMin(now.minute(), display.colorHSV(now.minute() * 400)); 
      display.blinker(now.second() % 2 == 0, display.colorHSV(now.second() * 1000));
    }
    display.show(); 
    
    displayCounter = (displayCounter + 1) % 7;
    vTaskDelay(1000 / portTICK_PERIOD_MS); 
  }
}

void NetworkCoreTask(void * pvParameters) {
  
  // --- BOOT UP SEQUENCE ---
  WiFi.begin(ssid, pass);
  unsigned long startWait = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - startWait < WIFI_TIMEOUT)) {
    vTaskDelay(200 / portTICK_PERIOD_MS);
  }
  
  if(WiFi.status() == WL_CONNECTED) {
    fetchTimeAndWeather();
    
    // Check for OTA updates ONLY once on boot
    bool isUpdating = checkForUpdates(); 
    
    if (isUpdating) {
        // If an update succeeds, the ESP32 resets automatically.
        // If we reach this line, the update FAILED. Restart device to recover tasks.
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        ESP.restart();
    }
    
  } else {
    setOfflineMode();
  }
  
  // This is only called if no update is happening
  WiFi.disconnect(true);

  // --- REGULAR UPDATE LOOP (Every 10 minutes) ---
  for(;;) {
  
    vTaskDelay((10 * 60 * 1000) / portTICK_PERIOD_MS); 

    WiFi.begin(ssid, pass);
    startWait = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startWait < WIFI_TIMEOUT)) {
      vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    if (WiFi.status() == WL_CONNECTED) {
      fetchTimeAndWeather();
    } else {
      setOfflineMode();
    }
    WiFi.disconnect(true);
  }
}