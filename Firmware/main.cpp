#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <esp_system.h>

#define ENABLE_BLE_SCAN 1

#if ENABLE_BLE_SCAN
#include <BLEDevice.h>
#endif

#define I2C_SDA 5
#define I2C_SCL 6

#define Encoder_Button_S1 1
#define Encoder_A 2
#define Encoder_B 3

#define SW2 43
#define SW3 44

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR_1 0x3C
#define OLED_ADDR_2 0x3D

// Optional battery/ADC meter. Leave disabled unless you wire a safe divider.
#define BATTERY_ADC_PIN -1
#define BATTERY_DIVIDER_RATIO 2.0f

const unsigned long DEBOUNCE_MS = 35;
const unsigned long LONG_PRESS_MS = 650;
const int MENU_VISIBLE_ROWS = 4;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

struct ButtonState {
  uint8_t pin;
  bool reading;
  bool stable;
  unsigned long lastEdgeMs;
  unsigned long pressedAtMs;
  bool shortPress;
  bool longPress;
  bool longSent;
};

enum InputAction {
  ACT_NONE,
  ACT_UP,
  ACT_DOWN,
  ACT_SELECT,
  ACT_BACK
};

struct BleResult {
  String name;
  String address;
  int rssi;
};

void runWiFiScan();
void runWiFiAnalyzer();
void runBleScan();
void runSoftAPDemo();
void showSysInfo();
void runI2CScan();
void runGpioMonitor();
void runDisplayTest();
void runDeepSleep();
void showAbout();

typedef void (*MenuHandler)();

struct MenuItem {
  const char *label;
  MenuHandler handler;
};

MenuItem menuItems[] = {
  {"WiFi Scan", runWiFiScan},
  {"WiFi Analyzer", runWiFiAnalyzer},
  {"BLE Scan", runBleScan},
  {"SoftAP Demo", runSoftAPDemo},
  {"Sys Info", showSysInfo},
  {"I2C Scanner", runI2CScan},
  {"GPIO Monitor", runGpioMonitor},
  {"Display Test", runDisplayTest},
  {"Sleep 10s", runDeepSleep},
  {"About / Help", showAbout}
};

const int NUM_MENU_ITEMS = sizeof(menuItems) / sizeof(menuItems[0]);

ButtonState btnSelect = {SW2, HIGH, HIGH, 0, 0, false, false, false};
ButtonState btnNext = {SW3, HIGH, HIGH, 0, 0, false, false, false};
ButtonState btnEncoder = {Encoder_Button_S1, HIGH, HIGH, 0, 0, false, false, false};

int menuIndex = 0;
int menuTop = 0;
uint8_t lastEncoderState = 0;
int8_t encoderAccumulator = 0;
bool encoderSeen = false;

const int MAX_BLE_RESULTS = 12;
BleResult bleResults[MAX_BLE_RESULTS];
int bleResultCount = 0;

int clampInt(int value, int low, int high) {
  if (value < low) return low;
  if (value > high) return high;
  return value;
}

String trimText(String text, uint8_t maxChars) {
  if (text.length() <= maxChars) return text;
  if (maxChars <= 1) return "~";
  return text.substring(0, maxChars - 1) + "~";
}

void printTrimmed(String text, int x, int y, uint8_t maxChars) {
  display.setCursor(x, y);
  display.print(trimText(text, maxChars));
}

void drawHeader(String title) {
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_BLACK);
  printTrimmed(title, 2, 2, 16);
  display.setCursor(106, 2);
  display.print(encoderSeen ? "ENC" : "BTN");
  display.setTextColor(SSD1306_WHITE);
}

void drawFooter(String left, String right) {
  display.drawFastHLine(0, 54, SCREEN_WIDTH, SSD1306_WHITE);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 56);
  display.print(trimText(left, 13));
  if (right.length() > 0) {
    int x = SCREEN_WIDTH - (right.length() * 6);
    if (x < 58) x = 58;
    display.setCursor(x, 56);
    display.print(trimText(right, 11));
  }
}

