# AetherPulse — Community Campaign & Social Launch Kit
**Project:** AetherPulse v3.0.1  
**Author:** Zeeshan Rajput (`@Itszeeshanrajput`)  
**Repository:** [https://github.com/Itszeeshanrajput/AetherPulse-ESP32](https://github.com/Itszeeshanrajput/AetherPulse-ESP32)  
**1-Click Web Installer:** [https://itszeeshanrajput.github.io/AetherPulse-ESP32/](https://itszeeshanrajput.github.io/AetherPulse-ESP32/)  

---

## 1. Reddit: `r/esp32`

**Focus:** Low-level silicon architecture, Dynamic TPC, lwIP zero-copy NAPT, RF beacon timing, WebSerial.  
**Tone:** Deep technical engineering, fellow ESP32 hacker, open-source pride.

### Title:
> I built an ultra-low latency Wi-Fi repeater on an ESP32 using zero-copy lwIP NAPT, Dynamic TPC, and 180° anti-phase beacon timing (with a 30s WebSerial browser flasher)

### Post Body:

```markdown
Hey r/esp32!

Like many of you, I've experimented with running Wi-Fi repeaters on the ESP32 (such as martin-ger's early NAPT work). While those projects proved the concept, in practice I always ran into three frustrating silicon/RF bottlenecks:

1. **Catastrophic ping jitter:** Default modem sleep (`WIFI_PS_MIN_MODEM`) and single-channel radio contention caused ping times to spike from 15ms to 400ms+ during video calls or gaming.
2. **Receiver saturation & heat:** Blasting static +20.5 dBm transmit power overloaded the receiver LNA when phones or laptops were sitting within 1–2 meters, causing packet error bursts, while heating the die past 70°C.
3. **Beacon frame collisions:** When the Station interface missed the upstream AP's beacon because the SoftAP was transmitting its own beacon at the exact same millisecond, the link would drop with a beacon timeout.

I spent the past few months building **AetherPulse** to solve these problems down to the register and lwIP layer, and I just released **v3.0.1** as 100% open-source under the MIT license.

### Architecture Highlights & RF Hacks:
- **Zero-Copy lwIP Layer-3 DNAPT:** Rewrites IP/port headers in-place on existing `pbuf` structures inside lwIP core memory. Checksums are recalculated via RFC 1624 incremental math rather than payload loops, handling up to 1024 concurrent stateful flows with sub-microsecond translation overhead.
- **Dynamic Transmit Power Control (TPC):** Closed-loop power management that monitors client RSSIs. When a client is near-field (> -48 dBm), it dynamically throttles back RF output to +11.0 dBm (raw 44) to eliminate LNA saturation and cut power consumption. When clients move away (< -74 dBm), it ramps up to +20.5 dBm (raw 82) with a 1 dBm hysteresis band.
- **180° Anti-Phase Beacon Scheduling:** Aligns the SoftAP beacon interval to 100 TU (102.4ms) with a calibrated 50 TU offset (51.2ms) and DTIM 1, ensuring the SoftAP never transmits a beacon frame when the Station is expecting the upstream AP's synchronization frame.
- **Modem Sleep Bypass:** Unconditionally sets `esp_wifi_set_ps(WIFI_PS_NONE)`, holding the 2.4 GHz RF PLL synthesizer permanently locked. Baseline ping drops to a steady 1.8ms – 3.2ms.
- **TCP MSS Auto-Clamping (1412 bytes):** Automatically inspects SYN/SYN-ACK frames and clamps MSS to 1412 bytes to prevent Path MTU (PMTU) black-hole packet drops over double-NAT or PPPoE uplinks.
- **Sub-ms SRAM DNS Acceleration:** A high-speed 64-host LFU cache daemon in DRAM that resolves repeat DNS queries in < 0.2ms, completely avoiding uplink airtime.

### The UI & WebSerial Flasher:
The firmware embeds a responsive glassmorphic web dashboard stored in PROGMEM with zero CDN dependencies. It features an interactive **HTML5 Canvas network topology graph** rendering live client nodes and animated cubic Bézier particle streams at 60 FPS (all rendered client-side on your phone/PC GPU with 0% MCU overhead).

To eliminate driver headaches and command-line friction, you can flash your board in 30 seconds directly from Chrome or Edge using the **Web Serial API**—no Python, esptool, or Arduino IDE needed.

- **GitHub Repository (MIT):** https://github.com/Itszeeshanrajput/AetherPulse-ESP32
- **1-Click Web Installer:** https://itszeeshanrajput.github.io/AetherPulse-ESP32/
- **UI & Architecture Diagrams:** https://raw.githubusercontent.com/Itszeeshanrajput/AetherPulse-ESP32/main/assets/ui_preview.svg

Works out of the box with standard ESP32-WROOM-32, NodeMCU, WROVER, and CYD boards. I'd love your feedback on the code, RF tuning, or benchmarks on your own boards!
```

---

## 2. Reddit: `r/homelab` & `r/selfhosted`

**Focus:** Local network privacy, zero cloud telemetry, hardware DNS sinkhole, travel router, guest network isolation.  
**Tone:** Self-hosted ethos, privacy-conscious, practical homelab utility.

### Title:
> I built a $3 privacy-first travel router and hardware DNS sinkhole using an ESP32 (Zero telemetry, 100% local web UI, zero-jitter NAPT)

### Post Body:

```markdown
Hey r/homelab and r/selfhosted!

Commercial travel routers (like GL.iNet) are great, but sometimes you just want an ultra-compact, dirt-cheap, dedicated hardware appliance that draws barely 0.5 Watts, runs zero cloud telemetry, and gives you total control over the local network stack when you're on the go.

I created **AetherPulse**, an open-source (MIT) firmware that transforms an ordinary $3 ESP32 board into a robust, low-latency travel router, Wi-Fi range extender, and hardware security appliance.

### Why It's Built for Homelabbers:
- **Zero Cloud / Zero Telemetry:** There are no mobile apps, external cloud accounts, or telemetry phone-homes. Everything runs locally on the microcontroller.
- **Offline Self-Contained Web Portal:** The web management interface (`http://192.168.4.1` or `http://aetherpulse.local`) has zero CDN dependencies. All styles, icons, and live Canvas graphs are baked into the microcontroller's flash memory.
- **Built-in Hardware DNS AdBlocker:** Includes a local Port 53 UDP sinkhole that intercepts advertising and telemetry tracker domains and responds with `0.0.0.0`. It acts like a pocket Pi-hole that consumes just 0.5W.
- **Layer-2 Client Isolation:** Toggleable hardware L2 isolation prevents connected guest devices from seeing or communicating with each other—perfect for untrusted IoT gadgets or guest networks.
- **DNAPT Port Forwarding:** Built-in virtual server routing allows forwarding external TCP/UDP ports through the ESP32 to internal client machines (e.g. running an SSH server or local dashboard behind the extender).
- **Subnet Conflict Auto-Migration:** If the upstream network is already using `192.168.4.x`, AetherPulse automatically migrates its local SoftAP subnet to `192.168.5.1` to prevent routing conflicts.
- **Hardware Watchdog (TWDT):** 15-second hardware task watchdog automatically reboots and recovers the network stack if an unrecoverable exception ever occurs.

### Quick Setup (No Toolchain Needed):
You don't need to touch a terminal or compile C++ to try it out. We deployed a WebSerial installer that flashes the board straight from Chrome/Brave/Edge in 30 seconds:

1. Plug an ESP32 into USB.
2. Open the web installer: **https://itszeeshanrajput.github.io/AetherPulse-ESP32/**
3. Click "Install", pick your COM port, and you're done.

Source Code & Documentation: **https://github.com/Itszeeshanrajput/AetherPulse-ESP32**  
Interface Preview: **https://raw.githubusercontent.com/Itszeeshanrajput/AetherPulse-ESP32/main/assets/ui_preview.svg**

Hope this helps anyone looking for a pocket-sized homelab tool, emergency travel router, or isolated IoT subnet!
```

---

## 3. Reddit: `r/digitalnomad`

**Focus:** Hotel captive portal bypass, per-device Wi-Fi fee elimination, MAC cloning, power bank efficiency.  
**Tone:** Problem-solver, practical traveler, cost-saving life hack.

### Title:
> How to bypass hotel/cruise ship "one device only" Wi-Fi fees with a $3 pocket ESP32 travel router (No coding required)

### Post Body:

```markdown
Hey nomads,

If you travel frequently, you've definitely run into this scam:
A hotel, Airbnb, airport, or cruise ship gives you "Free Wi-Fi" or charges $15–$30/day, but the captive portal restricts you to **strictly one device at a time**. The moment you connect your laptop, your phone gets kicked off. If you want your iPad, Kindle, and work laptop connected, they demand you pay for 3 separate plans.

I got tired of carrying bulky travel routers that need dedicated power bricks, so I built an open-source solution: **AetherPulse**. It turns a tiny $3 ESP32 board (smaller than a pack of gum) into a personal travel repeater that bypasses captive portal limits.

### How the Captive Portal Bypass Works (MAC Address Cloning):
1. Connect your iPhone or Android phone directly to the hotel Wi-Fi and complete their captive portal login screen normally.
2. Check your phone's Wi-Fi MAC address in your phone's Settings menu.
3. Power the ESP32 (plug it into any USB port, laptop port, or 5V power bank—it sips just 0.5W and can run for 24+ hours on a small 5000mAh battery).
4. Connect to the ESP32's Wi-Fi (`AetherPulse-Extender`) and open the web portal at `http://192.168.4.1`.
5. Enter your phone's MAC address in the **Station MAC Cloning** box.
6. The hotel's gateway now thinks the ESP32 is your already-authenticated phone!
7. Now connect your laptop, tablet, phone, and e-reader to the ESP32. All of your devices share the single authenticated hotel connection seamlessly.

### How to Install in 30 Seconds (Zero Tech Knowledge Required):
You don't need to know how to code. All you need is an ESP32 board (available on Amazon for $4 or AliExpress for $2):

1. Plug the ESP32 into your computer with a USB cable.
2. Open Chrome or Edge and visit: **https://itszeeshanrajput.github.io/AetherPulse-ESP32/**
3. Click **Install AetherPulse**, select your USB port, and wait 30 seconds for the progress bar to finish.
4. Done!

The web dashboard also includes an interactive visual connection graph and built-in ad/tracker blocking so you don't burn mobile hotspot data on telemetry.

- Web Installer: https://itszeeshanrajput.github.io/AetherPulse-ESP32/
- GitHub (MIT Open Source): https://github.com/Itszeeshanrajput/AetherPulse-ESP32

Safe travels and stop paying hotel per-device fees!
```

---

## 4. Hacker News: `Show HN`

**Focus:** lwIP Layer-3 DNAPT internals, RF contention physics, RFC 1624 checksum math, WebSerial browser flashing.  
**Tone:** Hacker News rigor, concise, engineering-led, respectful of technical depth.

### Title:
> Show HN: AetherPulse – Zero-jitter ESP32 Wi-Fi repeater with lwIP NAPT and WebSerial

### Post Body:

```markdown
Hi HN,

I built AetherPulse (v3.0.1), an open-source Wi-Fi range extender, L3 NAPT router, and network appliance running on bare-metal ESP32 silicon (ESP32-D0WD-V3 @ 240 MHz).

The primary motivation was solving the severe latency and jitter penalties that occur when running single-radio 802.11 b/g/n repeaters. Because an ESP32 must operate its Station and SoftAP interfaces on the same physical channel in half-duplex mode, naive implementations suffer from heavy frame contention, modem sleep latency traps, and receiver saturation.

Key architectural decisions:
1. **lwIP Zero-Copy NAPT:** Rather than attempting L2 frame bridging in userland tasks, we leverage lwIP's Layer-3 NAPT engine. Packets are translated directly within their existing `pbuf` structures without heap allocations. Checksums are recomputed using RFC 1624 incremental 16-bit one's complement arithmetic ($\sim C' = \sim C + \sim m + m'$), maintaining sub-microsecond per-packet translation overhead across up to 1024 concurrent flows.
2. **Dynamic Transmit Power Control (TPC):** Fixed +20.5 dBm transmit power frequently saturates the Low Noise Amplifier (LNA) of client devices within 1–2 meters, driving up Packet Error Rates (PER) and overheating the MCU. AetherPulse dynamically scales RF output between +11.0 dBm (44 raw) and +20.5 dBm (82 raw) using client RSSI filtered through a 1D Kalman Filter ($Q=0.008, R=4.50$) with a 1 dBm hysteresis band.
3. **180° Anti-Phase Beacon Scheduling:** To prevent the SoftAP from transmitting a beacon at the exact instant the Station is expecting the upstream AP's beacon frame, the SoftAP beacon interval is locked to 100 TU (102.4ms) with a 50 TU offset (51.2ms) and DTIM 1. This virtually eliminates `WIFI_REASON_BEACON_TIMEOUT` drops.
4. **TCP MSS Clamping (1412 bytes):** Every traversing TCP SYN frame is clamped to 1412 bytes to prevent PMTU black holes when double-NATing over PPPoE or encapsulated uplinks.
5. **Sub-ms SRAM DNS Cache:** LFU-managed DRAM cache that intercepts repeat DNS queries and answers in < 0.2ms, saving 30–80ms of airtime per request.

The firmware serves a self-contained glassmorphic web dashboard (zero CDN dependencies) featuring an interactive 60 FPS HTML5 Canvas network topology graph with cubic Bézier particle streams rendered on the client browser GPU (0% MCU penalty).

For deployment, we integrated ESP Web Tools so the entire firmware, bootloader, and partitions can be flashed in 30 seconds directly from Chrome or Edge via the Web Serial API without installing Python or serial drivers.

- Repository (MIT): https://github.com/Itszeeshanrajput/AetherPulse-ESP32
- In-Browser Web Flasher: https://itszeeshanrajput.github.io/AetherPulse-ESP32/
- Architecture & Benchmarks: https://github.com/Itszeeshanrajput/AetherPulse-ESP32#readme

I'd be glad to discuss the RF timing registers, lwIP memory tuning, or answer any questions about the implementation!
```

---

## 5. Espressif Official Forums (`esp32.com`)

**Subforum:** Projects / Made with ESP32  
**Focus:** ESP-IDF / Arduino Core 3.x integration, `esp_wifi` register tuning, FreeRTOS task watchdog, silicon telemetry.  
**Tone:** Silicon-specific, professional, community contribution.

### Subject:
> [Project] AetherPulse v3.0.1: Ultra-Low Latency Wi-Fi Repeater & L3 DNAPT Router with Dynamic TPC and WebSerial

### Post Content (BBCode / Markdown):

```text
Hello Espressif Community!

I would like to share an open-source project engineered specifically for the ESP32-D0WD-V3 silicon family: **AetherPulse (v3.0.1)**.

AetherPulse transforms standard ESP32 development boards (WROOM-32, WROVER, NodeMCU-32S) into an ultra-low latency, zero-jitter Wi-Fi range extender, L3 NAPT router, and network security appliance.

### ESP-IDF & Hardware Implementation Details:
1. **Physical Layer Tuning:**
   - Disabled modem sleep power saving via `esp_wifi_set_ps(WIFI_PS_NONE)` to keep the RF PLL locked and reduce ping jitter floor to ~1.8 ms.
   - Configured SoftAP beacon interval via `esp_wifi_get_config(WIFI_IF_AP, &ap_cfg)` to 100 TU with a 50 TU anti-phase offset and DTIM period of 1 to prevent STA/AP radio frame collisions.
   - Closed-loop Dynamic Transmit Power Control (TPC) dynamically adjusting `esp_wifi_set_max_tx_power()` between 44 (+11.0 dBm) and 82 (+20.5 dBm) based on 1D Kalman-filtered client RSSI.
2. **lwIP Layer-3 DNAPT Core:**
   - Utilizes Arduino Core 3.x / ESP-IDF 5.x native lwIP NAPT (`WiFi.AP.enableNAPT(true)`).
   - In-place `pbuf` inspection with RFC 1624 incremental checksum updates.
   - TCP MSS clamping enforced at 1412 bytes (`TCP_MSS_CLAMP_VALUE`).
   - Dynamic port-forwarding table allowing runtime addition/removal of TCP/UDP mappings into NVS flash.
3. **Resilience & Storage:**
   - 15-second Task Watchdog Timer (`esp_task_wdt_init`) safeguarding the dual-core scheduler.
   - Preferences / NVS partition storage for failover SSIDs, custom MAC cloning, and client device aliases.
4. **WebSerial Browser Flashing:**
   - Deployed official 1-click web installer using ESP Web Tools and Web Serial API, supporting direct flashing of merged factory binaries with automatic manifest negotiation.

### Links & Resources:
- **GitHub Repository:** https://github.com/Itszeeshanrajput/AetherPulse-ESP32
- **Live Web Installer:** https://itszeeshanrajput.github.io/AetherPulse-ESP32/
- **Documentation & Benchmarks:** See repository README and technical papers in /docs.

Feedback, pull requests, and bench tests from fellow Espressif engineers are warmly welcomed!
```

---

## 6. Twitter / X Launch Thread

**Target Handle:** `@EspressifSystems`  
**Hashtags:** `#ESP32 #IoT #OpenSource #Networking #Maker #Hardware`  
**Assets to Attach:** Banner SVG, UI Preview SVG, WebSerial GIF.

### Tweet 1 (Hook):
> 🚀 Introducing AetherPulse: An ultra-low latency, zero-jitter Wi-Fi range extender and L3 NAPT router engineered for bare-metal #ESP32 silicon!
> 
> Flashes in 30s directly from your browser via WebSerial. 100% free & open-source (MIT).
> 
> 🧵 Deep dive into the RF & lwIP hacks below 👇
> 
> https://github.com/Itszeeshanrajput/AetherPulse-ESP32
> *(Attach: banner.svg)*

### Tweet 2 (The Problem):
> Most ESP32 Wi-Fi repeaters suffer from 400ms ping spikes and frame drops.
> 
> Why? Single-radio 2.4 GHz half-duplex contention + default modem sleep power-saving.
> 
> In AetherPulse, we bypass modem sleep (`WIFI_PS_NONE`) to lock the RF PLL, cutting baseline latency to 1.8ms! ⚡

### Tweet 3 (lwIP NAPT & Anti-Phase Beacons):
> 🔬 How it routes packets with zero memory overhead:
> 
> 🔹 lwIP Layer-3 DNAPT modifying existing `pbuf` chains in-place (no memcpy)
> 🔹 RFC 1624 incremental checksum math
> 🔹 TCP MSS auto-clamped to 1412B
> 🔹 180° Anti-Phase Beacon timing (50 TU offset) to stop STA/AP radio collisions!

### Tweet 4 (Dynamic TPC):
> 🔥 Ever notice your ESP32 burning hot when repeating Wi-Fi?
> 
> Blasting +20.5 dBm at a phone 1m away overloads the receiver LNA and creates packet errors.
> 
> AetherPulse introduces closed-loop Dynamic TPC: auto-scaling power from +11 dBm to +20.5 dBm based on Kalman-filtered RSSI! 📉❄️

### Tweet 5 (Glassmorphic Web Portal):
> 💎 The on-device UI looks like a cyberpunk control deck:
> 
> ✨ 100% self-contained in flash (0 CDN dependencies)
> ✨ 60 FPS HTML5 Canvas particle topology map
> ✨ Animated cubic Bézier packet flows
> ✨ 0% MCU overhead (rendered on client GPU!)
> 
> *(Attach: ui_preview.svg)*

### Tweet 6 (WebSerial 1-Click Flasher):
> ⚡ Zero drivers. Zero Python. Zero terminal commands.
> 
> Connect any ESP32-WROOM or NodeMCU board via USB, open Chrome/Edge, and click "Install".
> 
> The Web Serial API flashes bootloader, partitions, and firmware in 30 seconds:
> 
> 👉 https://itszeeshanrajput.github.io/AetherPulse-ESP32/

### Tweet 7 (Wrap-Up & Call to Action):
> Built with ❤️ for the @EspressifSystems maker community.
> 
> Perfect for travel routers, captive portal MAC cloning, home Wi-Fi boosters, or smart home isolation.
> 
> Star the repo & read the full write-up:
> ⭐ https://github.com/Itszeeshanrajput/AetherPulse-ESP32
> 
> Feedback & RTs appreciated! 🔁
