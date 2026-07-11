/*
  Pocket BLE Anomaly Scanner
  Board: LILYGO T-Display S3 / ESP32-S3

  Purpose:
  Passive BLE awareness tool for defensive wireless monitoring.
  This sketch scans nearby BLE advertisements, displays nearby devices,
  scores simple risk indicators, tracks device history, and allows basic
  button-driven UI controls.

  Safe-use note:
  This tool does not interact with payment devices, card readers, or terminals.
  It only passively observes BLE advertisements in the local environment.

  Current features:
  - BLE scan / freeze
  - Strongest RSSI sorting
  - Device type inference
  - Device history tracking
  - First seen / last seen timestamps
  - Seen count
  - Strongest RSSI per device
  - Suspicion score
  - Trusted watch list
  - Runtime trusted / untrusted marking
  - Color-coded rows
  - Suspicious devices shown with red names
  - Selected device highlight
  - Signal bars
  - Page indicator
  - Scroll bar
  - Scan animation
  - Device details view
  - USB serial export logging

  Future hardware expansion hooks:
  - Vibration motor for silent alerts
  - SPIFFS or SD card persistent logging
  - Battery percentage, requires battery voltage wiring/support
  - Sleep / stealth mode power management
*/

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <TFT_eSPI.h>

// =========================================================
// Hardware Configuration
// =========================================================

#define TFT_BL 15

// Front buttons on many LILYGO T-Display S3 boards.
// If your buttons behave backwards, swap these values.
#define BUTTON_A 0
#define BUTTON_B 14

// Optional vibration motor pin for future use.
// Leave disabled unless you wire a motor circuit.
#define VIBRATION_PIN -1

// =========================================================
// Scanner Configuration
// =========================================================

#define MAX_DEVICES 30
#define DISPLAY_LIMIT 5
// Keep the actual BLE scan short so button input stays responsive.
// A longer refresh interval below preserves the slower update feel.
#define SCAN_TIME_SECONDS 1
#define SCAN_REFRESH_INTERVAL_MS 5000

// RSSI thresholds.
// RSSI values closer to 0 are stronger.
#define RSSI_VERY_CLOSE -40
#define RSSI_NEARBY -55
#define RSSI_STRONG -65

// Devices at or above this score are shown as suspicious.
#define ALERT_SCORE 70

// Button timing.
// A short press should be held close to 1 second.
// A long press is held longer so it does not conflict with short press.
#define SHORT_PRESS_MIN_MS 800
#define LONG_PRESS_MS 2000
#define BUTTON_DEBOUNCE_MS 300
#define SERIAL_EXPORT_INTERVAL_MS 5000

// =========================================================
// Display + BLE Globals
// =========================================================

TFT_eSPI tft = TFT_eSPI();
BLEScan* pBLEScan;

// UI state.
bool scanningEnabled = true;
bool silentMode = true;
bool alertActive = false;
bool detailsMode = false;
bool needsRedraw = true;

int currentPage = 0;
int selectedIndex = 0;
int animationFrame = 0;
unsigned long lastScanTime = 0;
unsigned long lastSerialExportTime = 0;

// Button state tracking.
unsigned long buttonAPressStart = 0;
unsigned long buttonBPressStart = 0;
unsigned long lastButtonActionTime = 0;
bool buttonAWasDown = false;
bool buttonBWasDown = false;

// =========================================================
// Trusted Device Watch List
// =========================================================

/*
  Persistent trusted devices can be added here manually.
  Use the BLE address shown on-screen or in Serial Monitor.

  Note:
  Some phones and privacy-focused BLE devices rotate addresses.
  Those may not remain trusted by address forever.
*/
String trustedDevices[] = {
  "aa:bb:cc:dd:ee:ff",
  "11:22:33:44:55:66"
};

int trustedDeviceCount = sizeof(trustedDevices) / sizeof(trustedDevices[0]);

// Runtime trusted devices are temporary until reboot.
String runtimeTrustedDevices[MAX_DEVICES];
int runtimeTrustedCount = 0;

// Runtime untrusted overrides are also temporary until reboot.
// This lets you temporarily untrust a device even if it is listed
// in the hardcoded trustedDevices[] array above.
String runtimeUntrustedDevices[MAX_DEVICES];
int runtimeUntrustedCount = 0;

// =========================================================
// Data Structures
// =========================================================

struct BLEDeviceInfo {
  String name;
  String deviceType;
  String address;
  int rssi;
  bool hasName;
  bool trusted;
  int score;
  String riskLabel;
  int seenCount;
  unsigned long firstSeen;
  unsigned long lastSeen;
  int strongestRSSI;
};

BLEDeviceInfo devices[MAX_DEVICES];
int deviceCount = 0;
int rawDeviceCount = 0;

