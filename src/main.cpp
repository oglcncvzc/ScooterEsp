#define DEBUGGING true
#define SOFT_START_PIN 15    // connected to d8 micro
#define SPEED_CONFIG1_PIN 19 // connected to d2 micro
#define SPEED_CONFIG2_PIN 4  // connected to d6 micro
#define STATUS_LED_PIN 2
#define POWER_OUT_PIN 5
#define LIGHT_PIN 18
#define BUTTON_PIN 14

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
#define OLED_RESET -1 // Reset pin # (or -1 if sharing Arduino reset pin)

#include <Arduino.h>
#include <Math.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "WiFi.h"
#include "AsyncUDP.h"
#include <WiFiManager.h> // https://github.com/tzapu/WiFiManager

AsyncUDP udp;
const char *ssid = "TolgaSAN iphone";
const char *password = "TolgaSAN74";
uint8_t path[] = "/websocket";
const char host[] = "172.20.10.2";

bool ledStatus = false;
int speedLevel = 1;

//----------------
#define RX_START_BYTE 2
byte rxBuffer[50];
byte rxBufferIndex = 0;
byte rxDataLen = 0;
byte rxHash = 0;

byte driverMode = 2; // 0-2
byte passMode = 15;
byte startUpMode = 128; // 128-192
byte currentLimit = 12; // 1-50
byte wheelInch = 20;
byte softStartSpeed = 0;
bool softStartEnabled = true;
double periodMS = 0;
double speed = 0;
bool lightEnabled = false;
bool buttonStatus = false;
bool waitButtonRelease = false;
bool ecuPower = false;

uint32_t txMillis = 0;
uint32_t buttonMillis = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

void sendSettings(byte maxSpeed)
{
  if (millis() - txMillis < 200)
    return;
  txMillis = millis();
  // byte txBuffer[20] = {1, 20, 1, driverMode, passMode, startUpMode, 46, 0, wheelInch * 10, 1, 0, 0, maxSpeed, currentLimit, 1, 164, 0, 0, 5, 0};
  byte txBuffer[20] = {1, 20, 1, 2, 15, 128, 46, 0, 200, 1, 0, 0, 100, 12, 1, 164, 0, 0, 5, 182};
  byte hash = 0;
  for (byte i = 0; i < 19; i++)
  {
    hash ^= txBuffer[i];
  }
  txBuffer[19] = hash;
  for (byte i = 0; i < 20; i++)
  {
    Serial2.write(txBuffer[i]);
    Serial.print(txBuffer[i]);
    Serial.print("-");
  }
  Serial.println();
}

bool rxRoutine()
{
  while (Serial2.available() > 0)
  {
    // udp.print("idata");
    byte rxByte = Serial2.read();
    // Serial.print(rxByte);
    // Serial.print("-");
    if (rxBufferIndex == 0)
    {

      if (rxByte == RX_START_BYTE)
      {
        rxBuffer[rxBufferIndex++] = rxByte;
        rxDataLen = 0;
        rxHash = 0;
      }
    }
    else if (rxBufferIndex == 1)
    {
      rxBuffer[rxBufferIndex++] = rxByte;
      rxDataLen = rxByte;
      if (rxByte != 14)
        rxBufferIndex = 0;
    }
    else if (rxBufferIndex > 1 && rxBufferIndex + 1 < rxDataLen)
    {
      rxBuffer[rxBufferIndex++] = rxByte;
    }
    else
    {
      rxBuffer[rxBufferIndex++] = rxByte;
      for (byte i = 0; i < rxDataLen - 1; i++)
      {
        rxHash ^= rxBuffer[i];
      }
      rxBufferIndex = 0;

      if (rxHash == rxByte)
      {
        // Serial.println(rxByte);
        return true;
      }
      else
      {
        // Serial.println("hash error");
        // for (byte i = 0 ; i < rxDataLen  ; i++) {
        //   Serial.print(rxBuffer[i]);
        //   Serial.print("-");
        // }
      }
    }
  }
  return false;
}

void onRxData()
{
  periodMS = ((uint16_t)(rxBuffer[8]) << 8) | rxBuffer[9];
  if (periodMS > 3000)
    speed = 0;
  else
    speed = 2.54 * (double)wheelInch * 3.14 * 6.0 * 6.0 / (periodMS);

  Serial.println(speed);
}

uint32_t softStartMillis = 0;
void calculateSoftStart()
{
  if (millis() - softStartMillis < 100)
    return;
  softStartMillis = millis();
  softStartSpeed = speed + (speed < 20 ? 15 : 20);
  if (softStartSpeed < 5)
    softStartSpeed = 5;
}

//----------------------

WiFiManager wm;
WiFiManagerParameter custom_field; // global param ( for non blocking w params )
String getParam(String name)
{
  // read parameter from server, for customhmtl input
  String value;
  if (wm.server->hasArg(name))
  {
    value = wm.server->arg(name);
  }
  return value;
}

