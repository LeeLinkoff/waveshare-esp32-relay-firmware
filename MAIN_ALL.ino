#include <Arduino.h>

#include "esp_system.h"

#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include "WS_MQTT.h"
#include "WS_Bluetooth.h"
#include "WS_GPIO.h"
#include "WS_RTC.h"
#include "WS_ETH.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_heap_caps.h"


#ifndef WS_BLE_DEBUG
#define WS_BLE_DEBUG 1
#endif

#if WS_BLE_DEBUG
  #define BLE_LOG(...) Serial.printf(__VA_ARGS__)
  #define BLE_LOG_FLUSH_REQUEST() requestSerialFlush()
#else
  #define BLE_LOG(...)
  #define BLE_LOG_FLUSH_REQUEST()
#endif

static volatile bool g_flushRequested = false;


void requestSerialFlush() {
  g_flushRequested = true;
}

static const char* ResetReasonHuman(esp_reset_reason_t r)
{
  switch (r) {

    case ESP_RST_POWERON:
      return "Power was applied or restored (normal power-on)";

    case ESP_RST_EXT:
      return "External reset pin triggered (EN or RESET pin)";

    case ESP_RST_SW:
      return "Software requested a reboot (esp_restart or abort)";

    case ESP_RST_PANIC:
      return "Fatal crash (assert, abort, or unhandled exception)";

    case ESP_RST_INT_WDT:
      return "CPU watchdog fired (interrupts blocked too long)";

    case ESP_RST_TASK_WDT:
      return "Task watchdog fired (a task stalled or starved the scheduler)";

    case ESP_RST_WDT:
      return "System watchdog fired (system services stopped running)";

    case ESP_RST_BROWNOUT:
      return "Power dip or voltage drop (insufficient supply or relay surge)";

    default:
      return "Unknown reset reason";
  }
}

void RunInits()
{
  BLE_LOG("INIT GPIO");
  GPIO_Init();

  BLE_LOG("INIT I2C");
  I2C_Init();

  BLE_LOG("INIT RTC");
  RTC_Init();

  BLE_LOG("INIT ETH");
  ETH_Init();

  BLE_LOG("INIT MQTT");
  MQTT_Init();

  BLE_LOG("INIT RELAY");
  Relay_Init();

  BLE_LOG("INIT RELAY");
  Bluetooth_Init();
}

void LedTask(void *param)
{
    // optional boot delay
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1)
    {
        // green blink, low current
        RGB_Open_Time(0, 10, 0, 200, 800);

        // sleep longer than on+off
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void printEthMAC() 
{
   uint8_t mac[6];
   ETH.macAddress(mac);
   BLE_LOG("ETH MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void printFreeRTOSInfo()
{
  BLE_LOG(F("\n=== FreeRTOS / ESP32 INFO ==="));
  BLE_LOG("FreeRTOS tick rate : %d Hz\n", configTICK_RATE_HZ);
  BLE_LOG("CPU cores          : %d\n", portNUM_PROCESSORS);
  BLE_LOG("CPU frequency      : %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  BLE_LOG(F("============================\n"));
}


/********************************************************  Initializing  ********************************************************/
void setup()
{ 
  /*
    !!!!!!! VERY IMPORTAMNT !!!!!!!!!!
    > Use "Minimal SPIFFS (1.9<B APP with OTA/128KB SPIFFS)" so code fits
    > Make sure "USB CDC ON Boot" is enabled to see printing statements in serial monitor

    * IMPORTANT: Serial initialization order on ESP32 / ESP32-S3

      DO NOT call delay(), Serial.print(), Serial.printf(), or Serial.flush() *before* Serial.begin().

      Why this matters:
      - On ESP32, the Serial object is NOT initialized until Serial.begin() runs.
      - Before Serial.begin():
        - The UART/USB-CDC driver is not configured
        - Internal buffers and mutexes do not exist
      - If code calls Serial.printf() or Serial.flush() before Serial.begin():
      - The call can block forever waiting on uninitialized internals
      - Or trigger an assert / watchdog timeout
      - Result: ESP32 resets immediately
      - setup() runs again
      - Same bad code runs again
      - This creates an *infinite reset loop*

      Common failure pattern:
        delay(...)
        Serial.printf(...)   // ❌ Serial not initialized yet
        Serial.begin(...)

      Correct pattern (always):
        Serial.begin(...)
        delay(...)
        Serial.printf(...)

      On ESP32-S3 with USB CDC:
      - Early Serial output can also be silently dropped until the host opens the port
      - But calling Serial BEFORE Serial.begin() is always unsafe and can reset the chip

      Rule:  Serial.begin() MUST be the first Serial-related call in setup().
  */

  // !! SB CDC On Boot needs to be enabled  to see ppriting statements !!

  // ---- Serial: safe order, no early calls ----
  Serial.begin(115200);
  delay(3000);   // give USB CDC time, but do not depend on it

  BLE_LOG("RESET CAUSE: ");
  BLE_LOG("%s\n", ResetReasonHuman(esp_reset_reason()));

  RunInits();
  Buzzer_Open_Time(500, 0);
 
  BLE_LOG("Setting LED to blue");
  // BLUE immediately, no delay
  RGB_Open_Time(0, 0, 3, 3000, 0);

  BLE_LOG("INITIAL BOOT OK!");

  BLE_LOG("Setting LED to blinking green");
  xTaskCreatePinnedToCore(
    LedTask,
    "LedTask",
    2048,
    NULL,
    1,        // LOW priority
    NULL,
    0         // core 0 (safe)
  );

  BLE_LOG("FINAL BOOT OK!");

  printFreeRTOSInfo();

  printEthMAC();

  BLE_LOG_FLUSH_REQUEST();
}




/**********************************************************  While  **********************************************************/
void loop()
{
  if (g_flushRequested) {
    g_flushRequested = false;
    Serial.flush();
    vTaskDelay(pdMS_TO_TICKS(1));  // yield after flush
  }
}