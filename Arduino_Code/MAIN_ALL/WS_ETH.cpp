#include <time.h>
#include <sys/time.h>

#include "WS_ETH.h"


#if USE_TWO_ETH_PORTS
ETHClass ETH1(1);
#endif


IPAddress ETH_ip;


void testClient(const char *host, uint16_t port);
void diagnosis();

void printSystemTime()
{
  time_t now = time(nullptr);
  struct tm t;

  if (!gmtime_r(&now, &t))
  {
    Serial.printf("System time not available\n");
    return;
  }

  Serial.printf("SYS: %04d-%02d-%02d %02d:%02d:%02d\n",
                t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
                t.tm_hour, t.tm_min, t.tm_sec);
}

void printRTCTime()
{
  Serial.printf(
    "RTC: %04d-%02d-%02d %02d:%02d:%02d\n",
    datetime.year,
    datetime.month,
    datetime.day,
    datetime.hour,
    datetime.minute,
    datetime.second
  );
}

/*
diagnosis()

Purpose:
  One-shot network forensics and SNTP state verification.
  This function proves whether SNTP has already produced a valid system UTC time.
  It does NOT attempt to start or modify SNTP.

What it tests:
  1. Current Ethernet IP address
  2. DNS resolution (pool.ntp.org)
  3. Outbound TCP connectivity (HTTP)
  4. Whether the system clock already contains a valid UTC epoch from SNTP

Critical behavior:
  - Does NOT call configTime()
  - Does NOT initialize SNTP
  - Does NOT write RTC
  - Does NOT change any device state
  - Reads system time only

How SNTP is evaluated:
  - time(nullptr) is read once
  - If epoch > 1609459200 (2021-01-01 UTC), SNTP is considered successful
  - Otherwise SNTP is considered not working or not yet completed

When to use:
  - After Ethernet comes up
  - When SNTP behavior is in question
  - To prove whether failures are network-level or firmware-level
  - During bring-up or deployment to new networks

When NOT to use:
  - As a time acquisition mechanism
  - As a retry loop
  - In production control paths

Determinism:
  This function is side-effect free and reports the current network and system
  time state at the moment it is called; results reflect real-time conditions.

Expected outcomes:
  - DNS fail → network resolution problem
  - HTTP fail → general outbound connectivity issue
  - System epoch < 2021 → SNTP not functioning or blocked
  - System epoch valid → SNTP is working correctly

This function exists to remove ambiguity:
  It separates “SNTP creation” from “SNTP verification” and proves the result
  using only the system clock.
*/
void diagnosis()
{
    Serial.printf("Running network diagnostics...\n");

    IPAddress ip = ETH.localIP();
    Serial.printf("Current IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    // DNS test
    IPAddress testIP;
    if (Network.hostByName("pool.ntp.org", testIP))
    {
        Serial.printf("DNS OK: %d.%d.%d.%d\n", testIP[0], testIP[1], testIP[2], testIP[3]);
    }
    else
    {
        Serial.printf("DNS FAILED\n");
    }

    // TCP test
    Serial.printf("Testing HTTP connectivity...\n");
    testClient("example.com", 80);

    // Deterministic SNTP test (NO configTime here)
    Serial.printf("Testing system UTC (SNTP result)...\n");

    time_t now = time(nullptr);
    Serial.printf("System epoch=%ld\r\n", (long)now);

    if (now > 1609459200)
    {
        struct tm utc;
        gmtime_r(&now, &utc);

        Serial.printf("SNTP OK: %04d-%02d-%02d %02d:%02d:%02d UTC\r\n",
               utc.tm_year + 1900,
               utc.tm_mon + 1,
               utc.tm_mday,
               utc.tm_hour,
               utc.tm_min,
               utc.tm_sec);
    }
    else
    {
        Serial.printf("SNTP FAILED: system still at epoch %ld\n", (long)now);
    }
}

/*
Acquisition_time()

Purpose:
  Create a valid system UTC clock using the ESP32 native SNTP subsystem,
  then mirror that time into the external RTC (PCF85063) if SNTP succeeds
  within a fixed timeout window.

Design rules:
  - SNTP is initialized once via configTime(); timezone configuration is applied on each call.
  - The ESP32 system clock is the *only* source of truth.
  - The RTC is a mirror, never an authority.
  - No retry storms and no implicit SNTP re-initialization beyond the initial configTime() call.

What this function actually does:

  1. Forces libc into pure UTC:
        TZ=UTC0
        No offsets, no DST, no localization.

  2. Starts SNTP only once:
        configTime(0,0,servers)
        SNTP runs asynchronously, but this function blocks while polling the system clock.

  3. Polls the system clock:
        time(nullptr)

     Until either:
        A) time becomes sane (epoch > 1609459200)
        B) timeout expires (20 seconds)

  4. If SNTP responds in time:
        - System clock becomes real UTC
        - RTC is written from system UTC
        - Function returns TRUE

  5. If SNTP is slow or blocked:
        - System time stays near 0 / 1970
        - Loop times out
        - RTC is NOT written
        - Function returns FALSE

Determinism properties:

  Deterministic control flow with externally contingent outcome:

    Input:
      - Network availability
      - UDP/123 reachability
      - DNS resolution
      - SNTP server responsiveness

    Output:
      - TRUE  → system UTC became valid within the timeout and RTC was synchronized
      - FALSE → system UTC did not become valid within the timeout (SNTP may still succeed later)

  No partial success is acted upon; only system time validity within the timeout is accepted.
  No unbounded retries are performed by this function; polling is limited to a fixed timeout.
  No silent failure.
  No guessing.

What happens if SNTP is slow:

  Case 1: SNTP replies within 20 seconds
      → now > 1609459200
      → system UTC created
      → RTC written
      → SUCCESS

  Case 2: SNTP replies after 20 seconds
      → function already returned FALSE
      → system clock may update later
      → but RTC will NOT be written
      → caller must re-call Acquisition_time() if desired

  Case 3: SNTP never replies (UDP blocked / DNS broken)
      → system epoch remains small
      → function returns FALSE
      → RTC remains unchanged
      → hard failure is visible in logs

Why this is correct:

  - Prevents race conditions
  - Prevents RTC corruption with invalid time
  - Makes NTP failure explicit instead of implicit
  - Keeps time acquisition behavior explicit and bounded by a fixed timeout.

This function does NOT:
  - Retry forever
  - Mask network problems
  - Use RTC as fallback
  - Guess time
  - Accept partial initialization
*/
bool Acquisition_time(void)
{
    static bool sntp_initialized = false;

    Serial.printf("[NTP] Using native SNTP\n");

    setenv("TZ", "UTC0", 1);
    tzset();

    // Initialize SNTP only once
    if (!sntp_initialized) {
        configTime(0, 0, "pool.ntp.org", "time.nist.gov");
        sntp_initialized = true;
    }

    const uint32_t timeoutMs = 20000;
    uint32_t start = millis();

    while (millis() - start < timeoutMs)
    {
        time_t now = time(nullptr);
        Serial.printf("[NTP] system epoch=%ld\n", (long)now);

        // Epoch sanity check: > 2021-01-01
        if (now > 1609459200)
        {
            Serial.printf("[NTP] SUCCESS, system UTC created\n");

            // Write RTC from system UTC
            struct tm utc;
            gmtime_r(&now, &utc);

            datetime_t t = {0};
            t.year   = utc.tm_year + 1900;
            t.month  = utc.tm_mon + 1;
            t.day    = utc.tm_mday;
            t.dotw   = utc.tm_wday;
            t.hour   = utc.tm_hour;
            t.minute = utc.tm_min;
            t.second = utc.tm_sec;

            PCF85063_Set_All(t);
            datetime = t;

            Serial.printf("[RTC] Updated from system UTC\n");
            return true;
        }

        delay(500);
    }

    Serial.printf("[NTP] FAILURE: SNTP never set system time\n");
    return false;
}

void testClient(const char *host, uint16_t port)
{
  Serial.printf("\nconnecting to %s\n", host);

  NetworkClient client;
  
  if (!client.connect(host, port))
  {
    Serial.printf("connection failed\n");
    return;
  }

  client.printf("GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", host);

  uint32_t start = millis();
  while (client.connected() && !client.available())
  {
    if (millis() - start > 3000)   // 3s timeout
    {
      Serial.printf("timeout waiting for response\n");
      client.stop();
      return;
    }
    delay(10);
  }

  while (client.available())
  {
    Serial.printf("%c", (char)client.read());
  }

  Serial.printf("\nclosing connection\n");
  client.stop();
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info)
{
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      Serial.printf("ETH Started\n");
      //set eth hostname here
      ETH.setHostname("esp32-eth0");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED: 
      Serial.printf("ETH Connected\n");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ETH_ip = ETH.localIP();
      Serial.printf("[ETH GOT IP] %d.%d.%d.%d  epoch=%ld\n",
                     ETH_ip[0], ETH_ip[1], ETH_ip[2], ETH_ip[3],
                    (long)time(nullptr));
      Acquisition_time();
      printRTCTime();
      printSystemTime();
      break;                    
    case ARDUINO_EVENT_ETH_LOST_IP:
      Serial.printf("ETH Lost IP\n");
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      Serial.printf("ETH Disconnected\n");
      break;
    case ARDUINO_EVENT_ETH_STOP:
      Serial.printf("ETH Stopped\n");
      break;
    default: break;
  }
}

void ETH_Init(void)
{
  Serial.printf("Ethernet Start\n");
  Network.onEvent(onEvent);
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI);
#if USE_TWO_ETH_PORTS
  ETH1.begin(ETH1_PHY_TYPE, ETH1_PHY_ADDR, ETH1_PHY_CS, ETH1_PHY_IRQ, ETH1_PHY_RST, SPI);
#endif
}