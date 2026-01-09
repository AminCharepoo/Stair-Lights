#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include <WiFiManager.h> 
#include <WebServer.h>
#include <ArduinoOTA.h>

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ LED ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
#define LEDS_PIN 2
#define NUMPIXELS 111

Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, LEDS_PIN, NEO_GRB + NEO_KHZ800);

int redValue = 125;
int greenValue = 0;
int blueValue = 0;
int brightness = 125;
bool allAtOnce = false;
// ^^^^^^^^^^ LED ^^^^^^^^^^

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ IR SENSOR ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
#define PIR_PIN 3

int motionVal = 0;
bool personNearby = false;
long lastSeenPerson = 0;
long timeOn = 5000;

long ignoreMotionUntil = 0;
const long ignoreTime = 1000;

bool lastState = false;
// ^^^^^^^^^^ IR SENSOR ^^^^^^^^^^


// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ BUTTON ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
#define BUTTON_PIN 4

long buttonDebounce = 0;
long debounce = 1000; 
// ^^^^^^^^^^ BUTTON ^^^^^^^^^^

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ WEB SERVER ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
//AsyncWebServer server(80);
WebServer server(80);
bool websiteOnline = false;
// ^^^^^^^^^^ WEB SERVER ^^^^^^^^^^

// STORAGE
Preferences prefs;

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ SLEEP ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
long wakeUntil = 0;
bool wokenBySensor = false;
bool wokenByButton = false;
long IRwakeTime = 30000;
long websiteWakeTime = 60000;
// ^^^^^^^^^^ SLEEP ^^^^^^^^^^

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ PHOTORESISTOR ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
#define PHOTO_PIN 0
int threshold = 200;
// ^^^^^^^^^^ PHOTORESISTOR ^^^^^^^^^^

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ DEBUG ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
bool serialDebugged = false;
bool developerMode = false;
// ^^^^^^^^^^ DEBUG ^^^^^^^^^^

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ WEBSITE ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
String getHTML() {
  String html = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
  <title>ESP32 RGB LED</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; text-align: center;}
    body {max-width: 600px; margin:0 auto; padding: 25px;}
    input[type=range] {width: 80%;}
  </style>
</head>
<body>
  <h2>ESP32 RGB LED Controller</h2>
  <h4>Red</h4>
  <input type="range" min="0" max="255" value=")rawliteral";
  html += String(redValue);
  html += R"rawliteral(" id="red" oninput="updateColor(this)">
  <h4>Green</h4>
  <input type="range" min="0" max="255" value=")rawliteral";
  html += String(greenValue);
  html += R"rawliteral(" id="green" oninput="updateColor(this)">
  <h4>Blue</h4>
  <input type="range" min="0" max="255" value=")rawliteral";
  html += String(blueValue);
  html += R"rawliteral(" id="blue" oninput="updateColor(this)">
  <h4>Brightness</h4>
  <input type="range" min="0" max="255" value=")rawliteral";
  html += String(brightness);
  html += R"rawliteral(" id="brightness" oninput="updateBrightness(this)">
  <script>
    function updateColor(element) {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/update?color=" + element.id + "&value=" + element.value, true);
      xhr.send();
    }
    function updateBrightness(element) {
      var xhr = new XMLHttpRequest();
      xhr.open("GET", "/update?brightness=" + element.value, true);
      xhr.send();
    }
  </script>
</body>
</html>)rawliteral";

  return html;
}
// ^^^^^^^^^^ WEBSITE ^^^^^^^^^^

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ MAIN FUNCTIONS ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ 
void setup() {
  Serial.begin(115200);
  delay(100);

  // storage setup
  prefs.begin("leddata", false);

  // pixel setup
  pixels.begin();
  pixels.setBrightness(brightness);

  // pin setup
  setupPins();

  // figure out if button or motion woke up the controller
  wakeupReason();

  Serial.println("Starting...");
  
  // -- setup wakeup/asleep --
  gpio_set_direction((gpio_num_t)PIR_PIN, GPIO_MODE_INPUT);
  gpio_set_direction((gpio_num_t)BUTTON_PIN, GPIO_MODE_INPUT);

  // create a bitmask for wakeup pins
  uint64_t wakeMask = (1ULL << PIR_PIN) | (1ULL << BUTTON_PIN);

  // enable GPIO wakeup for multiple pins
  esp_deep_sleep_enable_gpio_wakeup(wakeMask, ESP_GPIO_WAKEUP_GPIO_HIGH);
}