void setup()
{
  pinMode(POWER_OUT_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(SOFT_START_PIN, OUTPUT);
  pinMode(SPEED_CONFIG1_PIN, OUTPUT);
  pinMode(SPEED_CONFIG2_PIN, OUTPUT);
  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  digitalWrite(POWER_OUT_PIN, true);
  digitalWrite(LIGHT_PIN, true);

  // put your setup code here, to run once:
  Serial.begin(9600);
  Serial2.begin(9600);
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C))
  {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ; // Don't proceed, loop forever
  }

  // Clear the buffer
  display.clearDisplay();
  display.setTextSize(3);
  display.setTextColor(WHITE);
  display.setCursor(0, 24);
  display.print("EVARIO");
  display.display();
  delay(1000);

  // WiFi.begin(ssid, password);
  Serial.println("starting");
  const char *custom_radio_str = "<div> </div>";
  new (&custom_field) WiFiManagerParameter(custom_radio_str); // custom html input
  wm.setTitle("Evario Swan");
  wm.addParameter(&custom_field);
  // wm.setSaveParamsCallback(saveParamCallback);
  std::vector<const char *> menu = {"wifi", "info", "sep", "restart", "exit"};
  wm.setMenu(menu);

  // // set dark theme
  wm.setClass("invert");

  wm.setConfigPortalBlocking(false);
  wm.setConfigPortalTimeout(60);
  // automatically connect using saved credentials if they exist
  // If connection fails it starts an access point with the specified name
  if (wm.autoConnect("Evario_Swan"))
  {
    Serial.println("connected...yeey :)");
  }
  else
  {
    Serial.println("Configportal running");
  }
  wm.startConfigPortal("EvarioSwan");

  delay(500);
  // event handler
  if (udp.connect(IPAddress(64, 225, 108, 18), 8015))
  {
    // Serial.println("UDP connected");
    udp.onPacket([](AsyncUDPPacket packet)
                 {
            uint8_t data[100];
            int len=packet.length();
            packet.readBytes(data,packet.length());
            if(len == 5 && data[0]==5 && data[1]==5 && data[2]==5){
              lightEnabled = data[3];
              speedLevel = data[4];
            }
            // else
            // Serial.write(packet.data(), packet.length());
            // Serial.println();
            //reply to the client
            packet.printf("Got"); });
    // Send unicast
    udp.print("Hello Server!");
  }
}

void sendSpeed()
{
  calculateSoftStart();
  int maxSpeed;
  if (speedLevel == 1)
  {
    maxSpeed = 100;
    softStartEnabled = false;
    digitalWrite(SPEED_CONFIG1_PIN, LOW);
    digitalWrite(SPEED_CONFIG2_PIN, LOW);
    digitalWrite(SOFT_START_PIN, HIGH);
  }
  else if (speedLevel == 2)
  {
    maxSpeed = 40;
    softStartEnabled = true;
    digitalWrite(SPEED_CONFIG1_PIN, HIGH);
    digitalWrite(SPEED_CONFIG2_PIN, LOW);
    digitalWrite(SOFT_START_PIN, LOW);
  }
  else
  {
    maxSpeed = 20;
    softStartEnabled = true;
    digitalWrite(SPEED_CONFIG1_PIN, HIGH);
    digitalWrite(SPEED_CONFIG2_PIN, HIGH);
    digitalWrite(SOFT_START_PIN, LOW);
  }

  if (softStartEnabled)
    sendSettings(softStartSpeed > maxSpeed ? maxSpeed : softStartSpeed);
  else
    sendSettings(maxSpeed);
}
void printSpeed()
{
  display.clearDisplay();
  display.setTextSize(7);
  display.setTextColor(WHITE);
  display.setCursor(24, 16);
  if (speed < 10)
    display.print(" ");
  display.print((int)speed);
  display.display();
}

void buttonRoutine()
{
  if (!digitalRead(BUTTON_PIN))
  {
    if (!buttonStatus)
    {
      buttonMillis = millis();
      buttonStatus = true;
    }
    else
    {
      if (millis() - buttonMillis > 3000)
      {
        if (!waitButtonRelease)
        {
          ecuPower = !ecuPower;
          waitButtonRelease = true;
        }
      }
    }
  }
  else
  {
    buttonStatus = false;
    waitButtonRelease = false;
  }
}

uint32_t relayMillis = 0;
uint32_t ipMillis = 0;
void loop()
{
  wm.process();
  if (millis() - relayMillis > 200)
  {
    // digitalWrite(5, ledStatus);
    // digitalWrite(18, HIGH);
    relayMillis = millis();

    // udp.print((int)speed);
    digitalWrite(STATUS_LED_PIN, ledStatus = !ledStatus);
    digitalWrite(POWER_OUT_PIN, !ecuPower);
    digitalWrite(LIGHT_PIN, ecuPower ? !lightEnabled : true);
    printSpeed();
  }
  if (millis() - ipMillis > 100)
  {
    // digitalWrite(5, ledStatus);
    // digitalWrite(18, HIGH);
    ipMillis = millis();

    udp.print((int)speed);
    Serial.print(WiFi.localIP().toString());
  }
  buttonRoutine();

  if (rxRoutine())
    onRxData();

  sendSpeed();
}