# ESP32 BLE Time-Based Authentication – Proof of Concept (Waveshare Platform)

## Read This First

This repository is **not** a production firmware.

It is a **proof of concept** demonstrating a **security pattern**, not an endorsement of the hardware, vendor SDK, or platform stability.

The proof of concept is complete.
Further development was intentionally abandoned due to **hardware instability and vendor-supplied code quality**.

---

## What This Code Demonstrates (Successfully)

This project demonstrates a **working security model**:

- Bluetooth Low Energy command authentication
- Replay protection using UTC time
- System time sourced from an external NTP server
- Validation of BLE commands using:
  - Channel
  - UTC epoch
  - HMAC
- Authenticated dispatch to GPIO or downstream peripherals (relays, etc.)

**This part works. Repeatedly and deterministically within the scope of the authentication protocol.**

The cryptographic and protocol logic is sound.

---

## Why This Project Stops Here

Development stopped **not because of the design**, but because of the **platform**.

### 1. Hardware Power Instability (Waveshare Board)

The Waveshare ESP32 board used in this project exhibits:

- Intermittent brownout resets
- Spontaneous resets with no deterministic software trigger observable at the application level
- Reset behavior that changes depending on:
  - Ethernet state
  - RTC writes
  - BLE activity
  - Timing of SNTP completion

These resets occur **after successful boot**, **after successful NTP sync**, and **after correct RTC updates**.

This is a **hardware / power-integrity problem**, not an application logic issue.

---

### 2. Fragile Vendor Ethernet + SNTP Stack

The vendor-supplied Ethernet integration is fragile and tightly coupled to execution context:

- SNTP behavior depends on where code runs (event callback vs loop)
- Minor refactoring can break time acquisition
- Blocking vs deferred execution changes system stability
- Identical logic behaves differently depending on file placement

This is **vendor stack fragility**, not misuse or misunderstanding of the APIs.

---

### 3. Unacceptable Reliability for Production Work

Extending this project beyond a proof of concept would require:

- Replacing vendor Ethernet code
- Re-engineering power delivery
- Deep FreeRTOS task isolation
- Long-term soak testing to survive random resets

None of that improves the **security model** being demonstrated.

At that point, the work becomes **salvaging a broken platform**, not building a product.

---

## What This Project Explicitly Does NOT Claim

This project does **not** claim:

- That Waveshare ESP32 boards are production-ready
- That the vendor Ethernet stack is stable
- That this firmware can run unattended long-term
- That this is suitable for industrial or commercial deployment

If long-term unattended reliability is a requirement, this hardware and vendor stack are unsuitable.

---

## Final Assessment

- The **BLE authentication design works**
- The **time-based replay protection works**
- The **cryptographic validation works**
- The **platform does not**

This repository exists to document a **security approach**, not to normalize or excuse unreliable hardware.

---

## Status

**Frozen. Proof of concept complete.**

No further effort will be spent compensating for Waveshare hardware instability or vendor SDK issues.