// =========================================================
// Utility Functions
// =========================================================

String formatTime(unsigned long milliseconds) {
  unsigned long seconds = milliseconds / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long remainingSeconds = seconds % 60;

  return String(minutes) + "m" + String(remainingSeconds) + "s";
}

bool isGenericName(String name) {
  name.toLowerCase();

  if (name == "unknown") return true;
  if (name == "") return true;
  if (name.indexOf("ble") >= 0) return true;
  if (name.indexOf("bt") >= 0) return true;
  if (name.indexOf("device") >= 0) return true;
  if (name.indexOf("uart") >= 0) return true;
  if (name.indexOf("serial") >= 0) return true;

  return false;
}

bool isRuntimeUntrustedDevice(String address) {
  address.toLowerCase();

  for (int i = 0; i < runtimeUntrustedCount; i++) {
    String untrusted = runtimeUntrustedDevices[i];
    untrusted.toLowerCase();

    if (address == untrusted) {
      return true;
    }
  }

  return false;
}

void addRuntimeUntrustedDevice(String address) {
  if (runtimeUntrustedCount >= MAX_DEVICES) return;
  if (isRuntimeUntrustedDevice(address)) return;

  runtimeUntrustedDevices[runtimeUntrustedCount] = address;
  runtimeUntrustedCount++;
}

void removeRuntimeUntrustedDevice(String address) {
  address.toLowerCase();

  for (int i = 0; i < runtimeUntrustedCount; i++) {
    String untrusted = runtimeUntrustedDevices[i];
    untrusted.toLowerCase();

    if (untrusted == address) {
      for (int j = i; j < runtimeUntrustedCount - 1; j++) {
        runtimeUntrustedDevices[j] = runtimeUntrustedDevices[j + 1];
      }

      runtimeUntrustedCount--;
      return;
    }
  }
}

bool isTrustedDevice(String address) {
  address.toLowerCase();

  // Runtime untrusted overrides take priority over trusted devices.
  if (isRuntimeUntrustedDevice(address)) {
    return false;
  }

  // Check manually configured trusted devices.
  for (int i = 0; i < trustedDeviceCount; i++) {
    String trusted = trustedDevices[i];
    trusted.toLowerCase();

    if (address == trusted) {
      return true;
    }
  }

  // Check runtime trusted devices.
  for (int i = 0; i < runtimeTrustedCount; i++) {
    String trusted = runtimeTrustedDevices[i];
    trusted.toLowerCase();

    if (address == trusted) {
      return true;
    }
  }

  return false;
}

void addRuntimeTrustedDevice(String address) {
  if (runtimeTrustedCount >= MAX_DEVICES) return;
  if (isTrustedDevice(address)) return;

  runtimeTrustedDevices[runtimeTrustedCount] = address;
  runtimeTrustedCount++;
}

void removeRuntimeTrustedDevice(String address) {
  address.toLowerCase();

  for (int i = 0; i < runtimeTrustedCount; i++) {
    String trusted = runtimeTrustedDevices[i];
    trusted.toLowerCase();

    if (trusted == address) {
      for (int j = i; j < runtimeTrustedCount - 1; j++) {
        runtimeTrustedDevices[j] = runtimeTrustedDevices[j + 1];
      }

      runtimeTrustedCount--;
      return;
    }
  }
}

int calculateSignalBars(int rssi) {
  if (rssi > -45) return 5;
  if (rssi > -55) return 4;
  if (rssi > -65) return 3;
  if (rssi > -75) return 2;
  return 1;
}

int calculateSuspicionScore(bool hasName, String name, int rssi, int seenCount, bool trusted) {
  if (trusted) {
    return 0;
  }

  int score = 0;

  // Unknown or hidden names are common, but still useful as a weak signal.
  if (!hasName) {
    score += 20;
  }

  // Generic advertised names are less useful for identification.
  if (isGenericName(name)) {
    score += 15;
  }

  // Strong RSSI can mean the device is physically nearby.
  if (rssi > RSSI_VERY_CLOSE) {
    score += 40;
  } else if (rssi > RSSI_NEARBY) {
    score += 25;
  } else if (rssi > RSSI_STRONG) {
    score += 10;
  }

  // Repeated sightings suggest persistent local presence.
  if (seenCount >= 10) {
    score += 20;
  } else if (seenCount >= 5) {
    score += 10;
  }

  if (score > 100) {
    score = 100;
  }

  return score;
}

String getRiskLabel(int score, bool trusted, int rssi) {
  if (trusted) return "TRUSTED";
  if (score >= ALERT_SCORE) return "ALERT";
  if (rssi > RSSI_VERY_CLOSE) return "VERY CLOSE";
  if (score >= 45) return "WATCH";
  if (score >= 25) return "LOW";
  return "NORMAL";
}

