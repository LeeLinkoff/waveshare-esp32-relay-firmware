# waveshare-esp32-relay-firmware

A standalone ESP32 firmware implementation for Waveshare relay boards, derived from vendor example code and extended with authenticated BLE commands, controlled logging, and brownout-aware execution.

---

## Key improvements

### Authenticated BLE control
Relay control over BLE is protected using an HMAC-based authentication scheme that binds each command to a short-lived UTC timestamp and relay channel identifier.

The device verifies:
- The system clock is valid and synchronized
- The command timestamp is within a narrow acceptance window
- The HMAC signature matches the expected value derived from the shared secret

Commands that fail validation are rejected before any relay state is modified. This prevents replay attacks, stale commands, and unauthenticated control over BLE.

---

### Elimination of tight vendor busy-loops
The original vendor firmware relies on infinite while(1) loops with fixed delays for tasks such as BLE status transmission.

These patterns:
- Waste CPU time
- Increase power draw
- Starve other FreeRTOS tasks under load
- Exacerbate brownout conditions on marginal power supplies

In this firmware, long-running behavior is implemented using cooperative FreeRTOS tasks that explicitly yield via vTaskDelay, allowing the scheduler to operate deterministically.

---

### Explicit task separation
Time-sensitive and potentially blocking behaviors are separated into distinct tasks with defined responsibilities and priorities.

Specifically:
- BLE I/O and packet parsing
- Relay actuation
- Buzzer signaling
- Status and telemetry transmission

Direct hardware signaling inside BLE callbacks is kept minimal and non-blocking, with no delays or long-running operations executed in callback context.
---

### Brownout-aware behavior
The firmware does not attempt to disable ESP32 brownout detection in software.

Instead, it is designed to behave predictably on boards with constrained or marginal power delivery by:
- Avoiding burst activity during callbacks
- Reducing simultaneous peripheral activation
- Preventing tight CPU loops
- Yielding cooperatively to the scheduler

Brownout detection remains enabled so that power issues are surfaced rather than masked.

---

### Deterministic logging
Serial logging is gated and explicitly flushed outside of time-sensitive execution paths.

On ESP32-S3 devices using USB CDC, uncontrolled logging can:
- Stall USB endpoints
- Block internal mutexes
- Trigger watchdog resets

This firmware ensures that logging does not interfere with BLE handling, relay control, or system stability.

---

## What this is not

This project is not:
- A fork intended to track upstream Waveshare releases
- A vendor-supported firmware replacement
- A drop-in image guaranteed to work on all Waveshare relay board variants

It is a reference-quality, independently maintained firmware derived from publicly released Waveshare example code, intended for engineers who want full visibility into system behavior and explicit control over BLE, FreeRTOS, and hardware interactions.