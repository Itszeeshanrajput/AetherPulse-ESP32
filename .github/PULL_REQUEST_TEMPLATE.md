## Description
Brief summary of the changes introduced in this PR.

## Type of Change
- [ ] 🐛 Bug fix (non-breaking change which fixes an issue)
- [ ] ✨ New feature (non-breaking change which adds functionality)
- [ ] ⚡ Performance optimization (latency, throughput, or memory usage)
- [ ] 📝 Documentation update
- [ ] 🎨 UI/UX styling refinement

## Hardware Verification
- [ ] Built cleanly with `arduino-cli` or `PlatformIO`
- [ ] Verified on physical ESP32 silicon over USB serial at 115200 baud
- [ ] Checked free heap (`free_heap > 100KB`) to guarantee no memory leak
- [ ] Verified lwIP thread-safety (`LOCK_TCPIP_CORE` / `UNLOCK_TCPIP_CORE`)

## Checklist
- [ ] My code follows the project style and zero-modem-sleep latency principles
- [ ] I have not committed any personal credentials or private SSIDs (`credentials.h` remains gitignored)
- [ ] All new and existing tests pass cleanly