uint16_t getRiskColor(String label) {
  if (label == "TRUSTED") return TFT_BLUE;
  if (label == "ALERT") return TFT_RED;
  if (label == "VERY CLOSE") return TFT_ORANGE;
  if (label == "WATCH") return TFT_YELLOW;
  if (label == "LOW") return TFT_CYAN;
  return TFT_GREEN;
}

// =========================================================
// Device Type Detection
// =========================================================

/*
  Attempt to infer a human-readable device type.

  BLE names are often hidden or randomized.
  This provides more useful classifications than simply "Unknown".
*/
String detectDeviceType(BLEAdvertisedDevice device) {
  String name = device.haveName()
    ? device.getName().c_str()
    : "";

  name.toLowerCase();

  // ---------------------------------------------------------
  // 1. Name-pattern detection
  // ---------------------------------------------------------
  // If a device exposes a readable name, this is usually the most useful signal.
  if (name.indexOf("iphone") >= 0) return "Phone";
  if (name.indexOf("ipad") >= 0) return "Tablet";
  if (name.indexOf("android") >= 0) return "Phone";
  if (name.indexOf("airpods") >= 0) return "Earbuds";
  if (name.indexOf("buds") >= 0) return "Earbuds";
  if (name.indexOf("headphone") >= 0) return "Headphones";
  if (name.indexOf("watch") >= 0) return "Watch";
  if (name.indexOf("fitbit") >= 0) return "Fitness";
  if (name.indexOf("garmin") >= 0) return "Fitness";
  if (name.indexOf("macbook") >= 0) return "Laptop";
  if (name.indexOf("windows") >= 0) return "Laptop";
  if (name.indexOf("keyboard") >= 0) return "Keyboard";
  if (name.indexOf("mouse") >= 0) return "Mouse";
  if (name.indexOf("logi") >= 0) return "Logitech Device";
  if (name.indexOf("esp32") >= 0) return "ESP32";
  if (name.indexOf("tile") >= 0) return "Tracker";

  // ---------------------------------------------------------
  // 2. Service UUID analysis
  // ---------------------------------------------------------
  // Some BLE devices advertise standard service UUIDs.
  // This helps identify devices even when the name is hidden.
  if (device.haveServiceUUID()) {
    String serviceUUID = device.getServiceUUID().toString().c_str();
    serviceUUID.toLowerCase();

    // Human Interface Device service.
    if (serviceUUID.indexOf("1812") >= 0) return "HID Device";

    // Heart Rate service.
    if (serviceUUID.indexOf("180d") >= 0) return "Fitness";

    // Running Speed and Cadence service.
    if (serviceUUID.indexOf("1814") >= 0) return "Fitness";

    // Cycling Speed and Cadence service.
    if (serviceUUID.indexOf("1816") >= 0) return "Fitness";

    // Eddystone beacon service UUID.
    if (serviceUUID.indexOf("feaa") >= 0) return "Eddystone Beacon";

    // Exposure Notification service, seen on some phones.
    if (serviceUUID.indexOf("fd6f") >= 0) return "Phone Beacon";

    // Battery service alone is too generic, but useful as a weak hint.
    if (serviceUUID.indexOf("180f") >= 0) return "Battery Device";
  }

  // ---------------------------------------------------------
  // 3. Appearance value analysis
  // ---------------------------------------------------------
  // Appearance values are BLE-assigned categories.
  // Not all devices advertise these, but when present they are very helpful.
  if (device.haveAppearance()) {
    uint16_t appearance = device.getAppearance();

    switch (appearance) {
      case 0x0040:
        return "Phone";

      case 0x0080:
        return "Computer";

      case 0x00C0:
        return "Watch";

      case 0x00C1:
        return "Sports Watch";

      case 0x0140:
        return "Display";

      case 0x0180:
        return "Remote";

      case 0x03C0:
        return "HID Device";

      case 0x03C1:
        return "Keyboard";

      case 0x03C2:
        return "Mouse";

      case 0x0340:
        return "Heart Rate Sensor";

      case 0x0440:
        return "Glucose Meter";

      case 0x0480:
        return "Sensor";

      default:
        break;
    }
  }

  // ---------------------------------------------------------
  // 4. Manufacturer data + vendor fingerprinting
  // ---------------------------------------------------------
  // BLE manufacturer data often starts with a Bluetooth SIG company ID.
  // The ID is little-endian in the advertisement payload.
  if (device.haveManufacturerData()) {
    String mfg = device.getManufacturerData();

    if (mfg.length() > 1) {
      uint8_t company1 = (uint8_t)mfg[0];
      uint8_t company2 = (uint8_t)mfg[1];
      uint16_t companyID = (company2 << 8) | company1;

      // Beacon fingerprints first, because they are more specific than vendor name.
      if (companyID == 0x004C && mfg.length() > 3) {
        uint8_t appleType = (uint8_t)mfg[2];
        uint8_t appleLength = (uint8_t)mfg[3];

        // iBeacon prefix is commonly Apple company ID + 0x02 + 0x15.
        if (appleType == 0x02 && appleLength == 0x15) {
          return "iBeacon";
        }

        // Apple Continuity / nearby ecosystem advertisements.
        // These are often private or rotating and may not expose a real name.
        return "Apple Private";
      }

      // AltBeacon-style payload often contains 0xBEAC after the company ID.
      if (mfg.length() > 3) {
        uint8_t b2 = (uint8_t)mfg[2];
        uint8_t b3 = (uint8_t)mfg[3];

        if (b2 == 0xBE && b3 == 0xAC) {
          return "AltBeacon";
        }
      }

      switch (companyID) {
        case 0x004C:
          return "Apple Device";

        case 0x0006:
          return "Microsoft Device";

        case 0x0075:
          return "Samsung Device";

        case 0x00E0:
          return "Google Device";

        case 0x0002:
          return "Intel Device";

        case 0x046D:
          return "Logitech Device";

        case 0x0087:
          return "Garmin Device";

        case 0x00AD:
          return "Fitbit Device";

        case 0x012D:
          return "Sony Device";

        case 0x009E:
          return "Bose Device";

        case 0x00C7:
          return "Tile Tracker";

        case 0x02E5:
          return "ESP32 Device";

        default:
          return "Unknown Vendor";
      }
    }
  }

  // ---------------------------------------------------------
  // 5. Behavioral / privacy heuristic
  // ---------------------------------------------------------
  // Many modern devices use random/private addresses and no name.
  // This does not identify the exact device, but it gives a more useful label.
  if (!device.haveName()) {
    return "Private BLE";
  }

  return "Unknown";
}

