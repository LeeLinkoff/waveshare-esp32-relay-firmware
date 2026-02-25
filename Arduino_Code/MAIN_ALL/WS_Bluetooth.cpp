#include "WS_Bluetooth.h"

#include "common.h"


BLEServer* pServer;                                                             // Used to represent a BLE server
BLECharacteristic* pTxCharacteristic;
BLECharacteristic* pRxCharacteristic;

const uint8_t SECRET_KEY[] = "key-fsa-relay";
const size_t SECRET_LEN = sizeof(SECRET_KEY) - 1;


static void formatUtcDatetime(char* out, size_t outLen)
{
    time_t now = time(nullptr);
    struct tm tmUtc;
    gmtime_r(&now, &tmUtc);

    // Example: 2026-02-22 23:41:05 UTC
    strftime(out, outLen, "%Y-%m-%d %H:%M:%S UTC", &tmUtc);
}

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

// ===== dispatch helpers (all noise lives here) =====

static void handleBle2Byte(const uint8_t* b)
{
  // 2-byte BLE command: [opcode, selector]
  // opcode 0x06 = RS485 bridge command
  // selector (buf[1]) selects a pre-defined Modbus RTU frame in Send_Data[][]
  // When Extension_Enable is true, forwards the selected frame over RS485
  // Used to control external (off-board) relay channels only

  Serial.printf("PATH: 2 byte command, Bytes = 0x%02X 0x%02X, Extension_Enable = %s \n", b[0], b[1], Extension_Enable ? "TRUE" : "FALSE");

  if (!Extension_Enable)
  {
    Serial.printf("REJECT: Extension disabled\n");
    return;
  }

  if (b[0] != 0x06)
  {
    Serial.printf("REJECT: opcode != 0x06 (got 0x%02X)\n", b[0]);
    return;
  }

  Serial.printf("ACCEPT: RS485 command (selector=0x%02X)\n", b[1]);

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

  Serial.printf("PATH: 14 byte RTC event, RTC_Event_Enable = %s\n", RTC_Event_Enable ? "TRUE" : "FALSE");

  if (!RTC_Event_Enable)
  {
    Serial.printf("REJECT: RTC events disabled\n");
    return;
  }

  Serial.printf("ACCEPT: RTC event packet\n");

  BLE_Set_RTC_Event((uint8_t*)b);
}

