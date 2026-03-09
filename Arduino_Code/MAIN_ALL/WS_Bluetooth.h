#pragma once

#include <HardwareSerial.h>     // Reference the ESP32 built-in serial port library
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

#include "WS_GPIO.h"
#include "WS_Serial.h"
#include "WS_Information.h"
#include "WS_Relay.h"
#include "WS_MQTT.h"
#include "WS_RTC.h"


#define SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"                                   // UUID of the server
#define DEVICE_LAST_CONNECT_UUID  "beb5484a-36e1-4688-b7f5-ea07361b26a8"                      // UUID of the characteristic to record last time a Bluetooth device connected to waveshare relay
#define RELAY_STATE_SET_CHARACTERISTIC_UUID  "beb5483e-36e1-4688-b7f5-ea07361b26a8"           // UUID of the characteristic that external Bluetooth device sends command to waveshare relay


#define Bluetooth_Mode    2


void Bluetooth_Init();
void BLETask(void *parameter);
void BLE_Set_RTC_Event(uint8_t* valueBytes);