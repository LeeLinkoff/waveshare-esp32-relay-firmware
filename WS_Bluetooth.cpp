#include <string>
#include <cstring>

#include "WS_Bluetooth.h"


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


BLEServer* pServer;                                                             // Used to represent a BLE server
BLECharacteristic* pTxCharacteristic;
BLECharacteristic* pRxCharacteristic;

const uint8_t SECRET_KEY[] = "key-fsa-relay";
const size_t SECRET_LEN = sizeof(SECRET_KEY) - 1;


// ===== auth helpers =====

static bool systemUtcIsValid()
{
    time_t now = time(nullptr);
    return now > 1700000000;   // any sane date after 2023
}

static uint32_t sysUtcSecondsNow()
{
    return (uint32_t)time(nullptr);
}

// inLen must be 17 (binary) or 34 (ASCII hex). Output is always 17 bytes in out17
static bool normalizeAuthPayload(const uint8_t* in, size_t inLen, uint8_t* out17)
{
  // Case A: raw binary
  if (inLen == 17) {
    memcpy(out17, in, 17);
    return true;
  }

  // Case B: ASCII hex (UTF-8)
  if (inLen == 34)
  {
    auto hexNibble = [](uint8_t c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      return -1;
    };

    for (int i = 0; i < 17; i++) {
      int hi = hexNibble(in[i * 2]);
      int lo = hexNibble(in[i * 2 + 1]);
      if (hi < 0 || lo < 0) return false;
      out17[i] = (uint8_t)((hi << 4) | lo);
    }

    return true;
  }

  return false;
}

// ===== logging helpers (no Arduino String, binary safe) =====

static void logBleFrame(const uint8_t* data, size_t len)
{
  BLE_LOG("\n=== BLE RX START ===\n");

  BLE_LOG("Raw BLE len = %u\n", (unsigned)len);

  if (data == nullptr) {
    BLE_LOG("ERROR: rxData == nullptr\n");
  }
  else if (len == 0) {
    BLE_LOG("ERROR: len == 0\n");
  }
  else if (len > 0)
  {
    BLE_LOG("Raw bytes: ");
    for (size_t i = 0; i < len; i++) {
      BLE_LOG("%02X ", data[i]);
    }
    BLE_LOG("\n");
  }

  BLE_LOG("=== BLE RX END ===\n");
}

// ===== dispatch helpers (all noise lives here) =====

static void handleBle2Byte(const uint8_t* b)
{
  // 2-byte BLE command: [opcode, selector]
  // opcode 0x06 = RS485 bridge command
  // selector (buf[1]) selects a pre-defined Modbus RTU frame in Send_Data[][]
  // When Extension_Enable is true, forwards the selected frame over RS485
  // Used to control external (off-board) relay channels only

  BLE_LOG("PATH: 2 byte command\n");
  BLE_LOG("Bytes = %02X %02X\n", b[0], b[1]);
  BLE_LOG("Extension_Enable = %s\n", Extension_Enable ? "TRUE" : "FALSE");

  if (!Extension_Enable) {
    BLE_LOG("REJECT: Extension disabled\n");
    return;
  }

  if (b[0] != 0x06) {
    BLE_LOG("REJECT: opcode != 0x06 (got 0x%02X)\n", b[0]);
    return;
  }

  BLE_LOG("ACCEPT: RS485 command (selector=0x%02X)\n", b[1]);
  RS485_Analysis((uint8_t*)b);
}

static void handleBleRtc14(const uint8_t* b)
{
  // Legacy 14-byte RTC scheduling packet (unauthenticated).
  // Vendor-defined format used to program timed relay events over BLE.
  // Processed only when RTC_Event_Enable == true.
  // Independent of HMAC/authenticated relay control.
  //
  // Legacy 14-byte RTC scheduling packet (unauthenticated).
  // Format:
  // [0]  0xA1 start
  // [1]  Year century (BCD)
  // [2]  Year (BCD)
  // [3]  Month (BCD)
  // [4]  Day (BCD)
  // [5]  Day-of-week (BCD)
  // [6]  0xAA separator
  // [7]  Hour (BCD)
  // [8]  Minute (BCD)
  // [9]  Second (BCD)
  // [10] Relay/state (high nibble = channel, low nibble = state)
  // [11] All-relays flag (0=single, 1=all)
  // [12] Repetition mode
  // [13] 0xFF end

  BLE_LOG("PATH: 14 byte RTC event\n");
  BLE_LOG("RTC_Event_Enable = %s\n", RTC_Event_Enable ? "TRUE" : "FALSE");

  if (!RTC_Event_Enable) {
   BLE_LOG("REJECT: RTC events disabled\n");
    return;
  }

  BLE_LOG("ACCEPT: RTC event packet\n");
  BLE_Set_RTC_Event((uint8_t*)b);
}