// =========================================================
// Device History Management
// =========================================================

int findDeviceIndexByAddress(String address) {
  for (int i = 0; i < deviceCount; i++) {
    if (devices[i].address == address) {
      return i;
    }
  }

  return -1;
}

void updateDeviceRecord(BLEAdvertisedDevice device, String name, String address, int rssi, bool hasName) {
  int index = findDeviceIndexByAddress(address);
  unsigned long now = millis();

  if (index >= 0) {
    devices[index].name = name;
    devices[index].deviceType = detectDeviceType(device);
    devices[index].rssi = rssi;
    devices[index].hasName = hasName;
    devices[index].trusted = isTrustedDevice(address);
    devices[index].seenCount++;
    devices[index].lastSeen = now;

    if (rssi > devices[index].strongestRSSI) {
      devices[index].strongestRSSI = rssi;
    }

    devices[index].score = calculateSuspicionScore(
      devices[index].hasName,
      devices[index].name,
      devices[index].rssi,
      devices[index].seenCount,
      devices[index].trusted
    );

    devices[index].riskLabel = getRiskLabel(
      devices[index].score,
      devices[index].trusted,
      devices[index].rssi
    );

    return;
  }

  if (deviceCount < MAX_DEVICES) {
    devices[deviceCount].name = name;
    devices[deviceCount].deviceType = detectDeviceType(device);
    devices[deviceCount].address = address;
    devices[deviceCount].rssi = rssi;
    devices[deviceCount].hasName = hasName;
    devices[deviceCount].trusted = isTrustedDevice(address);
    devices[deviceCount].seenCount = 1;
    devices[deviceCount].firstSeen = now;
    devices[deviceCount].lastSeen = now;
    devices[deviceCount].strongestRSSI = rssi;

    devices[deviceCount].score = calculateSuspicionScore(
      devices[deviceCount].hasName,
      devices[deviceCount].name,
      devices[deviceCount].rssi,
      devices[deviceCount].seenCount,
      devices[deviceCount].trusted
    );

    devices[deviceCount].riskLabel = getRiskLabel(
      devices[deviceCount].score,
      devices[deviceCount].trusted,
      devices[deviceCount].rssi
    );

    deviceCount++;
  }
}

void clearDeviceHistory() {
  deviceCount = 0;
  rawDeviceCount = 0;
  currentPage = 0;
  selectedIndex = 0;
  needsRedraw = true;
}

// =========================================================
// Sorting
// =========================================================

void sortDevicesByScoreThenRSSI() {
  for (int i = 0; i < deviceCount - 1; i++) {
    for (int j = i + 1; j < deviceCount; j++) {
      bool shouldSwap = false;

      if (devices[j].score > devices[i].score) {
        shouldSwap = true;
      } else if (devices[j].score == devices[i].score && devices[j].rssi > devices[i].rssi) {
        shouldSwap = true;
      }

      if (shouldSwap) {
        BLEDeviceInfo temp = devices[i];
        devices[i] = devices[j];
        devices[j] = temp;
      }
    }
  }
}

