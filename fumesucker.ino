//   Copyright 2016 Lukas Lösche <lloesche@fedoraproject.org>
//
//   Licensed under the Apache License, Version 2.0 (the "License");
//   you may not use this file except in compliance with the License.
//   You may obtain a copy of the License at
//
//       http://www.apache.org/licenses/LICENSE-2.0
//
//   Unless required by applicable law or agreed to in writing, software
//   distributed under the License is distributed on an "AS IS" BASIS,
//   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//   See the License for the specific language governing permissions and
//   limitations under the License.

#include <SPI.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_PCD8544.h>

#define FAN1_PIN      9   // FAN1 PWM pin going to ULN2003A pin 1
#define FAN2_PIN     10   // FAN2 PWM pin going to ULN2003A pin 4
#define BUTTON_PIN   A0   // Rotary encoder button
#define ENCODER_PIN1 A1   // Rotary encoder wheel
#define ENCODER_PIN2 A2   // Rotary encoder wheel
#define PIN_BKLT      2   // PCD8544 backlight pin
#define PIN_VCC       3   // PCD8544 VCC pin
#define PIN_SCLK      4   // PCD8544 clock pin
#define PIN_SDIN      5   // PCD8544 serial data pin
#define PIN_DC        6   // PCD8544 data/command choice pin
#define PIN_SCE       7   // PCD8544 chip selection pin
#define PIN_RESET     8   // PCD8544 reset pin
#define bklt_on     LOW   // PCD8544 backlight pin state when light is on (depends on your display manufacturer)
#define bklt_off   HIGH   // PCD8544 backlight pin state when light is off (depends on your display manufacturer)
#define min_speed    15   // Minimum speed (analogWrite PWM) our fan will run at when ramped up
#define ramp_speed   30   // Minimum speed (analogWrite PWM) our fan will start from
#define max_speed   255   // Maximum speed (255 = analogWrite() max value)
#define pwm_mode   0x01   // Timer 1 PWM Mode: 0x01=31kHz, 0x02=3.9kHz, 0x03=490Hz (default), 0x04=122Hz, 0x05=30Hz

boolean change_pending = false;        // True if a a speed change is pending by turning the rotary encoder
boolean ramping = false;               // True if we're currently ramping up the fans
int speed;                             // An analogWrite() PWM value of 0 (off) or between min_speed and max_speed
int speed_percent;                     // The current fan speed in percent
volatile int set_speed_percent;        // The fan speed that will be set when the button is pressed
volatile byte encoder_sequence1 = 0;   // Stores the sequence of ENCODER_PIN1 states
volatile byte encoder_sequence2 = 0;   // Stores the sequence of ENCODER_PIN2 states
volatile boolean turned = false;       // True if the rotary encoder was turned
volatile boolean button = false;       // True if the rotary encoder button was pushed
unsigned long interval = 3000;         // Timeout value in ms after which we turn of the backlight and redraw the display
unsigned long last_action_millis = 0;  // Time in millis() we last performed an action
unsigned long ramp_start_millis = 0;   // Time in millis() a ramp up was started

Adafruit_PCD8544 display = Adafruit_PCD8544(PIN_SCLK, PIN_SDIN, PIN_DC, PIN_SCE, PIN_RESET);


void setup() {
  noInterrupts();     // Turn interrupts off

  allPinsToOutput();  // Save some power

  // Set PWM Mode on Timer1 (Arduino pins 9/10, chip pins 15/16)
  TCCR1A = (TCCR1A & 0b11111000) | pwm_mode;
  TCCR1B = (TCCR1B & 0b11111000) | pwm_mode;

  // Enable pin change interrupts
  PCICR =  0b00000010;  // Enable PCIE1
  PCMSK1 = 0b00000111;  // for A0, A1 and A2 (D14-D16)

  // Read previously stored speed from EEPROM.
  speed_percent = EEPROM.read(0);
  // Validate the stored data and default to sane values.
  if(speed_percent < 0) {
    speed_percent = 0;
  } else if(speed_percent > 100) {
    speed_percent = 100;
  }
  set_speed_percent = speed_percent;
  // Map the speed in percent (0-100) to an analogWrite() PWM value between min_speed
  // and max_speed (255) or set to 0 (turn fans off) if speed_percent is 0%
  speed = speed_percent > 0 ? map(speed_percent, 0, 100, min_speed, max_speed) : 0;

  // Set up rotary encoder pins
  pinMode(BUTTON_PIN, INPUT);
  pinMode(ENCODER_PIN1, INPUT);
  pinMode(ENCODER_PIN2, INPUT);
  digitalWrite(BUTTON_PIN, HIGH);
  digitalWrite(ENCODER_PIN1, HIGH);
  digitalWrite(ENCODER_PIN2, HIGH);

  // Set up fan pins
  pinMode(FAN1_PIN, OUTPUT);
  pinMode(FAN2_PIN, OUTPUT); 

  applySpeed();  // Apply the previously stored speed

  // Initialize the display backlight
  pinMode(PIN_VCC, OUTPUT);
  digitalWrite(PIN_VCC, HIGH); // Display on
  pinMode(PIN_BKLT, OUTPUT);
  digitalWrite(PIN_BKLT, bklt_off); // Backlight is off by default

  // Initialize the display
  display.setRotation(2);
  display.begin(60);
  display.clearDisplay();
  display.display();

  render(false);  // Initial render of the screen

  interrupts();   // Turn interrupts on
}


