#include <Arduino.h>              // Required

#include "esp_heap_caps.h"
#include "esp_system.h"           // esp_reset_reason()

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "I2C_Driver.h"           // I2C_Init()

#include "WS_GPIO.h"              // GPIO_Init(), Relay_Init(), RGB_Light()
#include "WS_RTC.h"               // RTC_Init()
#include "WS_ETH.h"               // ETH_Init(), ETH.macAddress()
#include "WS_Bluetooth.h"         // Bluetooth_Init()

#include "common.h"





void printHeapStats()
{
    Serial.printf("Free heap: %u\n", heap_caps_get_free_size(MALLOC_CAP_8BIT));
    Serial.printf("Largest free block: %u bytes\n", heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
    Serial.printf("Min ever free heap: %u\n", ESP.getMinFreeHeap());
}

static const char* ResetReasonHuman(esp_reset_reason_t r)
{
  switch (r)
  {
    case ESP_RST_UNKNOWN:     return "Reset reason cannot be determined";
    case ESP_RST_POWERON:     return "Power-on reset";
    case ESP_RST_EXT:         return "External reset pin";
    case ESP_RST_SW:          return "Software reset (esp_restart)";
    case ESP_RST_PANIC:       return "Panic reset (exception or abort)";
    case ESP_RST_INT_WDT:     return "Interrupt watchdog reset";
    case ESP_RST_TASK_WDT:    return "Task watchdog reset";
    case ESP_RST_WDT:         return "Other watchdog reset";
    case ESP_RST_DEEPSLEEP:   return "Wake from deep sleep";
    case ESP_RST_BROWNOUT:    return "Brownout reset (voltage drop)";
    case ESP_RST_SDIO:        return "SDIO reset";
    case ESP_RST_USB:         return "USB reset";
    case ESP_RST_JTAG:        return "JTAG reset";
    case ESP_RST_EFUSE:       return "eFuse error reset";
    case ESP_RST_PWR_GLITCH:  return "Power glitch reset";
    case ESP_RST_CPU_LOCKUP:  return "CPU lockup (double exception)";
    default:                  return "Unmapped reset reason";
  }
}

void RunInits()
{
  Serial.printf("INIT GPIO\n");
  GPIO_Init();

  Serial.printf("INIT I2C\n");
  I2C_Init();

  Serial.printf("INIT RTC\n");
  RTC_Init();

  Serial.printf("INIT ETH\n");
  ETH_Init();

  Serial.printf("INIT RELAY\n");
  Relay_Init();

  Serial.printf("INIT BLUETOOTH\n");
  Bluetooth_Init();
}

void printEthMAC() 
{
   uint8_t mac[6];
   ETH.macAddress(mac);
   Serial.printf("ETH MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void printFreeRTOSInfo()
{
  Serial.printf("\n=== FreeRTOS / ESP32 INFO ===\n");
  Serial.printf("FreeRTOS tick rate : %d Hz\n", configTICK_RATE_HZ);
  Serial.printf("CPU cores          : %d\n", portNUM_PROCESSORS);
  Serial.printf("CPU frequency      : %lu MHz\n", (unsigned long)getCpuFrequencyMhz());
  Serial.printf("============================\n");
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

   esp_reset_reason_t rr = esp_reset_reason();
  Serial.printf("RESET CAUSE: %s (code=%d)\n", ResetReasonHuman(rr), (int)rr);

  RunInits();
  Buzzer_Open_Time(500, 0);
 
  Serial.printf("Setting LED to blue\n");
  RGB_Light(0, 0, 255);   // stays blue
  delay(2000);   // 2 seconds of blue you can actually see

  Serial.printf("INITIAL BOOT OK!\n");

  printFreeRTOSInfo();
  printEthMAC();
  printHeapStats();

  Serial.printf("FINAL BOOT OK!\n");
  Serial.printf("Setting LED to blinking green\n");
}




/**********************************************************  While  **********************************************************/
void loop()
{
  static uint32_t last = 0;
  static bool on = false;

  uint32_t now = millis();
  if (now - last >= 500)
  {
    last = now;
    on = !on;

    if (on) {
      RGB_Light(0, 255, 0);   // green
    } else {
      RGB_Light(0, 0, 0);     // off
    }
  }
}