void loop() {
  // make sure serial is updated in case it didnt start up in time
  if (millis() > 1000 && !serialDebugged) {Serial.println("debug"); serialDebugged = true;}

  if (wokenBySensor && !wokenByButton) { handleIRsensor(); }

  // if button pressed
  if (digitalRead(BUTTON_PIN) == HIGH) {
    if (!wokenByButton) { // if the microcontroller hasn't already been woken up by the button start the website
      wifiManagerConnect();
      startWebServer();

      turnOnPixels();

      wakeUntil = millis() + websiteWakeTime;
      String message = "Awake for: " + String(websiteWakeTime / 1000) + " seconds";
      Serial.println(message);

      wokenByButton = true;
    }
    else { // if the microntroller has already been woken up by the button reset the timer
      if (checkButtonDebounce()) {
        Serial.println("Button retriggered, resetting timer");
        wakeUntil = millis() + websiteWakeTime;
        buttonDebounce = millis() + debounce;
      }
    }
  }

  if (wokenByButton) {
    server.handleClient();
    ArduinoOTA.handle();
  }

  // go to sleep after certain amount of time
  if (millis() >= wakeUntil) { 
    goToSleep();
  }
}
// ^^^^^^^^^^ MAIN FUNCTIONS ^^^^^^^^^^

void setupPins() {
  pinMode(PHOTO_PIN, INPUT);
  pinMode(PIR_PIN, INPUT_PULLDOWN);
  pinMode(BUTTON_PIN, INPUT_PULLDOWN);
  pinMode(8, OUTPUT);
  digitalWrite(8, HIGH);
}

// read which pin is high on wakeup to figure out which pin was the reason the microntroller woke up
void wakeupReason() {
  Serial.println("wakeupReason()");

  if (digitalRead(PIR_PIN) == HIGH) {
    handleWakeIR();
  }
  else if (digitalRead(BUTTON_PIN) == HIGH) {
    handleAwakeButton();
  }
  else {
    Serial.println("Neither");
  }
}


// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ LED STRIP FUNCTIONS ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄
void updateColor() {
  pixels.fill(pixels.Color(redValue, greenValue, blueValue));
  pixels.show();
}

void updateBrightness() {
  pixels.setBrightness(brightness);
  pixels.show();
}

void turnOnPixels() {
  if (allAtOnce) { updateColor(); }
  else { oneByOneMode(); }
}

void turnOffPixels() {
  pixels.fill(0);  
  pixels.show();
}

void ledOneByOne() {
  for (int i = 0; i < NUMPIXELS; i ++) {
    pixels.setPixelColor(i, pixels.Color(redValue, greenValue, blueValue));
    pixels.show();
    delay(30);
  }
}
// ^^^^^^^ LED STRIP FUNCTIONS ^^^^^^^

bool checkButtonDebounce() {
  if (millis() > buttonDebounce) return true;
  else return false;
}

bool checkIfDark() {
  int val = analogRead(PHOTO_PIN);
  Serial.println(val);

  if (val < threshold) {
    //Serial.println("It is dark");
    return true;
  }
  else {
    //Serial.println("Too bright");
    return false;
  }
}

void handleIRsensor() {
  motionVal = digitalRead(PIR_PIN);
  if (motionVal == HIGH) { wakeUntil = millis() + IRwakeTime; }
}