// =========================================================
// Drawing Helpers
// =========================================================

void drawSignalBars(int x, int y, int bars, uint16_t color) {
  for (int i = 0; i < 5; i++) {
    int barHeight = 3 + (i * 3);
    int barX = x + (i * 5);
    int barY = y + (15 - barHeight);

    if (i < bars) {
      tft.fillRect(barX, barY, 3, barHeight, color);
    } else {
      tft.drawRect(barX, barY, 3, barHeight, TFT_DARKGREY);
    }
  }
}

void drawScrollBar(int totalItems, int currentPageValue) {
  if (totalItems <= DISPLAY_LIMIT) {
    return;
  }

  int maxPages = (totalItems + DISPLAY_LIMIT - 1) / DISPLAY_LIMIT;
  int barX = 314;
  int barY = 68;
  int barHeight = 160;
  int thumbHeight = max(12, barHeight / maxPages);
  int thumbY = barY + ((barHeight - thumbHeight) * currentPageValue / max(1, maxPages - 1));

  tft.drawRect(barX, barY, 4, barHeight, TFT_DARKGREY);
  tft.fillRect(barX, thumbY, 4, thumbHeight, TFT_CYAN);
}

void drawHeader() {
  tft.fillRect(0, 0, 320, 64, TFT_BLACK);

  String spinner[] = {"|", "/", "-", "\\"};
  String scanIcon = scanningEnabled ? spinner[animationFrame % 4] : "F";

  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("BLE Scanner", 8, 8);

  if (scanningEnabled) {
    tft.fillRoundRect(210, 6, 100, 22, 4, TFT_DARKGREEN);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.drawString("SCAN", 220, 9);
  } else {
    tft.fillRoundRect(210, 6, 100, 22, 4, TFT_ORANGE);
    tft.setTextColor(TFT_BLACK, TFT_ORANGE);
    tft.drawString("FREEZE", 218, 9);
  }

  tft.setTextSize(1);
  tft.setTextColor(scanningEnabled ? TFT_GREEN : TFT_BLACK, scanningEnabled ? TFT_BLACK : TFT_ORANGE);
  tft.drawString(scanIcon, 292, 15);

  tft.setTextColor(silentMode ? TFT_CYAN : TFT_YELLOW, TFT_BLACK);
  tft.drawString(silentMode ? "SILENT" : "ALERTS", 220, 32);

  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("Seen: " + String(deviceCount), 8, 38);
  tft.drawString("Raw: " + String(rawDeviceCount), 80, 38);

  int maxPages = max(1, (deviceCount + DISPLAY_LIMIT - 1) / DISPLAY_LIMIT);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Page " + String(currentPage + 1) + "/" + String(maxPages), 220, 48);

  tft.drawLine(0, 62, 320, 62, TFT_DARKGREY);
}

void drawDeviceRow(BLEDeviceInfo device, int row, bool selected) {
  int y = 68 + (row * 34);
  uint16_t riskColor = getRiskColor(device.riskLabel);
  uint16_t rowBackground = selected ? TFT_NAVY : TFT_BLACK;

  if (selected) {
    tft.fillRoundRect(0, y - 3, 312, 33, 4, rowBackground);
    tft.drawRoundRect(0, y - 3, 312, 33, 4, TFT_CYAN);
    tft.setTextColor(TFT_CYAN, rowBackground);
    tft.drawString(">", 4, y + 8);
  }

  tft.fillRect(selected ? 14 : 2, y, 4, 28, riskColor);

  int textX = selected ? 22 : 10;

  if (!device.trusted && device.score >= ALERT_SCORE) {
    tft.setTextColor(TFT_RED, rowBackground);
  } else if (device.trusted) {
    tft.setTextColor(TFT_BLUE, rowBackground);
  } else {
    tft.setTextColor(TFT_WHITE, rowBackground);
  }

  tft.drawString(device.name.substring(0, 18), textX, y);

  tft.setTextColor(TFT_DARKGREY, rowBackground);
  tft.drawString(device.address, textX, y + 11);

  tft.setTextColor(TFT_CYAN, rowBackground);
  tft.drawString(device.deviceType.substring(0, 18), textX, y + 22);

  tft.setTextColor(TFT_YELLOW, rowBackground);
  tft.drawString("R:" + String(device.rssi), 172, y);

  drawSignalBars(220, y + 2, calculateSignalBars(device.rssi), riskColor);

  tft.setTextColor(riskColor, rowBackground);
  tft.drawString(device.riskLabel, 250, y);

  tft.setTextColor(TFT_CYAN, rowBackground);
  tft.drawString("S:" + String(device.score), 172, y + 12);

  tft.setTextColor(TFT_LIGHTGREY, rowBackground);
  tft.drawString("Seen:" + String(device.seenCount), 220, y + 12);
}

