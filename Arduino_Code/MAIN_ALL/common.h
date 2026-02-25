#pragma once  // Prevent multiple inclusion of this header

#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t serialMutex;
