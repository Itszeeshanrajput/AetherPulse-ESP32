# Contributing to AetherPulse ⚡

First off, thank you for considering contributing to AetherPulse! Open source thrives because of makers, firmware hackers, and networking engineers like you.

Whether you're fixing a typo, optimizing lwIP packet buffers, adding support for a new ESP32 board, or designing a clean UI feature, your contributions are warmly welcomed.

---

## 🗺️ High-Level Architecture Overview

AetherPulse is built on top of the ESP-IDF / Arduino ESP32 core with high-performance C++ networking extensions:

| Module | Core File(s) | Responsibility |
| :--- | :--- | :--- |
| **Orchestrator** | `AetherPulse-ESP32.ino`, `config.h` | Dual-core task orchestration, startup sequencing, LED telemetry |
| **NAPT Engine** | `napt_engine.cpp`, `napt_engine.h` | Zero-copy lwIP layer-3 NAT table allocation and port forwarding |
| **Fast Roaming** | `roaming_engine.cpp`, `roaming_engine.h` | 1D Kalman filter RSSI smoothing, 802.11k/v neighbor scanning |
| **Web Portal** | `web_portal.cpp`, `portal_html.h` | Async web server, SSE real-time telemetry, HTML5 Canvas topology |
| **DNS Cache & Filter** | `dns_cache.cpp`, `dns_cache.h` | In-memory LRU DNS cache and sinkhole adblock engine |
| **CLI Shell** | `cli_shell.cpp`, `cli_shell.h` | Non-blocking VT100 serial console at 115200 baud |
| **NVS Storage** | `storage_manager.cpp`, `storage_manager.h` | Non-volatile flash persistence for Wi-Fi credentials and config |

---

## 🚀 Quickstart for Developers

### Option 1: Arduino CLI (Fastest CLI Build)

1. **Install arduino-cli**:
   ```bash
   curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
   ```
2. **Install ESP32 Core**:
   ```bash
   arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   arduino-cli core install esp32:esp32 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. **Compile**:
   ```bash
   arduino-cli compile --fqbn esp32:esp32:esp32:FlashSize=4M,PartitionScheme=default .
   ```
4. **Flash to Physical Hardware**:
   ```bash
   arduino-cli upload -p /dev/ttyUSB0 --fqbn esp32:esp32:esp32 .
   ```

### Option 2: PlatformIO

```bash
pio run
pio run -t upload
```

---

## 🎯 High-Priority Areas Where We Need Help

Check our [Good First Issues](https://github.com/Itszeeshanrajput/AetherPulse-ESP32/issues?q=is%3Aissue+is%3Aopen+label%3A%22good+first+issue%22) or pick from these roadmaps:

1. **Home Assistant Discovery**:
   - Expose an MQTT or mDNS auto-discovery payload so Home Assistant instantly discovers repeater telemetry (connected clients, upstream RSSI, chip temperature, uptime).
2. **Web Portal Multi-Language (i18n)**:
   - Add a lightweight client-side language dictionary for Spanish, German, French, Chinese, and Urdu in `portal_html.h`.
3. **Cheap Yellow Display (CYD / ESP32-2432S028R) LCD Support**:
   - Implement an optional TFT_eSPI driver to display real-time network throughput and topology on the 2.8" SPI display.
4. **ESP32-S3 / ESP32-C3 / ESP32-C6 Porting**:
   - Expand board definitions and test Wi-Fi 6 capabilities on RISC-V C6 chips.
5. **Battery / Solar Power-Save Scheduler**:
   - Configurable timed sleep or radio power reduction during off-peak hours.

---

## 📋 Coding Standards & Performance Principles

Because AetherPulse runs as a 24/7 autonomous networking appliance on constrained embedded hardware:

1. **Zero Modem Sleep**:
   - Never enable `WIFI_PS_MIN_MODEM` or `WIFI_PS_MAX_MODEM` on repeater paths; AetherPulse relies on `WIFI_PS_NONE` to ensure sub-10ms packet forwarding and zero jitter.
2. **Deterministic Heap Management**:
   - Never dynamically allocate in fast paths. Avoid frequent `String` concatenations in packet loops. Verify `ESP.getFreeHeap()` stays above 100 KB under continuous load.
3. **lwIP Thread Safety**:
   - Always wrap calls modifying lwIP internal state in `LOCK_TCPIP_CORE()` and `UNLOCK_TCPIP_CORE()`.
4. **Security & Privacy**:
   - **NEVER** hardcode or commit Wi-Fi passwords, SSIDs, or credentials to Git. Keep `credentials.h` strictly in `.gitignore`.

---

## 📤 Submitting a Pull Request

1. Fork the repo and create your branch from `main`:
   ```bash
   git checkout -b feature/awesome-new-capability
   ```
2. Commit your changes with clear, descriptive commit messages:
   ```bash
   git commit -m "feat(telemetry): add MQTT auto-discovery for Home Assistant"
   ```
3. Push to your fork:
   ```bash
   git push origin feature/awesome-new-capability
   ```
4. Open a Pull Request on GitHub using our PR template.

All contributors are prominently recognized in our project documentation and release notes! Thank you for helping build the fastest, most reliable open-source repeater firmware for ESP32.