void drawMainScreen() {
  tft.fillScreen(TFT_BLACK);
  drawHeader();

  int maxPages = max(1, (deviceCount + DISPLAY_LIMIT - 1) / DISPLAY_LIMIT);

  if (currentPage >= maxPages) {
    currentPage = maxPages - 1;
  }

  int startIndex = currentPage * DISPLAY_LIMIT;
  int endIndex = min(startIndex + DISPLAY_LIMIT, deviceCount);
  int row = 0;

  if (deviceCount == 0) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("No devices yet", 70, 110);

    if (!scanningEnabled) {
      tft.setTextSize(1);
      tft.setTextColor(TFT_ORANGE, TFT_BLACK);
      tft.drawString("Scan is frozen", 110, 140);
    }

    return;
  }

  if (!scanningEnabled) {
    tft.fillRoundRect(85, 64, 150, 16, 3, TFT_ORANGE);
    tft.setTextSize(1);
    tft.setTextColor(TFT_BLACK, TFT_ORANGE);
    tft.drawString("SCAN FROZEN", 112, 68);
  }

  for (int i = startIndex; i < endIndex; i++) {
    drawDeviceRow(devices[i], row, i == selectedIndex);
    row++;
  }

  drawScrollBar(deviceCount, currentPage);
}

/*
  Full-screen alert mode.

  Currently disabled in favor of inline UI highlighting.
  Keeping this function for future optional use.
*/
void drawAlertScreen() {
  tft.fillScreen(TFT_RED);
  tft.setTextColor(TFT_WHITE, TFT_RED);
  tft.setTextSize(3);
  tft.drawString("ALERT", 105, 35);

  tft.setTextSize(2);
  tft.drawString("Suspicious BLE", 55, 85);
  tft.drawString("Device Nearby", 65, 112);

  if (deviceCount > 0) {
    tft.setTextSize(1);
    tft.drawString(devices[0].name.substring(0, 24), 20, 155);
    tft.drawString(devices[0].address, 20, 170);
    tft.drawString("RSSI: " + String(devices[0].rssi), 20, 185);
    tft.drawString("Score: " + String(devices[0].score), 120, 185);
  }

  tft.setTextSize(1);
  tft.drawString("Press button to return", 80, 220);
}

void drawDeviceDetailsScreen() {
  tft.fillScreen(TFT_BLACK);

  if (deviceCount == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.drawString("No device", 95, 100);
    return;
  }

  BLEDeviceInfo device = devices[selectedIndex];
  uint16_t riskColor = getRiskColor(device.riskLabel);

  tft.setTextSize(2);
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Device Details", 10, 8);

  tft.drawLine(0, 34, 320, 34, TFT_DARKGREY);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);

  tft.drawString("Name:", 10, 45);
  tft.drawString(device.name.substring(0, 28), 80, 45);

  tft.drawString("Type:", 10, 65);
  tft.drawString(device.deviceType.substring(0, 24), 80, 65);

  tft.drawString("Address:", 10, 85);
  tft.drawString(device.address, 80, 85);

  tft.drawString("RSSI:", 10, 105);
  tft.drawString(String(device.rssi), 80, 105);

  tft.drawString("Strongest:", 10, 125);
  tft.drawString(String(device.strongestRSSI), 80, 125);

  tft.drawString("Seen:", 10, 145);
  tft.drawString(String(device.seenCount), 80, 145);

  tft.drawString("First seen:", 10, 165);
  tft.drawString(formatTime(device.firstSeen), 100, 165);

  tft.drawString("Last seen:", 10, 185);
  tft.drawString(formatTime(device.lastSeen), 100, 185);

  tft.drawString("Trusted:", 10, 205);
  tft.drawString(device.trusted ? "YES" : "NO", 100, 205);

  tft.drawString("Score:", 150, 205);
  tft.drawString(String(device.score), 205, 205);

  tft.setTextColor(riskColor, TFT_BLACK);
  tft.drawString("Risk: " + device.riskLabel, 10, 220);

  drawSignalBars(255, 102, calculateSignalBars(device.rssi), riskColor);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("A: back  B: next  B hold: trust", 35, 232);
}

// =========================================================
// Logging
// =========================================================

