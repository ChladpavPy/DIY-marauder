#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>

#define I2C_SDA 5
#define I2C_SCL 6
#define Encoder_Button_S1 1
#define Encoder_A 2
#define Encoder_B 3
#define SW2 43
#define SW3 44

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

int menuIndex = 0;
int lastEncoderA = HIGH;
const int NUM_ITEMS = 3;
String menuItems[NUM_ITEMS] = {"WiFi Scan", "Beacon Spam", "Sys Info"};

void showBootScreen() {
  display.clearDisplay();
  
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
  display.drawRect(2, 2, SCREEN_WIDTH-4, SCREEN_HEIGHT-4, SSD1306_WHITE);

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(15, 15);
  display.println("MARAUDER");

  display.setTextSize(1);
  display.setCursor(25, 40);
  display.println("SYSTEM ONLINE");

  display.display();
  delay(2000); 
}

void drawMenu() {
  display.clearDisplay();
  
  display.fillRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(5, 3);
  display.println("DIY Marauder OS");

  display.setTextColor(SSD1306_WHITE);
  for (int i = 0; i < NUM_ITEMS; i++) {
    int yPos = 25 + (i * 12);
    if (i == menuIndex) {
      display.setCursor(0, yPos);
      display.print("> ");
    } else {
      display.setCursor(0, yPos);
      display.print("  ");
    }
    display.println(menuItems[i]);
  }
  
  display.display();
}

void runWiFiScan() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3);
  display.println("Skenuji pasmo...");
  display.display();

  int n = WiFi.scanNetworks();
  
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print("Cile nalezene: ");
  display.print(n);

  display.setTextColor(SSD1306_WHITE);
  
  if (n == 0) {
    display.setCursor(0, 30);
    display.println("Zadne site v dosahu.");
  } else {
    int yPos = 16;
    for (int i = 0; i < n && i < 5; ++i) {
      display.setCursor(0, yPos);
      String ssid = WiFi.SSID(i);
      if(ssid.length() > 12) ssid = ssid.substring(0, 12);
      display.print(ssid);
      
      display.setCursor(90, yPos);
      display.print(WiFi.RSSI(i));
      display.print("dB");
      
      yPos += 10;
    }
  }
  
  display.display();
  while(digitalRead(SW3) == HIGH && digitalRead(Encoder_Button_S1) == HIGH) {
    delay(50);
  }
}

void runBeaconSpam() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3);
  display.println("BEACON SPAM");
  
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 30);
  display.println("Vysilam fake AP...");
  display.setCursor(0, 50);
  display.println("Zmackni SW3 pro stop");
  display.display();

  const char* spamNames[] = {"Never", "Gonna", "Give", "You", "Up"};
  int i = 0;
  
  while(digitalRead(SW3) == HIGH && digitalRead(Encoder_Button_S1) == HIGH) {
    WiFi.softAP(spamNames[i % 5]);
    i++;
    delay(400);
  }
  
  WiFi.softAPdisconnect(true);
}

void showSysInfo() {
  display.clearDisplay();
  display.fillRect(0, 0, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setCursor(2, 3);
  display.println("SYS INFO");
  
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 25);
  display.print("MAC: ");
  display.println(WiFi.macAddress());
  
  display.setCursor(0, 40);
  display.print("RAM: ");
  display.print(ESP.getFreeHeap() / 1024);
  display.println(" KB");
  
  display.display();
  
  while(digitalRead(SW3) == HIGH && digitalRead(Encoder_Button_S1) == HIGH) {
    delay(50);
  }
}

void setup() {
  Serial.begin(115200);

  pinMode(Encoder_A, INPUT_PULLUP);
  pinMode(Encoder_B, INPUT_PULLUP);
  pinMode(Encoder_Button_S1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);

  Wire.begin(I2C_SDA, I2C_SCL);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
        for(;;); 
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  showBootScreen();
  drawMenu();
}

void loop() {
  int currentEncoderA = digitalRead(Encoder_A);
  
  if (currentEncoderA != lastEncoderA && currentEncoderA == LOW) {
    if (digitalRead(Encoder_B) != currentEncoderA) {
      menuIndex++;
      if (menuIndex >= NUM_ITEMS) menuIndex = 0;
    } else {
      menuIndex--;
      if (menuIndex < 0) menuIndex = NUM_ITEMS - 1;
    }
    drawMenu();
  }
  lastEncoderA = currentEncoderA;

  if (digitalRead(Encoder_Button_S1) == LOW || digitalRead(SW2) == LOW) {
    delay(200); 
    
    if (menuIndex == 0) {
      runWiFiScan();
    } else if (menuIndex == 1) {
      runBeaconSpam();
    } else if (menuIndex == 2) {
      showSysInfo();
    }
    
    drawMenu();
    delay(200);
  }
}
