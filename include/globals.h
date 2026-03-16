#ifndef GLOBALS_H
#define GLOBALS_H

// include libraries
#include <Wire.h>
#include <WiFi.h>
#include <time.h>
#include <DHT20.h>
#include <stdint.h>
#include "LittleFS.h"
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <SoftwareSerial.h>
#include <TinyGPS++.h>
#include <ThingsBoard.h>
#include <Arduino_MQTT_Client.h>
#include <HTTPClient.h>
#include <Adafruit_NeoPixel.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// include common files
#include "../src/common/info.h"

// DEFINE PIN
#define BOOT 0

// DEFINE MQTT
#define MQTT_SERVER "app.coreiot.io"
#define MQTT_PORT 1883U

// DEFINE DELAY
#define delay_time 10000
#define delay_gps 15000
#define delay_connect 100
#define delay_led 1000
#define delay_30_min 1800000

#define NUM_PIXELS 4
#define Brightness 39 // Set brightness to (0 to 255)

// DEFINE BAUD_RATE
#define BAUD_RATE_1 115200
#define BAUD_RATE_2 9600

// DEFINE NTP
#define httpPort 80

#endif