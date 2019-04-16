/* Seismic sensor
 *  
 *  - connect to Wifi
 *    - presetup
 *    - or create an access point for you to setup the wifi
 *  - read 3 axis digital accelerometer sensor
 *  - store into SPIFFS memory
 *  - make available these data onto a webserver
 *  - send an email if the data are bigger than the threshold
 *  
 *  ToDo:
 *    - Add sd datalogging
 *    - Stabilise web server (simplify)
 *    - non blocking email sending and SPIFFS writting
 *    - autoreconnect
 *    - auto restart
 *    - save only alarm
 *    - txt module
 *    - faster oled communication
 *    - 
 *  
*/
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiManager.h> 
#include <ESPmDNS.h>

WiFiManager wifiManager;

#include <time.h>        

#include <Wire.h>
#include "Adafruit_SHT31.h"
#include <U8x8lib.h>

Adafruit_SHT31 sht31 = Adafruit_SHT31();

// Initialize the OLED display using Wire library
#define SDA_PIN 4// GPIO21 -> SDA
#define SCL_PIN 15// GPIO22 -> SCL
#define RST_PIN 16
U8X8_SSD1306_128X64_NONAME_SW_I2C u8x8(SCL_PIN, SDA_PIN, RST_PIN);

String version = "v1.1";              // Version of this program
String identification = "Test unit";  // Identification of the module, location
const char* ssid     = "WWBYOD";
const char* password = "EiR226*?!nKY";

float temp, hum, tempSmooth = 0, humSmooth = 0;
float alpha = 0.7; // factor to tune
unsigned long startMillisData, startMillisDisplay;  //some global variables available anywhere in the program
unsigned long currentMillis;
const unsigned long periodData = 100;  //the value is a number of milliseconds
const unsigned long periodDisplay = 1000;  //the value is a number of milliseconds

void setup() {
  Serial.begin(115200);
  init_display();
  
  u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("Connect to Wifi");   
  WiFi.begin(ssid, password);
  if(WiFi.status() != WL_CONNECTED){
    //first parameter is name of access point, second is the password
    u8x8.setCursor(0, 2);u8x8.clearLine(2);u8x8.print("WifiManager");
    u8x8.setCursor(0, 3);u8x8.clearLine(3);u8x8.print("SSID:WetaSiesmic");
    u8x8.setCursor(0, 4);u8x8.clearLine(4);u8x8.print("PWD:simple123456");
    u8x8.setCursor(0, 5);u8x8.clearLine(5);u8x8.print("IP:192.168.4.1");
    wifiManager.autoConnect("WetaSiesmic", "simple123456");
  }
  u8x8.clearLine(2);u8x8.clearLine(3);u8x8.clearLine(4);u8x8.clearLine(5);
  Serial.println(F("WiFi connected"));

  u8x8.setCursor(0,2);u8x8.clearLine(2);u8x8.print("Init STH31");   
  if (! sht31.begin(0x44)) {   // Set to 0x45 for alternate i2c addr
    Serial.println("Couldn't find SHT31");
    u8x8.setCursor(0,2);u8x8.clearLine(2);u8x8.print("Couldn't find SHT31");
    while (1) delay(1);
  }
  u8x8.clearLine(2);
}

void loop() {
  currentMillis = millis();  
  if (currentMillis - startMillisData >= periodData){
    temp = sht31.readTemperature();
    hum = sht31.readHumidity();
    if (! isnan(temp)) {  // check if 'is not a number'
      tempSmooth = alpha * temp + (1-alpha) * tempSmooth;
    } 
    else Serial.println("Failed to read temperature");
    
    if (! isnan(hum)) {  // check if 'is not a number'
      humSmooth = alpha * hum + (1-alpha) * humSmooth;
    } 
    else Serial.println("Failed to read humidity");
    
    startMillisData = currentMillis;  
  }
  if (currentMillis - startMillisDisplay >= periodDisplay){
    Serial.print("Temp *C = ");Serial.println(tempSmooth);
    Serial.print("Hum. % = "); Serial.println(humSmooth);
    // display on Oled
    u8x8.setCursor(0, 2);
    u8x8.clearLine(2);
    u8x8.print("Temp *C = ");
    u8x8.print(tempSmooth);
    u8x8.setCursor(0, 3);
    u8x8.clearLine(3);
    u8x8.print("Hum. %  = ");
    u8x8.print(humSmooth);
    
    // send mqtt
    
    startMillisDisplay = currentMillis;  
  }
}

void init_display(){
  // Initialising the UI will init the display too.
  u8x8.begin();
  u8x8.setPowerSave(0);
  delay(1000);
  u8x8.setFont(u8x8_font_chroma48medium8_r);
  u8x8.setCursor(0, 0);
  u8x8.println("Sensor node");
}
