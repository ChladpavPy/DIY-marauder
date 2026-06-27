#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define I2C_SDA 6
#define I2C_SCL 7
#define Encoder_A 27
#define Encoder_B 28
#define Encoder_Button_S1 26
#define SW2 0
#define SW3 1 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

void setup() {
  pinMode(Encoder_A, INPUT_PULLUP);
  pinMode(Encoder_B, INPUT_PULLUP);
  pinMode(Encoder_Button_S1, INPUT_PULLUP);
  pinMode(SW2, INPUT_PULLUP);
  pinMode(SW3, INPUT_PULLUP);

  Wire.setSDA(I2C_SDA);
  Wire.setSCL(I2C_SCL);
  Wire.begin();

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for(;;);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("DIY Marauder");
  display.println("System Ready");
  display.display();
}

void loop() {
  if (digitalRead(SW2) == LOW) {
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("SW2 Pressed");
    display.display();
    delay(200); 
  }
}