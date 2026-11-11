# Contributing to AetherPulse Extreme

We welcome contributions from engineers, firmware developers, and wireless networking enthusiasts.

## How to Contribute

1. **Fork the Repository**: Clone your fork locally.
2. **Branch**: Create a feature branch (`git checkout -b feature/low-latency-tweak`).
3. **Build & Verify**:
   - Compile using `arduino-cli compile --fqbn esp32:esp32:esp32 .` or PlatformIO.
   - Test on physical ESP32 silicon over USB serial at 115200 baud.
4. **Code Quality**:
   - Maintain zero modem sleep (`WIFI_PS_NONE`) for jitter-free ping.
   - Respect lwIP thread safety (`LOCK_TCPIP_CORE` / `UNLOCK_TCPIP_CORE`).
   - Keep heap allocations deterministic for continuous 24/7 uptime.
5. **Pull Request**: Submit your PR with a description of testing and packet throughput logs.