static void handleBleAuth17or34(const uint8_t* raw, size_t rawLen, const uint8_t* SECRET_KEY, size_t SECRET_LEN)
{
  uint8_t bin[17];

  Serial.printf("STACK HWM at entry: %u\n", uxTaskGetStackHighWaterMark(NULL));

  if (!normalizeAuthPayload(raw, rawLen, bin))
  {
    Serial.printf("AUTH FAIL: normalizeAuthPayload failed\n");
    return;
  }

  if (rawLen == 34)
  {
    Serial.printf("AUTH: decoded ASCII hex (34) to binary (17) \n");
  }
  else
  {
    Serial.printf("AUTH: raw binary (17) \n");
  }

  /*
    * Byte 0 → channel (1 byte)
    * Bytes 1–4 → epoch (4 bytes)
    * Bytes 5-16 → Remaining bytes (HMAC first 12 bytes)
  */

  const uint8_t channel = bin[0];
  const uint32_t epoch =
    ((uint32_t)bin[1] << 24) |
    ((uint32_t)bin[2] << 16) |
    ((uint32_t)bin[3] << 8)  |
     (uint32_t)bin[4];

  Serial.printf("RX: Byte[0] Channel = %u, Byte[1..4] epoch = %u (0x%02X 0x%02X 0x%02X 0x%02X), Byte[5..16] HMAC = 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n", 
                (unsigned)channel, (unsigned)epoch,
                (unsigned)bin[1], (unsigned)bin[2], (unsigned)bin[3], (unsigned)bin[4],
                (unsigned)bin[5], (unsigned)bin[6], (unsigned)bin[7], (unsigned)bin[8],
                (unsigned)bin[9], (unsigned)bin[10], (unsigned)bin[11], (unsigned)bin[12],
                (unsigned)bin[13], (unsigned)bin[14], (unsigned)bin[15], (unsigned)bin[16]);

  if (!systemUtcIsValid())
  {
    Serial.printf("AUTH FAIL: system UTC invalid (still 1970)\n");
    return;
  }

  const uint32_t now = sysUtcSecondsNow();
  const uint32_t diff = (now > epoch) ? (now - epoch) : (epoch - now);

  Serial.printf("System UTC now = %u, Epoch delta = %u sec\n", (unsigned)now, (unsigned)diff);

  if (diff > 120)
  {
    Serial.printf("AUTH FAIL: epoch outside plus minus 120 sec\n");
    return;
  }

  uint8_t msg[5];
  msg[0] = channel;
  memcpy(msg + 1, bin + 1, 4);

  Serial.printf("STACK before HMAC: %u\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));

  // -------------------------------------------
  uint8_t fullMac[32];

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);

  if (info == nullptr)
  {
    Serial.printf("AUTH FAIL: SHA256 md_info NULL\n");
    return;
  }

  int rc = mbedtls_md_hmac(info, SECRET_KEY, SECRET_LEN, msg, sizeof(msg), fullMac);

  Serial.printf("STACK after HMAC: %u\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));

  if (rc != 0)
  {
    Serial.printf("AUTH FAIL: md_hmac rc=%d\n", rc);
    char errbuf[64];
    mbedtls_strerror(rc, errbuf, sizeof(errbuf));
    Serial.printf("AUTH FAIL: md_hmac rc=%d (%s)\n", rc, errbuf);
    return;
  }

  // The HMAC was not successfully computed.
  // HMAC computed successfully (rc == 0) — does NOT imply authentication is valid
  // 
  // Example:
  //
  // Given:
  //   SECRET_KEY = "key-fsa-relay"
  //   channel = 1
  //   epoch = 1739990000
  //   msg = [0x01 0x67 0xB5 0xA2 0x30]
  //
  //   Epoch 1739990000 in hex: 0x67B5A230
  //   So channel with epoch is: 01 67 B5 A2 30
  //
  // mbedtls_md_hmac(...) may return rc == 0 and produce:
  //
  //   fullMac = 9A 4F 12 7C 55 8D 21 90 AB 44 66 7F ...
  //
  // If incoming packet contains:
  //   01 67 B5 A2 30 AA AA AA AA AA AA AA AA AA AA AA AA
  //
  // rc is STILL 0 (HMAC computed successfully),
  // but memcmp(fullMac, bin+5, 12) will FAIL.
  //
  // rc == 0 means "HMAC calculated"
  // It does NOT mean "authentication valid"

  // Compare first 12 bytes of computed HMAC with received MAC (bin[5..16])
  if (memcmp(fullMac, bin + 5, 12) != 0)
  {
     Serial.printf("AUTH FAIL: HMAC mismatch\n");
     return;
  }

  Serial.printf("AUTH OK\n");

  uint8_t cmd = (uint8_t)(channel + '0');

  Serial.printf("Dispatch relay: channel=%u ascii=0x%02X\n", (unsigned)channel, (unsigned)cmd);

  Relay_Analysis(&cmd, Bluetooth_Mode);
}


/**********************************************************  Bluetooth   *********************************************************/

class MyServerCallbacks : public BLEServerCallbacks
{
    //By overriding the onConnect() and onDisconnect() functions
    
    // When the Device is connected, "Device connected" is printed.
    void onConnect(BLEServer* pServer)
    {                                        
      Serial.printf("Device connected\n"); 
    }

    // "Device disconnected" will be printed when the device is disconnected
    void onDisconnect(BLEServer* pServer)
    {                                       
      Serial.printf("Device disconnected\n");
    }
};


class MyRXCallback : public BLECharacteristicCallbacks
{
  public:
  /*
     onWrite()
     ├── buzzer pulse
     ├─ get raw pointer + length
      ├─ ASCII debug dump (bounded)
      ├─ dispatch by length:
      │     2   → handleBle2Byte
      │     14  → handleBleRtc14
      │     17/34 → handleBleAuth17or34
      │     else → reject
  */
  void onWrite(BLECharacteristic* pCharacteristic) override
  {
    Buzzer_Open_Time(300, 0);
    Buzzer_Open_Time(300, 150);

    // ---- RAW BLE PAYLOAD (binary safe, no String, no getValue) ----
    const size_t rxLen = pCharacteristic->getLength();
    const uint8_t* rxData = pCharacteristic->getData();

    Serial.printf("\nBLE on, Write fired, len=%u \n", pCharacteristic->getLength());
  
    // Print as ASCII safely (length-controlled)
    size_t n = rxLen;
    if (n > 64) n = 64;   // clamp to safe size
    char tmp[65];
    memcpy(tmp, rxData, n);
    tmp[n] = '\0';
    Serial.printf("ASCII: %s \n", tmp);

    if (!rxData || rxLen == 0) return;

    // ================= DISPATCH =================
    if (rxLen == 2)
    {
      handleBle2Byte(rxData);
    }
    else if (rxLen == 14)
    {
      handleBleRtc14(rxData);
    }
    else if (rxLen == 17 || rxLen == 34)
    {
      handleBleAuth17or34(rxData, rxLen, SECRET_KEY, SECRET_LEN);
    }
    else
    {
      Serial.printf("REJECT: unsupported payload length\n");
    }
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
  // Use raw buffer instead of Arduino String to avoid heap allocation and fragmentation in long-running BLE task
  if (!Data || !Data[0] || !pTxCharacteristic) return;

  size_t n = strnlen(Data, 256);
  if (n == 256) return;   // unterminated or too long → reject

  pTxCharacteristic->setValue((uint8_t*)Data, n);
}

void Bluetooth_Init()
{
  Serial.println("Before BLE init:");
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
  BLEDevice::init("ESP32-8-CHANNEL-RELAY");  // Initialize Bluetooth and start broadcasting                           
  Serial.println("After BLE init:");
  Serial.printf("Free heap: %u\n", ESP.getFreeHeap());
  Serial.println("\n");

  pServer = BLEDevice::createServer();                                          
  pServer->setCallbacks(new MyServerCallbacks());                               
  BLEService* pService = pServer->createService(SERVICE_UUID);

  pTxCharacteristic = pService->createCharacteristic(TX_CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_READ);
  BLEDescriptor* txDesc = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
  txDesc->setValue("Relay Status Output");
  pTxCharacteristic->addDescriptor(txDesc);
  
  pRxCharacteristic = pService->createCharacteristic(
                                    RX_CHARACTERISTIC_UUID,
                                    BLECharacteristic::PROPERTY_WRITE);
  pRxCharacteristic->setCallbacks(new MyRXCallback());
  BLEDescriptor* rxDesc = new BLEDescriptor(BLEUUID((uint16_t)0x2901));
  rxDesc->setValue("Relay Control Commands");
  pRxCharacteristic->addDescriptor(rxDesc);

  char dt[32];
  formatUtcDatetime(dt, sizeof(dt));
  char msg[128];
  snprintf(msg, sizeof(msg), "Successfully connected to ESP32-S3-POE-ETH-8DI-8RO on %s", dt);
  pTxCharacteristic->setValue((uint8_t*)msg, strlen(msg));

  pService->start();   

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();                   
  pAdvertising->addServiceUUID(SERVICE_UUID);                                   
  pAdvertising->setScanResponse(true);                                          
  pAdvertising->setMinPreferred(0x06);                                          
  pAdvertising->setMinPreferred(0x12);                                          
  BLEDevice::startAdvertising();                                                
  pAdvertising->start();
}
