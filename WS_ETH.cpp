#include "WS_ETH.h"
#include <time.h>
#include <sys/time.h>

void testClient(const char *host, uint16_t port);
void diagnosis();


static bool eth_connected = false;
static bool ntp_ok = false;

IPAddress ETH_ip;

void printSystemTime()
{
    time_t now = time(nullptr);
    struct tm *t = gmtime(&now);

    if (!t) {
        Serial.println("System time not available");
        return;
    }

    Serial.printf(
        "SYS: %04d-%02d-%02d %02d:%02d:%02d\n",
        t->tm_year + 1900,
        t->tm_mon + 1,
        t->tm_mday,
        t->tm_hour,
        t->tm_min,
        t->tm_sec
    );
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
  - Fully deterministic
  - No retries
  - No race conditions
  - No side effects
  - Same inputs always produce the same outputs

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
    printf("Running network diagnostics...\r\n");

    IPAddress ip = ETH.localIP();
    printf("Current IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

    // DNS test
    IPAddress testIP;
    if (Network.hostByName("pool.ntp.org", testIP)) {
        printf("DNS OK: %d.%d.%d.%d\n", testIP[0], testIP[1], testIP[2], testIP[3]);
    } else {
        printf("DNS FAILED\n");
    }

    // TCP test
    printf("Testing HTTP connectivity...\n");
    testClient("example.com", 80);

    // Deterministic SNTP test (NO configTime here)
    printf("Testing system UTC (SNTP result)...\n");

    time_t now = time(nullptr);
    printf("System epoch=%ld\r\n", (long)now);

    if (now > 1609459200) {
        struct tm utc;
        gmtime_r(&now, &utc);

        printf("SNTP OK: %04d-%02d-%02d %02d:%02d:%02d UTC\r\n",
               utc.tm_year + 1900,
               utc.tm_mon + 1,
               utc.tm_mday,
               utc.tm_hour,
               utc.tm_min,
               utc.tm_sec);
    } else {
        printf("SNTP FAILED: system still at epoch %ld\r\n", (long)now);
    }
}

/*
Acquisition_time()

Purpose:
  Deterministically create a valid system UTC clock using the ESP32 native SNTP
  subsystem, then mirror that time into the external RTC (PCF85063).

Design rules:
  - SNTP is initialized exactly once (static guard).
  - The ESP32 system clock is the *only* source of truth.
  - The RTC is a mirror, never an authority.
  - No retry storms, no hidden state, no implicit re-initialization.

What this function actually does:

  1. Forces libc into pure UTC:
        TZ=UTC0
        No offsets, no DST, no localization.

  2. Starts SNTP only once:
        configTime(0,0,servers)
     After that, the SNTP task runs asynchronously in the background.

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

  This function is deterministic in behavior:

    Input:
      - Network availability
      - UDP/123 reachability
      - DNS resolution
      - SNTP server responsiveness

    Output:
      - TRUE  → system UTC is valid, RTC is synchronized
      - FALSE → system UTC was never created

  No partial success states.
  No hidden retries.
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
  - Keeps security logic deterministic

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
        Serial.printf("[NTP] system epoch=%ld\r\n", (long)now);

        // Epoch sanity check: > 2021-01-01
        if (now > 1609459200) {
            Serial.println("[NTP] SUCCESS, system UTC created");

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

            Serial.println("[RTC] Updated from system UTC");
            return true;
        }

        delay(500);
    }

    Serial.printf("[NTP] FAILURE: SNTP never set system time\r\n");
    return false;
}


void testClient(const char *host, uint16_t port) {
  printf("\nconnecting to \r\n");;
  printf("%s\r\n",host);

  NetworkClient client;
  if (!client.connect(host, port)) {
    printf("connection failed\r\n");
    return;
  }
  client.printf("GET / HTTP/1.1\r\nHost: %s\r\n\r\n", host);
  while (client.connected() && !client.available());
  while (client.available()) {
    printf("%c",(char)client.read());
  }

  printf("closing connection\n");
  client.stop();
}

void onEvent(arduino_event_id_t event, arduino_event_info_t info) {
  switch (event) {
    case ARDUINO_EVENT_ETH_START:
      printf("ETH Started\r\n");
      //set eth hostname here
      ETH.setHostname("esp32-eth0");
      break;
    case ARDUINO_EVENT_ETH_CONNECTED: 
      printf("ETH Connected\r\n");
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ETH_ip = ETH.localIP();
      Serial.printf("[ETH GOT IP] %d.%d.%d.%d  epoch=%ld\n",
                     ETH_ip[0], ETH_ip[1], ETH_ip[2], ETH_ip[3],
                    (long)time(nullptr));

      Serial.printf("[NTP] Starting Acquisition_time()\n");
      ntp_ok = Acquisition_time();
      Serial.printf("[NTP] Acquisition_time returned %s\n", ntp_ok ? "TRUE" : "FALSE");
      Serial.printf("[NTP] System epoch after Acquisition=%ld\n",
                    (long)time(nullptr));

    if (ntp_ok) {
        printSystemTime();
        printRTCTime();
    } else {
        Serial.printf("NTP FAILED\r\n");
    }
    break;
    case ARDUINO_EVENT_ETH_LOST_IP:
      printf("ETH Lost IP\r\n");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_DISCONNECTED:
      printf("ETH Disconnected\r\n");
      eth_connected = false;
      break;
    case ARDUINO_EVENT_ETH_STOP:
      printf("ETH Stopped\r\n");
      eth_connected = false;
      break;
    default: break;
  }
}

void ETH_Init(void) {
  printf("Ethernet Start\r\n");
  Network.onEvent(onEvent);
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI);
  ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_CS, ETH_PHY_IRQ, ETH_PHY_RST, SPI);
#if USE_TWO_ETH_PORTS
  ETH1.begin(ETH1_PHY_TYPE, ETH1_PHY_ADDR, ETH1_PHY_CS, ETH1_PHY_IRQ, ETH1_PHY_RST, SPI);
#endif
}