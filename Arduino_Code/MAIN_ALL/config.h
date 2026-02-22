#ifndef WS_BLE_DEBUG
#define WS_BLE_DEBUG 1
#endif


#if WS_BLE_DEBUG
 #define BLE_LOG(fmt, ...)                     \
    do {                                       \
      Serial.printf(fmt, ##__VA_ARGS__);       \
      Serial.print('\n');                      \
    } while (0)
  #define BLE_LOG_FLUSH_REQUEST() requestSerialFlush()
#else
  #define BLE_LOG(...)
  #define BLE_LOG_FLUSH_REQUEST()
#endif