void loop() {
  // We work time based without delay().
  // Details: https://www.arduino.cc/en/Reference/Millis
  // Details: https://www.baldengineer.com/millis-tutorial.html
  unsigned long current_millis = millis();

  // If our last action (putton press / encoder turn) was more
  // than 'interval' ms ago.
  if ((unsigned long)(current_millis - last_action_millis) >= interval) {
    last_action_millis = millis();

    digitalWrite(PIN_BKLT, bklt_off);  // Turn off the backlight

    // If the encoder was turned to a new speed value (change_pending == true)
    // render screen also showing the new value.
    // Otherwise (change_pending == false) render screen
    // only showing the current speed.
    render(change_pending);
  }

  // If the encoder wheel was turned
  if (turned) {
    last_action_millis = millis();
    turned = false;

    digitalWrite(PIN_BKLT, bklt_on);  // Turn on the backlight

    render(true);  // Render screen showing the new speed value

    // This makes sure we only have a change pending
    // if a new speed value was dialed in.
    // I.e. if the wheel was turned but ended up at the same
    // speed we're already on we don't have a speed change pending.
    change_pending = speed_percent != set_speed_percent ? true : false;
  }

  // If the encoder button was pressed
  if (button) {
    last_action_millis = millis();
    digitalWrite(PIN_BKLT, bklt_on);  // Turn on the backlight
    button = false;

    // If there's a speed change pending
    // store the new speed in EEPROM
    // and apply it to the fans.
    if(change_pending) {
      noInterrupts();
      EEPROM.write(0, set_speed_percent);
      applySpeed();
      interrupts();
      render(true);
      change_pending = false;
    }
  }

  // If we were previously ramping up the fans and 'interval' ms have
  // passed turn down the speed to the actual value.
  if (ramping && (unsigned long)(current_millis - ramp_start_millis) >= interval) {
    ramping = false;
    analogWrite(FAN1_PIN, speed);
    analogWrite(FAN2_PIN, speed);
  }
}


// This function applies a new speed value to the fans.
void applySpeed() {
  int previous_speed = speed;
  speed_percent = set_speed_percent;
  speed = speed_percent > 0 ? map(speed_percent, 0, 100, min_speed, max_speed) : 0;

  // If the speed is lower than what the fans can start from we will
  // ramp them up at a higher speed and lower it in loop() after 'interval' ms
  // have passed.
  if (speed > 0 && speed < ramp_speed && previous_speed < ramp_speed) {
    ramping = true;
    ramp_start_millis = millis();
    analogWrite(FAN1_PIN, ramp_speed);
    analogWrite(FAN2_PIN, ramp_speed);
  } else {
    analogWrite(FAN1_PIN, speed);
    analogWrite(FAN2_PIN, speed);
  }
}


// This function renders the screen.
// It's only bool arg determins whether or not a second
// speed percentage number should be displayed or not.
// This is true when the encoder wheel was turned
// to set a new speed value.
// Details: https://learn.adafruit.com/adafruit-gfx-graphics-library?view=all
void render(bool show_second_value) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(BLACK, WHITE);
    display.setCursor(10, 0);
    display.print("FUME SUCKER");
    display.drawFastHLine(0,10,83,BLACK);
    display.setCursor(5, 15);
    display.print("FAN");
    display.setTextSize(2);
    display.setCursor(5, 25);
    display.print(speed_percent);
    display.print("%");

    // Display a second value with the new speed the
    // fans will be set to when the button is pushed.
    if (show_second_value) {
      display.setTextSize(1);
      display.setCursor(58, 25);
      display.print(set_speed_percent);
      display.print("%");
    }

    display.display();  // Actually render the display
}


// Handle pin change interrupts.
// Details: http://playground.arduino.cc/Main/PinChangeInterrupt
ISR (PCINT1_vect) {
  if (!digitalRead(BUTTON_PIN)) {   // Rotary encoder button was pushed
    button = true;
  } else {                          // Rotary encoder was turned
    // Reading the rotary encoder
    // Details: http://www.allaboutcircuits.com/projects/how-to-use-a-rotary-encoder-in-a-mcu-based-project/
    boolean encoder1_status = digitalRead(ENCODER_PIN1);
    boolean encoder2_status = digitalRead(ENCODER_PIN2);

    encoder_sequence1 <<= 1;
    encoder_sequence1 |= encoder1_status;
    encoder_sequence1 &= 0b00001111;

    encoder_sequence2 <<= 1;
    encoder_sequence2 |= encoder2_status;
    encoder_sequence2 &= 0b00001111;
    
    if (encoder_sequence1 == 0b00001001 && encoder_sequence2 == 0b00000011) {         // Left turn
      set_speed_percent--;
      if (set_speed_percent <= 0) {
        set_speed_percent = 0;
      }
      turned = true;
    } else if (encoder_sequence1 == 0b00000011 && encoder_sequence2 == 0b00001001) {  // Right turn
      set_speed_percent++;
      if (set_speed_percent >= 100) {
        set_speed_percent = 100;
      }
      turned = true;
    }
  }
}

// Initially set all pins to OUTPUT to
// save around 4mA of current.
// Details: https://youtu.be/urLSDi7SD8M
void allPinsToOutput() {
  for (int i=0; i<20; i++) {
    pinMode(i, OUTPUT);
  }
}