static void handleBleAuth17or34(const uint8_t* raw, size_t rawLen, const uint8_t* SECRET_KEY, size_t SECRET_LEN)
{
  BLE_LOG("PATH: AUTH packet (rawLen=%u)\n", (unsigned)rawLen);

  uint8_t bin[17];
  if (!normalizeAuthPayload(raw, rawLen, bin)) {
    BLE_LOG("AUTH FAIL: normalizeAuthPayload failed\n");
    return;
  }

  if (rawLen == 34) {
    BLE_LOG("AUTH: decoded ASCII hex (34) to binary (17)\n");
  } else {
    BLE_LOG("AUTH: raw binary (17)\n");
  }

  BLE_LOG("AUTH bin[0..16]: ");
  for (int i = 0; i < 17; i++) Serial.printf("%02X ", bin[i]);
  BLE_LOG("\n");

  const uint8_t channel = bin[0];
  const uint32_t epoch =
    ((uint32_t)bin[1] << 24) |
    ((uint32_t)bin[2] << 16) |
    ((uint32_t)bin[3] << 8)  |
     (uint32_t)bin[4];

  BLE_LOG("Byte[0] Channel = %u\n", channel);
  BLE_LOG("Byte[1..4] Epoch UTC = %u (0x%02X %02X %02X %02X)\n", (unsigned)epoch, bin[1], bin[2], bin[3], bin[4]);

  BLE_LOG("Byte[5..16] HMAC recv(12) = ");
  for (int i = 5; i < 17; i++) Serial.printf("%02X ", bin[i]);
  BLE_LOG("\n");

  if (!systemUtcIsValid()) {
    BLE_LOG("AUTH FAIL: system UTC invalid (still 1970)\n");
    return;
  }

  const uint32_t now = sysUtcSecondsNow();
  const uint32_t diff = (now > epoch) ? (now - epoch) : (epoch - now);

  BLE_LOG("System UTC now = %u\n", (unsigned)now);
  BLE_LOG("Epoch delta    = %u sec\n", (unsigned)diff);

  if (diff > 120) {
    BLE_LOG("AUTH FAIL: epoch outside plus minus 120 sec\n");
    return;
  }

  uint8_t msg[5];
  msg[0] = channel;
  memcpy(msg + 1, bin + 1, 4);

  BLE_LOG("HMAC input msg(5) = ");
  for (int i = 0; i < 5; i++) Serial.printf("%02X ", msg[i]);
  BLE_LOG("\n");

  uint8_t fullMac[32];

  mbedtls_md_context_t ctx;
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  mbedtls_md_init(&ctx);
  mbedtls_md_setup(&ctx, info, 1);
  mbedtls_md_hmac_starts(&ctx, SECRET_KEY, SECRET_LEN);
  mbedtls_md_hmac_update(&ctx, msg, sizeof(msg));
  mbedtls_md_hmac_finish(&ctx, fullMac);
  mbedtls_md_free(&ctx);

  BLE_LOG("HMAC calc first12 = ");
  for (int i = 0; i < 12; i++) BLE_LOG("%02X ", fullMac[i]);
  BLE_LOG("\n");

  uint8_t mismatch = 0;
  for (int i = 0; i < 12; i++) {
    const uint8_t recv = bin[5 + i];
    const uint8_t calc = fullMac[i];
    const uint8_t x = (uint8_t)(calc ^ recv);
    mismatch |= x;

    BLE_LOG("HMAC[%02d]: calc=%02X recv=%02X xor=%02X\n", i, calc, recv, x);
  }

  if (mismatch != 0) {
    BLE_LOG("AUTH FAIL: HMAC mismatch\n");
    return;
  }

  BLE_LOG("AUTH OK\n");

  uint8_t cmd = (uint8_t)(channel + '0');
  BLE_LOG("Dispatch relay: channel=%u ascii=0x%02X\n", channel, cmd);
  
  Relay_Analysis(&cmd, Bluetooth_Mode);
}


/**********************************************************  Bluetooth   *********************************************************/

class MyServerCallbacks : public BLEServerCallbacks
{
    //By overriding the onConnect() and onDisconnect() functions
    
    // When the Device is connected, "Device connected" is printed.
    void onConnect(BLEServer* pServer)
    {                                        
      BLE_LOG("Device connected"); 
    }

    // "Device disconnected" will be printed when the device is disconnected
    void onDisconnect(BLEServer* pServer)
    {                                       
      BLE_LOG("Device disconnected");

      BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();                 // Re-broadcast so that the device can query
      pAdvertising->addServiceUUID(SERVICE_UUID);                                 // Re-broadcast so that the device can query
      pAdvertising->setScanResponse(true);                                        // Re-broadcast so that the device can query
      pAdvertising->setMinPreferred(0x06);                                        // Re-broadcast so that the device can query 
      pAdvertising->setMinPreferred(0x12);                                        // Re-broadcast so that the device can query 
      BLEDevice::startAdvertising();                                              // Re-broadcast so that the device can query 
      pRxCharacteristic->notify();                                                // Re-broadcast so that the device can query  
      pAdvertising->start();                                                      // Re-broadcast so that the device can query
    }
};


