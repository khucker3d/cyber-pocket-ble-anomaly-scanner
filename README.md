# Pocket BLE Anomaly Scanner

<img width="504" height="378" alt="IMG_3110" src="https://github.com/user-attachments/assets/0509b985-766e-4f65-8cc5-8b8a34b9ad5a" />

## About:
This project focuses on defensive wireless awareness, embedded firmware development, and portable cybersecurity tooling using Bluetooth Low Energy (BLE) scanning and lightweight UI workflows. The scanner passively observes nearby BLE advertisements and provides a handheld interface.

#### Examples:
* BLE device discovery
* RSSI proximity awareness
* device history tracking
* suspicion scoring
* trusted/untrusted device management
* advanced BLE fingerprinting
* device type inference
* serial logging export
* portable freeze/review workflows

## Features:
### BLE Monitoring:
- Passive BLE scanning
- RSSI signal visualization
- Signal bars and threat highlighting
- Nearby device awareness
- Portable freeze/review workflow

### Device Awareness:
- Device type inference
- Device history tracking
- First seen/last seen tracking
- Strongest RSSI tracking
- Seen count tracking

### Advanced BLE Classification:
* Service UUID analysis
* Appearance value analysis
* Manufacturer/vendor fingerprinting
* Beacon classification
* Apple private advertisement detection
* Private BLE heuristics

### Trusted Device System:
* Trusted device watch list
* Runtime trust/untrust management
* Device details view
* Persistent trusted-device support

### UI Features:
* Embedded TFT UI
* Selected row highlighting
* Scroll bar and page navigation
* Device details screen
* FREEZE mode for stable analysis
* Reduced flicker redraw optimization

### Logging:
* CSV-friendly serial export logging
* USB serial monitoring support

## Hardware:
* [LILYGO T-Display S3 (ESP32-S3)](https://www.amazon.com/dp/B0BRTT727Z?ref=ppx_yo2ov_dt_b_fed_asin_title&th=1)
* USB-C cable
* (Optional) USB-C battery bank

## Software Stack
* Arduino IDE
* ESP32 Arduino Framework
* TFT_eSPI
* ESP32 BLE Libraries

## Current Limitations:
* BLE only
* No Wi-Fi scanning
* No packet capture
* No BLE decryption
* No persistent storage yet
* No SD card export yet
* No battery telemetry yet
* Apple BLE privacy randomization may rotate addresses
* Some BLE devices intentionally hide names

---

## Future Improvements:
### Wireless Analysis:
* Manufacturer database lookup
* Enhanced BLE service UUID analysis
* Rotating MAC correlation
* Behavioral fingerprinting
* Signal trend graphing
* Advertisement timing analysis

---

### Storage & Export:
* SPIFFS logging
* SD card export
* Saved trusted-device database
* Full CSV export workflows

---

### Portable Features:
* Battery percentage monitoring
* Sleep/stealth mode
* Low-power operation
* Rechargeable LiPo enclosure
* Vibration alerts

---

## [How To Use:](https://github.com/khucker3d/cyber-pocket-ble-anomaly-scanner/blob/main/How%20To%3A%20Setup%20%26%20Use.md)

---

## Improvement Ideas:
* Trend graphs
* Heatmaps
* Device filtering
* Category sorting
* Wireless recon dashboard integration

---

## Educational Focus:
This project was built as a hands-on embedded cybersecurity and wireless awareness learning platform covering:
* ESP32 firmware development
* BLE fingerprinting
* Bluetooth SIG manufacturer analysis
* Embedded UI systems
* State management
* Hardware debugging
* Embedded performance optimization
* Defensive wireless reconnaissance concepts

---

## Security Notes:
* This project is intended for learning, personal security practice, and portfolio demonstration.
* The scanner passively observes BLE advertisements and does not interact with payment terminals, card readers, or protected wireless systems.