void centerText(String text, int y, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  display.setTextSize(size);
  display.getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
  display.setTextSize(1);
}

void drawScrollBar(int topIndex, int total, int visible, int y, int h) {
  if (total <= visible) return;
  int thumbH = max(6, (h * visible) / total);
  int range = total - visible;
  int thumbY = y + ((h - thumbH) * topIndex) / range;
  display.drawFastVLine(126, y, h, SSD1306_WHITE);
  display.fillRect(124, thumbY, 4, thumbH, SSD1306_WHITE);
}

void showBusy(String title, String line1, String line2 = "") {
  display.clearDisplay();
  drawHeader(title);
  display.setCursor(0, 22);
  display.print(line1);
  if (line2.length() > 0) {
    display.setCursor(0, 34);
    display.print(line2);
  }
  display.display();
}

void adjustTopForSelection(int selected, int &top, int total, int visible) {
  if (selected < top) top = selected;
  if (selected >= top + visible) top = selected - visible + 1;
  top = clampInt(top, 0, max(0, total - visible));
}

void initButton(ButtonState &button) {
  bool value = digitalRead(button.pin);
  button.reading = value;
  button.stable = value;
  button.lastEdgeMs = millis();
  button.pressedAtMs = 0;
  button.shortPress = false;
  button.longPress = false;
  button.longSent = false;
}

void updateButton(ButtonState &button) {
  bool value = digitalRead(button.pin);
  unsigned long now = millis();

  if (value != button.reading) {
    button.reading = value;
    button.lastEdgeMs = now;
  }

  if ((now - button.lastEdgeMs) > DEBOUNCE_MS && value != button.stable) {
    button.stable = value;
    if (button.stable == LOW) {
      button.pressedAtMs = now;
      button.longSent = false;
    } else {
      if (!button.longSent) button.shortPress = true;
    }
  }

  if (button.stable == LOW && !button.longSent &&
      (now - button.pressedAtMs) > LONG_PRESS_MS) {
    button.longPress = true;
    button.longSent = true;
  }
}

bool takeShort(ButtonState &button) {
  if (!button.shortPress) return false;
  button.shortPress = false;
  return true;
}

bool takeLong(ButtonState &button) {
  if (!button.longPress) return false;
  button.longPress = false;
  return true;
}

void clearButtonEvents(ButtonState &button) {
  button.shortPress = false;
  button.longPress = false;
}

void pollControls() {
  updateButton(btnSelect);
  updateButton(btnNext);
  updateButton(btnEncoder);
}

int8_t readEncoderStep() {
  static const int8_t table[16] = {
    0, -1, 1, 0,
    1, 0, 0, -1,
    -1, 0, 0, 1,
    0, 1, -1, 0
  };

  uint8_t current = (digitalRead(Encoder_A) << 1) | digitalRead(Encoder_B);
  if (current == lastEncoderState) return 0;

  uint8_t transition = (lastEncoderState << 2) | current;
  lastEncoderState = current;
  encoderAccumulator += table[transition];

  if (encoderAccumulator >= 4) {
    encoderAccumulator = 0;
    encoderSeen = true;
    return 1;
  }
  if (encoderAccumulator <= -4) {
    encoderAccumulator = 0;
    encoderSeen = true;
    return -1;
  }
  return 0;
}

InputAction readInputAction(bool buttonScroll = true) {
  pollControls();

  int8_t encoderStep = readEncoderStep();
  if (encoderStep > 0) return ACT_DOWN;
  if (encoderStep < 0) return ACT_UP;

  if (takeShort(btnEncoder) || takeShort(btnSelect)) return ACT_SELECT;
  if (takeLong(btnEncoder) || takeLong(btnSelect)) return ACT_BACK;

  if (buttonScroll) {
    if (takeShort(btnNext)) return ACT_DOWN;
    if (takeLong(btnNext)) return ACT_UP;
  } else {
    if (takeShort(btnNext) || takeLong(btnNext)) return ACT_BACK;
  }

  return ACT_NONE;
}