// If you want to have the LEDS turn off before the system goes back sleep
/*
void handleIRsensor() {
  long now = millis();
  motionVal = digitalRead(PIR_PIN);

  if (now < ignoreMotionUntil) return;

  if (motionVal == HIGH) {
    if (!personNearby) Serial.println("Motion Detected!");
    personNearby = true;
    lastSeenPerson = now;
    wakeUntil = millis() + IRwakeTime;
  }
  else {
    if (personNearby && (millis() - lastSeenPerson > timeOn)) {
      Serial.println("Person probably gone");
      personNearby = false;
      ignoreMotionUntil = now + ignoreTime;
    }
  }

  if (personNearby != lastState) {
    if (personNearby) {
      updateColor();
      updateBrightness();
    }
    else {
      turnOffPixels();
    }
    lastState = personNearby;
  }
}
*/

// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ WEBSITE/WIFI ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄
void startWebServer() {
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", getHTML());
  });

  server.on("/update", HTTP_GET, []() {
    if (server.hasArg("color") && server.hasArg("value")) {
      String color = server.arg("color");
      int val = server.arg("value").toInt();

      if (color == "red")   { redValue = val; prefs.putInt("red", redValue); }
      if (color == "green") { greenValue = val; prefs.putInt("green", greenValue); }
      if (color == "blue")  { blueValue = val; prefs.putInt("blue", blueValue); }

      updateColor();
    }

    if (server.hasArg("brightness")) {
      brightness = server.arg("brightness").toInt();
      prefs.putInt("brightness", brightness);
      updateBrightness();
    }

    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

void wifiManagerConnect() {
  WiFiManager wm;

  bool res;
  res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  if(!res) {
      Serial.println("Failed to connect");
      // ESP.restart();
  } 
  else {
      //if you get here you have connected to the WiFi    
      Serial.println("connected...yeey :)");
  }
}
// ^^^^^^^ WEBSITE/WIFI ^^^^^^^


// ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄ WAKEUP FUNCITONS ⌄⌄⌄⌄⌄⌄⌄⌄⌄⌄
void handleWakeIR() {
  Serial.println("Woken by IR Sensor");
  
  // check if it is dark enough to turn on lights
  if (checkIfDark()) {
    Serial.println("Dark enough");
  }
  else {
    Serial.println("Too bright");
    blinkOnBoardLed(3, 100);
    return;
  }

  // Retrieve color values from storage
  redValue   = prefs.getInt("red", 125);
  greenValue = prefs.getInt("green", 0);
  blueValue  = prefs.getInt("blue", 0);
  brightness = prefs.getInt("brightness", 125);
  
  turnOnPixels();
  
  wokenBySensor = true;
  wakeUntil = millis() + IRwakeTime;
  String message = "Awake for: " + String(IRwakeTime / 1000) + " seconds";
  Serial.println(message);

  blinkOnBoardLed(2, 1000); // for visual debug
}

void handleAwakeButton() {
  Serial.println("Woken by button");

  // retrieve stored color values
  redValue   = prefs.getInt("red",   125);
  greenValue = prefs.getInt("green", 0);
  blueValue  = prefs.getInt("blue",  0);
  brightness = prefs.getInt("brightness",125);

  turnOnPixels();

  wifiManagerConnect();
  delay(500);

  ArduinoOTA
    .setHostname("esp32-led")
    .onStart([]() { 
      blinkOnBoardLed(3, 100);
      digitalWrite(8, LOW);
      wakeUntil = millis() + 240000; 
    })
    .onEnd([]() { 
      digitalWrite(8, HIGH);
    })
    .onError([](ota_error_t error) {
      Serial.printf("OTA Error[%u]\n", error);
    });
  ArduinoOTA.begin();

  startWebServer();

  wokenByButton = true;
  wakeUntil = millis() + websiteWakeTime;
  String message = "Awake for: " + String(websiteWakeTime / 1000) + " seconds";
  Serial.println(message);

  digitalWrite(8, LOW); // visual debug
}

void goToSleep() {
  turnOffPixels();
  Serial.println("Going to sleep");
  Serial.flush();
  esp_deep_sleep_start();
}
// ^^^^^^^ WAKEUP/SLEEP FUNCTIONS ^^^^^^^

// debug onboard led
void blinkOnBoardLed(int num, int interval) {
  for (int i = 0; i < num; i ++) {
    digitalWrite(8, LOW);
    delay(interval);
    digitalWrite(8, HIGH);
    delay(interval);
  }
}