void exportSerialLog() {
  Serial.println("================ BLE SCAN EXPORT ================");
  Serial.println("Name,Type,Address,RSSI,StrongestRSSI,SeenCount,Score,Risk,FirstSeen,LastSeen,Trusted");

  for (int i = 0; i < deviceCount; i++) {
    Serial.print(devices[i].name);
    Serial.print(",");
    Serial.print(devices[i].deviceType);
    Serial.print(",");
    Serial.print(devices[i].address);
    Serial.print(",");
    Serial.print(devices[i].rssi);
    Serial.print(",");
    Serial.print(devices[i].strongestRSSI);
    Serial.print(",");
    Serial.print(devices[i].seenCount);
    Serial.print(",");
    Serial.print(devices[i].score);
    Serial.print(",");
    Serial.print(devices[i].riskLabel);
    Serial.print(",");
    Serial.print(formatTime(devices[i].firstSeen));
    Serial.print(",");
    Serial.print(formatTime(devices[i].lastSeen));
    Serial.print(",");
    Serial.println(devices[i].trusted ? "yes" : "no");
  }

  Serial.println("=================================================");
}

// =========================================================
// BLE Scanning
// =========================================================

void scanForDevices() {
  BLEScanResults* results = pBLEScan->start(SCAN_TIME_SECONDS, false);
  rawDeviceCount = results->getCount();

  for (int i = 0; i < rawDeviceCount; i++) {
    BLEAdvertisedDevice device = results->getDevice(i);

    bool hasName = device.haveName();
    String name = hasName ? device.getName().c_str() : "Unknown";
    String address = device.getAddress().toString().c_str();
    int rssi = device.getRSSI();

    updateDeviceRecord(device, name, address, rssi, hasName);
  }

  sortDevicesByScoreThenRSSI();

  // Full-screen alert mode is disabled.
  // Suspicious devices are highlighted inline by drawDeviceRow().
  alertActive = false;

  pBLEScan->clearResults();
  animationFrame++;
}

// =========================================================
// Button Controls
// =========================================================

void selectNextDevice() {
  if (deviceCount == 0) return;

  selectedIndex++;

  if (selectedIndex >= deviceCount) {
    selectedIndex = 0;
  }

  currentPage = selectedIndex / DISPLAY_LIMIT;
  needsRedraw = true;
}

void goToNextPage() {
  if (deviceCount == 0) return;

  int maxPages = max(1, (deviceCount + DISPLAY_LIMIT - 1) / DISPLAY_LIMIT);

  currentPage++;

  if (currentPage >= maxPages) {
    currentPage = 0;
  }

  selectedIndex = currentPage * DISPLAY_LIMIT;

  if (selectedIndex >= deviceCount) {
    selectedIndex = deviceCount - 1;
  }

  needsRedraw = true;
}

void goToPreviousPage() {
  if (deviceCount == 0) return;

  int maxPages = max(1, (deviceCount + DISPLAY_LIMIT - 1) / DISPLAY_LIMIT);

  currentPage--;

  if (currentPage < 0) {
    currentPage = maxPages - 1;
  }

  selectedIndex = currentPage * DISPLAY_LIMIT;

  if (selectedIndex >= deviceCount) {
    selectedIndex = deviceCount - 1;
  }

  needsRedraw = true;
}

void refreshTrustedRiskState() {
  for (int i = 0; i < deviceCount; i++) {
    devices[i].trusted = isTrustedDevice(devices[i].address);
    devices[i].score = calculateSuspicionScore(
      devices[i].hasName,
      devices[i].name,
      devices[i].rssi,
      devices[i].seenCount,
      devices[i].trusted
    );
    devices[i].riskLabel = getRiskLabel(devices[i].score, devices[i].trusted, devices[i].rssi);
  }

  needsRedraw = true;
}

void toggleTrustedForSelectedDevice() {
  if (deviceCount == 0) return;

  String address = devices[selectedIndex].address;

  if (isTrustedDevice(address)) {
    removeRuntimeTrustedDevice(address);
    addRuntimeUntrustedDevice(address);
    Serial.println("UNTRUSTED: " + address);
  } else {
    removeRuntimeUntrustedDevice(address);
    addRuntimeTrustedDevice(address);
    Serial.println("TRUSTED: " + address);
  }

  refreshTrustedRiskState();
}