void showBootScreen() {
  display.clearDisplay();
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawRect(2, 2, SCREEN_WIDTH - 4, SCREEN_HEIGHT - 4, SSD1306_WHITE);

  centerText("XIAO", 12, 2);
  centerText("TOOLKIT", 31, 2);
  display.setTextSize(1);
  display.setCursor(18, 52);
  display.print("OLED 0.96 / ESP32S3");
  display.display();
  delay(1200);
}

void drawMenu() {
  adjustTopForSelection(menuIndex, menuTop, NUM_MENU_ITEMS, MENU_VISIBLE_ROWS);

  display.clearDisplay();
  drawHeader("XIAO Tools");

  for (int row = 0; row < MENU_VISIBLE_ROWS; row++) {
    int item = menuTop + row;
    int y = 15 + row * 10;
    if (item >= NUM_MENU_ITEMS) break;

    bool selected = item == menuIndex;
    if (selected) {
      display.fillRect(0, y - 1, 123, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(2, y);
    display.print(selected ? ">" : " ");
    printTrimmed(menuItems[item].label, 12, y, 17);
  }

  display.setTextColor(SSD1306_WHITE);
  drawScrollBar(menuTop, NUM_MENU_ITEMS, MENU_VISIBLE_ROWS, 14, 38);
  drawFooter("SW3 move", "SW2 open");
  display.display();
}

String authModeToString(wifi_auth_mode_t type) {
  switch (type) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/3";
    default: return "SEC";
  }
}

String wifiNameAt(int index) {
  String ssid = WiFi.SSID(index);
  if (ssid.length() == 0) ssid = "(hidden)";
  return ssid;
}

void drawWifiList(int count, int selected, int top) {
  display.clearDisplay();
  drawHeader(String("WiFi ") + count);

  if (count <= 0) {
    display.setCursor(0, 28);
    display.print("No networks found.");
    drawFooter("SW2 rescan", "hold back");
    display.display();
    return;
  }

  int visible = min(MENU_VISIBLE_ROWS, count);
  for (int row = 0; row < visible; row++) {
    int item = top + row;
    int y = 15 + row * 10;
    bool isSelected = item == selected;

    if (isSelected) {
      display.fillRect(0, y - 1, 123, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    printTrimmed(wifiNameAt(item), 1, y, 12);
    display.setCursor(78, y);
    display.print(WiFi.RSSI(item));
    display.setCursor(106, y);
    display.print("ch");
    display.print(WiFi.channel(item));
  }

  display.setTextColor(SSD1306_WHITE);
  drawScrollBar(top, count, MENU_VISIBLE_ROWS, 14, 38);
  drawFooter("SW2 detail", "hold back");
  display.display();
}

void showWifiDetail(int index) {
  while (true) {
    display.clearDisplay();
    drawHeader("WiFi Detail");
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 15);
    display.print("SSID ");
    printTrimmed(wifiNameAt(index), 30, 15, 16);

    display.setCursor(0, 25);
    display.print("RSSI ");
    display.print(WiFi.RSSI(index));
    display.print(" dBm");

    display.setCursor(0, 35);
    display.print("CH ");
    display.print(WiFi.channel(index));
    display.print("  ");
    display.print(authModeToString(WiFi.encryptionType(index)));

    display.setCursor(0, 45);
    display.print(WiFi.BSSIDstr(index));

    drawFooter("SW2 back", "");
    display.display();

    InputAction action = readInputAction(false);
    if (action == ACT_SELECT || action == ACT_BACK) return;
    delay(20);
  }
}

void runWiFiScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  while (true) {
    showBusy("WiFi Scan", "Scanning 2.4 GHz...");
    int count = WiFi.scanNetworks(false, true);
    int selected = 0;
    int top = 0;

    while (true) {
      if (count > 0) {
        selected = clampInt(selected, 0, count - 1);
        adjustTopForSelection(selected, top, count, MENU_VISIBLE_ROWS);
      }
      drawWifiList(count, selected, top);

      InputAction action = readInputAction(true);
      if (action == ACT_DOWN && count > 0) selected = (selected + 1) % count;
      if (action == ACT_UP && count > 0) selected = (selected + count - 1) % count;
      if (action == ACT_SELECT) {
        if (count > 0) showWifiDetail(selected);
        else break;
      }
      if (action == ACT_BACK) {
        WiFi.scanDelete();
        return;
      }
      delay(20);
    }

    WiFi.scanDelete();
  }
}

void drawChannelAnalyzer(int counts[15], int totalNetworks, int maxCount) {
  display.clearDisplay();
  drawHeader("WiFi Analyzer");

  display.setCursor(0, 14);
  display.print("APs ");
  display.print(totalNetworks);
  display.print("  max ");
  display.print(maxCount);

  int baseY = 51;
  display.drawFastHLine(0, baseY, 122, SSD1306_WHITE);
  for (int ch = 1; ch <= 13; ch++) {
    int x = 2 + (ch - 1) * 9;
    int h = (maxCount == 0) ? 0 : map(counts[ch], 0, maxCount, 0, 27);
    display.fillRect(x, baseY - h, 6, h, SSD1306_WHITE);
    if (ch % 2 == 1) {
      display.setCursor(x, 43);
      display.print(ch);
    }
  }

  drawFooter("SW2 rescan", "hold back");
  display.display();
}

void scanChannels(int counts[15], int &totalNetworks, int &maxCount) {
  for (int i = 0; i < 15; i++) counts[i] = 0;

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  totalNetworks = WiFi.scanNetworks(false, true);
  maxCount = 0;
  for (int i = 0; i < totalNetworks; i++) {
    int ch = WiFi.channel(i);
    if (ch >= 1 && ch <= 14) {
      counts[ch]++;
      if (counts[ch] > maxCount) maxCount = counts[ch];
    }
  }
  WiFi.scanDelete();
}

void runWiFiAnalyzer() {
  int counts[15];
  int totalNetworks = 0;
  int maxCount = 0;

  while (true) {
    showBusy("WiFi Analyzer", "Scanning channels...");
    scanChannels(counts, totalNetworks, maxCount);

    while (true) {
      drawChannelAnalyzer(counts, totalNetworks, maxCount);
      InputAction action = readInputAction(false);
      if (action == ACT_SELECT) break;
      if (action == ACT_BACK) return;
      delay(20);
    }
  }
}

void sortBleByRssi() {
  for (int i = 0; i < bleResultCount - 1; i++) {
    for (int j = i + 1; j < bleResultCount; j++) {
      if (bleResults[j].rssi > bleResults[i].rssi) {
        BleResult tmp = bleResults[i];
        bleResults[i] = bleResults[j];
        bleResults[j] = tmp;
      }
    }
  }
}

void collectBleResults() {
  bleResultCount = 0;

#if ENABLE_BLE_SCAN
  BLEDevice::init("");
  BLEScan *scan = BLEDevice::getScan();
  scan->setActiveScan(true);
  scan->setInterval(100);
  scan->setWindow(80);

#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  BLEScanResults *found = scan->start(4, false);
  int total = found ? found->getCount() : 0;
  for (int i = 0; i < total && bleResultCount < MAX_BLE_RESULTS; i++) {
    BLEAdvertisedDevice dev = found->getDevice(i);
#else
  BLEScanResults found = scan->start(4, false);
  int total = found.getCount();
  for (int i = 0; i < total && bleResultCount < MAX_BLE_RESULTS; i++) {
    BLEAdvertisedDevice dev = found.getDevice(i);
#endif
    String name = dev.haveName() ? String(dev.getName().c_str()) : String("(no name)");
    bleResults[bleResultCount].name = name;
    bleResults[bleResultCount].address = String(dev.getAddress().toString().c_str());
    bleResults[bleResultCount].rssi = dev.getRSSI();
    bleResultCount++;
  }

  sortBleByRssi();
  scan->clearResults();
  BLEDevice::deinit(true);
#endif
}

void drawBleList(int selected, int top) {
  display.clearDisplay();
  drawHeader(String("BLE ") + bleResultCount);

#if !ENABLE_BLE_SCAN
  display.setCursor(0, 24);
  display.print("BLE disabled.");
  display.setCursor(0, 36);
  display.print("Set ENABLE_BLE_SCAN 1");
  drawFooter("SW2 back", "");
  display.display();
  return;
#else
  if (bleResultCount == 0) {
    display.setCursor(0, 26);
    display.print("No BLE devices.");
    drawFooter("SW2 rescan", "hold back");
    display.display();
    return;
  }

  int visible = min(MENU_VISIBLE_ROWS, bleResultCount);
  for (int row = 0; row < visible; row++) {
    int item = top + row;
    int y = 15 + row * 10;
    bool isSelected = item == selected;
    if (isSelected) {
      display.fillRect(0, y - 1, 123, 10, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    printTrimmed(bleResults[item].name, 1, y, 14);
    display.setCursor(90, y);
    display.print(bleResults[item].rssi);
  }
  display.setTextColor(SSD1306_WHITE);
  drawScrollBar(top, bleResultCount, MENU_VISIBLE_ROWS, 14, 38);
  drawFooter("SW2 detail", "hold back");
  display.display();
#endif
}

void showBleDetail(int index) {
  while (true) {
    display.clearDisplay();
    drawHeader("BLE Detail");
    display.setCursor(0, 15);
    display.print("Name ");
    printTrimmed(bleResults[index].name, 30, 15, 16);
    display.setCursor(0, 28);
    display.print("RSSI ");
    display.print(bleResults[index].rssi);
    display.print(" dBm");
    display.setCursor(0, 41);
    printTrimmed(bleResults[index].address, 0, 41, 21);
    drawFooter("SW2 back", "");
    display.display();

    InputAction action = readInputAction(false);
    if (action == ACT_SELECT || action == ACT_BACK) return;
    delay(20);
  }
}

void runBleScan() {
#if !ENABLE_BLE_SCAN
  drawBleList(0, 0);
  while (true) {
    InputAction action = readInputAction(false);
    if (action == ACT_SELECT || action == ACT_BACK) return;
    delay(20);
  }
#else
  while (true) {
    showBusy("BLE Scan", "Scanning BLE...", "4 seconds");
    collectBleResults();
    int selected = 0;
    int top = 0;

    while (true) {
      if (bleResultCount > 0) {
        selected = clampInt(selected, 0, bleResultCount - 1);
        adjustTopForSelection(selected, top, bleResultCount, MENU_VISIBLE_ROWS);
      }
      drawBleList(selected, top);

      InputAction action = readInputAction(true);
      if (action == ACT_DOWN && bleResultCount > 0) selected = (selected + 1) % bleResultCount;
      if (action == ACT_UP && bleResultCount > 0) selected = (selected + bleResultCount - 1) % bleResultCount;
      if (action == ACT_SELECT) {
        if (bleResultCount > 0) showBleDetail(selected);
        else break;
      }
      if (action == ACT_BACK) return;
      delay(20);
    }
  }
#endif
}

void runSoftAPDemo() {
  const char *apSsid = "XIAO-TOOLS";
  const char *apPass = "12345678";

  WiFi.mode(WIFI_AP_STA);
  bool ok = WiFi.softAP(apSsid, apPass, 1, false, 4);

  while (true) {
    display.clearDisplay();
    drawHeader("SoftAP Demo");
    if (!ok) {
      display.setCursor(0, 24);
      display.print("AP start failed.");
    } else {
      display.setCursor(0, 15);
      display.print("SSID ");
      display.print(apSsid);
      display.setCursor(0, 25);
      display.print("PASS ");
      display.print(apPass);
      display.setCursor(0, 35);
      display.print("IP ");
      display.print(WiFi.softAPIP());
      display.setCursor(0, 45);
      display.print("Clients ");
      display.print(WiFi.softAPgetStationNum());
    }
    drawFooter("SW2 stop", "");
    display.display();

    InputAction action = readInputAction(false);
    if (action == ACT_SELECT || action == ACT_BACK) break;
    delay(250);
  }

  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
}

String resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_EXT: return "EXT";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT WDT";
    case ESP_RST_TASK_WDT: return "TASK WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "UNKNOWN";
  }
}

String kb(uint32_t bytes) {
  return String(bytes / 1024) + " KB";
}

void drawSysInfoPage(int page) {
  display.clearDisplay();
  drawHeader(String("Sys Info ") + (page + 1) + "/4");

  if (page == 0) {
    display.setCursor(0, 15);
    display.print("Chip ");
    display.print(ESP.getChipModel());
    display.setCursor(0, 25);
    display.print("Cores ");
    display.print(ESP.getChipCores());
    display.print(" Rev ");
    display.print(ESP.getChipRevision());
    display.setCursor(0, 35);
    display.print("CPU ");
    display.print(getCpuFrequencyMhz());
    display.print(" MHz");
    display.setCursor(0, 45);
    display.print("SDK ");
    printTrimmed(String(ESP.getSdkVersion()), 25, 45, 16);
  } else if (page == 1) {
    display.setCursor(0, 15);
    display.print("Heap ");
    display.print(kb(ESP.getFreeHeap()));
    display.setCursor(0, 25);
    display.print("Min ");
    display.print(kb(ESP.getMinFreeHeap()));
    display.setCursor(0, 35);
    display.print("Max block ");
    display.print(kb(ESP.getMaxAllocHeap()));
    display.setCursor(0, 45);
    display.print("PSRAM ");
    display.print(psramFound() ? kb(ESP.getFreePsram()) : "none");
  } else if (page == 2) {
    display.setCursor(0, 15);
    display.print("Flash ");
    display.print(kb(ESP.getFlashChipSize()));
    display.setCursor(0, 25);
    display.print("Sketch ");
    display.print(kb(ESP.getSketchSize()));
    display.setCursor(0, 35);
    display.print("Free OTA ");
    display.print(kb(ESP.getFreeSketchSpace()));
    display.setCursor(0, 45);
    printTrimmed(WiFi.macAddress(), 0, 45, 21);
  } else {
    display.setCursor(0, 15);
    display.print("Uptime ");
    display.print(millis() / 1000);
    display.print(" s");
    display.setCursor(0, 25);
    display.print("Temp ");
    display.print(temperatureRead(), 1);
    display.print(" C");
    display.setCursor(0, 35);
    display.print("Reset ");
    display.print(resetReasonToString(esp_reset_reason()));
#if BATTERY_ADC_PIN >= 0
    int raw = analogRead(BATTERY_ADC_PIN);
    float volts = (raw / 4095.0f) * 3.3f * BATTERY_DIVIDER_RATIO;
    display.setCursor(0, 45);
    display.print("BAT ");
    display.print(volts, 2);
    display.print(" V");
#else
    display.setCursor(0, 45);
    display.print("Battery ADC off");
#endif
  }

  drawFooter("SW3 page", "SW2 back");
  display.display();
}

void showSysInfo() {
  int page = 0;
  while (true) {
    drawSysInfoPage(page);
    InputAction action = readInputAction(true);
    if (action == ACT_DOWN) page = (page + 1) % 4;
    if (action == ACT_UP) page = (page + 3) % 4;
    if (action == ACT_SELECT || action == ACT_BACK) return;
    delay(20);
  }
}

String i2cName(uint8_t addr) {
  if (addr == 0x3C || addr == 0x3D) return "SSD1306 OLED";
  if (addr == 0x68) return "IMU/RTC";
  if (addr == 0x76 || addr == 0x77) return "BME/BMP";
  if (addr == 0x40) return "INA/SHT";
  if (addr == 0x20 || addr == 0x27) return "IO/LCD";
  return "device";
}

String hexAddr(uint8_t value) {
  char buffer[6];
  snprintf(buffer, sizeof(buffer), "0x%02X", value);
  return String(buffer);
}

void runI2CScan() {
  const int maxDevices = 16;
  uint8_t addresses[maxDevices];
  int count = 0;
  int selected = 0;
  int top = 0;

  while (true) {
    showBusy("I2C Scan", "Checking 0x01-0x7E");
    count = 0;
    for (uint8_t addr = 1; addr < 127 && count < maxDevices; addr++) {
      Wire.beginTransmission(addr);
      uint8_t error = Wire.endTransmission();
      if (error == 0) addresses[count++] = addr;
      delay(2);
    }

    while (true) {
      if (count > 0) {
        selected = clampInt(selected, 0, count - 1);
        adjustTopForSelection(selected, top, count, MENU_VISIBLE_ROWS);
      }

      display.clearDisplay();
      drawHeader(String("I2C ") + count);

      if (count == 0) {
        display.setCursor(0, 27);
        display.print("No I2C devices.");
      } else {
        int visible = min(MENU_VISIBLE_ROWS, count);
        for (int row = 0; row < visible; row++) {
          int item = top + row;
          int y = 15 + row * 10;
          bool isSelected = item == selected;
          if (isSelected) {
            display.fillRect(0, y - 1, 123, 10, SSD1306_WHITE);
            display.setTextColor(SSD1306_BLACK);
          } else {
            display.setTextColor(SSD1306_WHITE);
          }
          display.setCursor(2, y);
          display.print(hexAddr(addresses[item]));
          printTrimmed(i2cName(addresses[item]), 40, y, 13);
        }
        display.setTextColor(SSD1306_WHITE);
        drawScrollBar(top, count, MENU_VISIBLE_ROWS, 14, 38);
      }

      drawFooter("SW2 rescan", "hold back");
      display.display();

      InputAction action = readInputAction(true);
      if (action == ACT_DOWN && count > 0) selected = (selected + 1) % count;
      if (action == ACT_UP && count > 0) selected = (selected + count - 1) % count;
      if (action == ACT_SELECT) break;
      if (action == ACT_BACK) return;
      delay(20);
    }
  }
}

void drawGpioMonitor() {
  display.clearDisplay();
  drawHeader("GPIO Monitor");
  display.setCursor(0, 15);
  display.print("SW2 ");
  display.print(digitalRead(SW2) == LOW ? "DOWN" : "UP");
  display.print("  SW3 ");
  display.print(digitalRead(SW3) == LOW ? "DOWN" : "UP");

  display.setCursor(0, 27);
  display.print("Enc A ");
  display.print(digitalRead(Encoder_A));
  display.print(" B ");
  display.print(digitalRead(Encoder_B));
  display.print(" BTN ");
  display.print(digitalRead(Encoder_Button_S1) == LOW ? "D" : "U");

  display.setCursor(0, 39);
  display.print("Mode ");
  display.print(encoderSeen ? "rotary+buttons" : "buttons only");

  display.setCursor(0, 49);
  display.print("Hold SW2 to exit");
  display.display();
}

void runGpioMonitor() {
  clearButtonEvents(btnSelect);
  clearButtonEvents(btnNext);
  clearButtonEvents(btnEncoder);

  while (true) {
    pollControls();
    drawGpioMonitor();
    if (takeLong(btnSelect) || takeShort(btnEncoder) || takeLong(btnEncoder)) return;
    clearButtonEvents(btnNext);
    clearButtonEvents(btnSelect);
    delay(100);
  }
}

void runDisplayTest() {
  for (int frame = 0; frame < 90; frame++) {
    display.clearDisplay();
    drawHeader("Display Test");
    display.drawRect(0, 14, 128, 38, SSD1306_WHITE);
    display.drawLine(0, 51, 127, 14, SSD1306_WHITE);
    display.drawLine(0, 14, 127, 51, SSD1306_WHITE);
    display.fillCircle((frame * 3) % 128, 33, 4, SSD1306_WHITE);
    display.setCursor(30, 56);
    display.print("SW2 exits");
    display.display();

    InputAction action = readInputAction(true);
    if (action != ACT_NONE) break;
    delay(35);
  }

  display.invertDisplay(true);
  delay(180);
  display.invertDisplay(false);
}

void runDeepSleep() {
  while (true) {
    display.clearDisplay();
    drawHeader("Sleep 10s");
    display.setCursor(0, 18);
    display.print("Deep sleep saves power.");
    display.setCursor(0, 31);
    display.print("SW2 starts timer");
    display.setCursor(0, 44);
    display.print("SW3 cancels");
    drawFooter("SW2 sleep", "SW3 back");
    display.display();

    InputAction action = readInputAction(false);
    if (action == ACT_BACK) return;
    if (action == ACT_SELECT) {
      display.clearDisplay();
      drawHeader("Sleeping");
      display.setCursor(0, 28);
      display.print("Wake in 10 seconds...");
      display.display();
      delay(500);
      esp_sleep_enable_timer_wakeup(10ULL * 1000000ULL);
      esp_deep_sleep_start();
    }
    delay(20);
  }
}

void drawAboutPage(int page) {
  display.clearDisplay();
  drawHeader(String("Help ") + (page + 1) + "/3");

  if (page == 0) {
    display.setCursor(0, 15);
    display.print("SW3: next item");
    display.setCursor(0, 27);
    display.print("Hold SW3: previous");
    display.setCursor(0, 39);
    display.print("SW2: open/select");
  } else if (page == 1) {
    display.setCursor(0, 15);
    display.print("Rotary is optional.");
    display.setCursor(0, 27);
    display.print("Move encoder and");
    display.setCursor(0, 39);
    display.print("UI switches to ENC.");
  } else {
    display.setCursor(0, 15);
    display.print("OLED SDA ");
    display.print(I2C_SDA);
    display.print(" SCL ");
    display.print(I2C_SCL);
    display.setCursor(0, 27);
    display.print("ENC A/B ");
    display.print(Encoder_A);
    display.print("/");
    display.print(Encoder_B);
    display.setCursor(0, 39);
    display.print("SW2/SW3 ");
    display.print(SW2);
    display.print("/");
    display.print(SW3);
  }

  drawFooter("SW3 page", "SW2 back");
  display.display();
}

void showAbout() {
  int page = 0;
  while (true) {
    drawAboutPage(page);
    InputAction action = readInputAction(true);
    if (action == ACT_DOWN) page = (page + 1) % 3;
    if (action == ACT_UP) page = (page + 2) % 3;
    if (action == ACT_SELECT || action == ACT_BACK) return;
    delay(20);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(Encoder_A, INPUT_PULLUP);
  pinMode(Encoder_B, INPUT_PULLUP);
  pinMode(Encoder_Button_S1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);

  initButton(btnSelect);
  initButton(btnNext);
  initButton(btnEncoder);
  lastEncoderState = (digitalRead(Encoder_A) << 1) | digitalRead(Encoder_B);

#if BATTERY_ADC_PIN >= 0
  analogReadResolution(12);
#endif

  Wire.begin(I2C_SDA, I2C_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR_1)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR_2)) {
      while (true) delay(100);
    }
  }

  display.cp437(true);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  showBootScreen();
  drawMenu();
}

void loop() {
  InputAction action = readInputAction(true);

  if (action == ACT_DOWN) {
    menuIndex = (menuIndex + 1) % NUM_MENU_ITEMS;
    drawMenu();
  } else if (action == ACT_UP) {
    menuIndex = (menuIndex + NUM_MENU_ITEMS - 1) % NUM_MENU_ITEMS;
    drawMenu();
  } else if (action == ACT_SELECT) {
    display.clearDisplay();
    showBusy("Opening", menuItems[menuIndex].label);
    delay(120);
    menuItems[menuIndex].handler();
    clearButtonEvents(btnSelect);
    clearButtonEvents(btnNext);
    clearButtonEvents(btnEncoder);
    drawMenu();
  }

  delay(10);
}
}