class MyRXCallback : public BLECharacteristicCallbacks
{
  public:
  /*
     onWrite()
     ├── logBleFrame()        (raw dump, always)
     └── dispatch by length
          ├── 2 bytes  → RS485 selector (external relays only)
          ├── 14 bytes → legacy RTC scheduling
          └── 17/34    → authenticated relay command (HMAC
  */
  void onWrite(BLECharacteristic* pCharacteristic) override
  {
    Buzzer_Open_Time(300, 0);
    Buzzer_Open_Time(300, 150);

    // ---- RAW BLE PAYLOAD (binary safe, no String, no getValue) ----
    const size_t len = pCharacteristic->getLength();
    const uint8_t* rxData = pCharacteristic->getData();

    logBleFrame(rxData, len);

    // ================= DISPATCH =================
    if (len == 2)
    {
      handleBle2Byte(rxData);
    }
    else if (len == 14)
    {
      handleBleRtc14(rxData);
    }
    else if (len == 17 || len == 34)
    {
      handleBleAuth17or34(rxData, len, SECRET_KEY, SECRET_LEN);
    }
    else
    {
      Serial.printf("REJECT: unsupported payload length\n");
    }

    pCharacteristic->setValue("");
  }
};

void BLE_Set_RTC_Event(uint8_t* valueBytes)
{
  if(valueBytes[0] == 0xA1 && valueBytes[6] == 0xAA  && valueBytes[13] == 0xFF )
  {
    datetime_t Event_Time={0};
    Event_Time.year = (valueBytes[1]/16*10 + valueBytes[1] % 16) *100 + valueBytes[2]/16*10 + valueBytes[2] % 16;
    Event_Time.month = valueBytes[3]/16*10 + valueBytes[3] % 16;
    Event_Time.day = valueBytes[4]/16*10 + valueBytes[4] % 16;
    Event_Time.dotw = valueBytes[5]/16*10 + valueBytes[5] % 16;
    // valueBytes[6] == 0xAA; // check
    Event_Time.hour = valueBytes[7]/16*10 + valueBytes[7] % 16;
    Event_Time.minute = valueBytes[8]/16*10 + valueBytes[8] % 16;
    Event_Time.second = valueBytes[9]/16*10 + valueBytes[9] % 16;
    Repetition_event Repetition = (Repetition_event)valueBytes[12];       // cyclical indicators
    if(valueBytes[11]){                                                   // Whether to control all relays   1:Control all relays    0：Control a relay
      uint8_t CHxs = valueBytes[10];                                      // relay control
      TimerEvent_CHxs_Set(Event_Time, CHxs, Repetition);
    }
    else{
      uint8_t CHx = valueBytes[10]/16;
      bool State =  (valueBytes[10] % 16);
      TimerEvent_CHx_Set(Event_Time,CHx, State, Repetition);
    } 
  }
}

 // Send data using Bluetooth
void Bluetooth_SendData(char* Data)
{ 
  if (Data != nullptr && strlen(Data) > 0) {
    if (pServer->getConnectedCount() > 0) {
      String SendValue = String(Data);  // Convert char* to String
      pTxCharacteristic->setValue(SendValue);  // Set SendValue to the eigenvalue (String type)
      pTxCharacteristic->notify();  // Sends a notification to all connected devices
    }
  }
}

void Bluetooth_Init()
{
  /*************************************************************************
  Bluetooth
  *************************************************************************/
  BLEDevice::init("ESP32-8-CHANNEL-RELAY");                                        // Initialize Bluetooth and start broadcasting                           
  pServer = BLEDevice::createServer();                                          
  pServer->setCallbacks(new MyServerCallbacks());                               
  BLEService* pService = pServer->createService(SERVICE_UUID);                  
  pTxCharacteristic = pService->createCharacteristic(
                                    TX_CHARACTERISTIC_UUID,
                                    BLECharacteristic:: PROPERTY_READ);         // The eigenvalues are readable and can be read by remote devices
  pRxCharacteristic = pService->createCharacteristic(
                                    RX_CHARACTERISTIC_UUID,
                                    BLECharacteristic::PROPERTY_WRITE);         // The eigenvalues are writable and can be written to by remote devices
  pRxCharacteristic->setCallbacks(new MyRXCallback());

  pRxCharacteristic->setValue("Successfully Connect To ESP32-S3-POE-ETH-8DI-8RO");      
  pService->start();   

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();                   
  pAdvertising->addServiceUUID(SERVICE_UUID);                                   
  pAdvertising->setScanResponse(true);                                          
  pAdvertising->setMinPreferred(0x06);                                          
  pAdvertising->setMinPreferred(0x12);                                          
  BLEDevice::startAdvertising();                                                
  pRxCharacteristic->notify();                                                    
  pAdvertising->start();

  //RGB_Open_Time(0, 0, 60,1000, 0); 

  xTaskCreatePinnedToCore(
    BLETask,    
    "BLETask",   
    4096,                
    NULL,                 
    2,                   
    NULL,                 
    0                   
  );
}

void BLETask(void *parameter)
{
  while(1){
    Bluetooth_SendData(ipStr);
    vTaskDelay(pdMS_TO_TICKS(500));
  }
  vTaskDelete(NULL);
}