void handleButtonActions() {
  bool buttonADown = digitalRead(BUTTON_A) == LOW;
  bool buttonBDown = digitalRead(BUTTON_B) == LOW;
  unsigned long now = millis();

  if (now - lastButtonActionTime < BUTTON_DEBOUNCE_MS) {
    return;
  }

  // A + B together: open / close Device Details view.
  if (buttonADown && buttonBDown) {
    detailsMode = !detailsMode;
    needsRedraw = true;
    lastButtonActionTime = now;

    // Reset individual button state so the combo does not also trigger A or B.
    buttonAWasDown = false;
    buttonBWasDown = false;

    Serial.println(detailsMode ? "DETAILS MODE ON" : "DETAILS MODE OFF");
    return;
  }

  // Button A behavior.
  // Press once to immediately freeze / resume scan.
  // In details mode, press once to go back.
  // Holding A for LONG_PRESS_MS clears history.
  if (buttonADown && !buttonAWasDown) {
    buttonAPressStart = now;
    buttonAWasDown = true;

    if (detailsMode) {
      detailsMode = false;
      Serial.println("A PRESS: BACK");
    } else {
      scanningEnabled = !scanningEnabled;
      Serial.println(scanningEnabled ? "A PRESS: SCAN ON" : "A PRESS: SCAN FROZEN");
    }

    needsRedraw = true;
    lastButtonActionTime = now;
    return;
  }

  if (buttonADown && buttonAWasDown) {
    unsigned long heldTime = now - buttonAPressStart;

    if (heldTime >= LONG_PRESS_MS) {
      clearDeviceHistory();
      detailsMode = false;
      buttonAWasDown = false;
      lastButtonActionTime = now;
      needsRedraw = true;
      Serial.println("A LONG: CLEAR HISTORY");
      return;
    }
  }

  if (!buttonADown && buttonAWasDown) {
    buttonAWasDown = false;
    return;
  }

  // Button B behavior.
  // Press once for next page / next device.
  // Hold B for LONG_PRESS_MS to trust / untrust selected device.
  if (buttonBDown && !buttonBWasDown) {
    buttonBPressStart = now;
    buttonBWasDown = true;

    if (detailsMode) {
      selectNextDevice();
      Serial.println("B PRESS: NEXT DEVICE");
    } else {
      goToNextPage();
      Serial.println("B PRESS: NEXT PAGE");
    }

    needsRedraw = true;
    lastButtonActionTime = now;
    return;
  }

  if (buttonBDown && buttonBWasDown) {
    unsigned long heldTime = now - buttonBPressStart;

    if (heldTime >= LONG_PRESS_MS) {
      toggleTrustedForSelectedDevice();
      buttonBWasDown = false;
      lastButtonActionTime = now;
      Serial.println("B LONG: TRUST / UNTRUST");
      return;
    }
  }

  if (!buttonBDown && buttonBWasDown) {
    buttonBWasDown = false;
    return;
  }
}

// =========================================================
// Arduino Setup + Loop
// =========================================================

void setup() {
  Serial.begin(115200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT_PULLUP);

  if (VIBRATION_PIN >= 0) {
    pinMode(VIBRATION_PIN, OUTPUT);
    digitalWrite(VIBRATION_PIN, LOW);
  }

  tft.init();

  // Use 1 or 3 depending on your preferred screen orientation.
  tft.setRotation(3);
  tft.fillScreen(TFT_BLACK);

  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawString("Pocket BLE", 20, 35);
  tft.drawString("Anomaly Scanner", 20, 65);

  tft.setTextSize(1);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString("Passive BLE monitoring", 20, 110);
  tft.drawString("A press: freeze/back", 20, 135);
  tft.drawString("A long: clear", 20, 150);
  tft.drawString("B press: next page", 20, 165);
  tft.drawString("B long: trust/untrust", 20, 180);
  tft.drawString("A+B: details view", 20, 195);

  BLEDevice::init("");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setActiveScan(true);

  delay(1800);
  needsRedraw = true;
}

void loop() {
  handleButtonActions();

  // Only scan when not frozen.
  // The BLE scan itself is intentionally short because BLE scanning blocks input.
  // The refresh timer gives us a slower update rate without making buttons feel broken.
  if (scanningEnabled && millis() - lastScanTime >= SCAN_REFRESH_INTERVAL_MS) {
    scanForDevices();
    lastScanTime = millis();
    needsRedraw = true;
  }

  // Only redraw when something changes.
  // This reduces flicker while the scanner is frozen.
  if (needsRedraw) {
    if (detailsMode) {
      drawDeviceDetailsScreen();
    } else {
      drawMainScreen();
    }

    needsRedraw = false;
  }

  // Export log periodically to USB serial.
  // This is useful for copying into a CSV later.
  // Throttled so it does not make button input feel laggy.
  if (millis() - lastSerialExportTime >= SERIAL_EXPORT_INTERVAL_MS) {
    exportSerialLog();
    lastSerialExportTime = millis();
  }

  // Screen-only silent mode is currently always safe.
  // Future: add vibration here when VIBRATION_PIN is wired.
  if (!silentMode && alertActive && VIBRATION_PIN >= 0) {
    digitalWrite(VIBRATION_PIN, HIGH);
    delay(120);
    digitalWrite(VIBRATION_PIN, LOW);
  }

  delay(100);
}
