// My Battery Project
// Reads voltage, current, and temp to show the battery SOC (State of Charge).
// It's supposed to remember the battery level even if I unplug it.
// Written by me, with a lot of help from Google.

// All the libraries I need to make this work
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <EEPROM.h>

// --- Pin Setup ---
// Where I plugged everything into my ESP32
const int VOLTAGE_PIN = 13;       // The voltage sensor's signal pin
const int CURRENT_PIN = 4;        // The current sensor's signal pin
const int CALIBRATE_BUTTON = 5;   // The button to zero-out the current sensor
const int TEMP_PROBE_PIN = 23;    // The data pin for the DS18B20 temp probe

// --- Screen Stuff ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_SDA_PIN 21
#define OLED_SCL_PIN 22
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- OneWire Setup for the Temp Sensor ---
OneWire oneWireThing(TEMP_PROBE_PIN);
DallasTemperature tempSensor(&oneWireThing);

// --- Battery Settings ---
// The total size of my battery pack in Amp-hours. I looked this up online.
const float BATTERY_FULL_CAPACITY = 12.064;

// --- Sensor Tuning ---
// These numbers make the sensors read correctly. Had to figure these out by testing.
const float VOLTAGE_CORRECTION = 5.423; // My voltage divider isn't perfect, so this fixes it.
const float CURRENT_SENSITIVITY = 0.185; // This is from the ACS712 datasheet for the 5A version.
const float ESP32_VOLTAGE = 3.3;

// --- Global Variables ---
// Stuff that needs to be accessed by multiple functions
float noLoadVoltage = 0; // Stores the sensor voltage when there's no current
float batteryJuiceLeft;  // This holds the remaining Ah, gets loaded from memory
unsigned long timeOfLastCheck = 0;
unsigned long timeOfLastSave = 0;

// --- Tweakable Settings ---
// How many readings to average for the current. More means smoother but slower.
#define SAMPLES_TO_AVERAGE 200
// If the current is less than this, just call it zero. Helps ignore noise.
const float CURRENT_NOISE_THRESHOLD = 0.05;

void setup() {
  Serial.begin(115200); // Gotta start the serial port to see what's happening

  // --- EEPROM - The ESP32's memory ---
  EEPROM.begin(4); // I only need 4 bytes to store the battery level (a float).
  // Try to load the last battery level.
  EEPROM.get(0, batteryJuiceLeft);
  // If the memory is weird (first time running, etc.), just start fresh with a full battery.
  if (isnan(batteryJuiceLeft) || batteryJuiceLeft < 0 || batteryJuiceLeft > BATTERY_FULL_CAPACITY) {
    Serial.println("Memory was empty or weird. Assuming battery is full.");
    batteryJuiceLeft = BATTERY_FULL_CAPACITY;
  } else {
    Serial.print("Got old battery level from memory: ");
    Serial.println(batteryJuiceLeft);
  }

  // Set up the button. INPUT_PULLUP means I don't need an external resistor for it.
  pinMode(CALIBRATE_BUTTON, INPUT_PULLUP);

  // Get the screen running
  Wire.begin(OLED_SDA_PIN, OLED_SCL_PIN);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Can't find the screen, freezing.");
    while (true); // Loop forever if the screen isn't connected
  }

  // Start the temp sensor
  tempSensor.begin();

  analogReadResolution(12); // Use the full 12-bit ADC range

  zeroOutCurrentSensor(); // Do the first calibration when it turns on

  delay(2000); // Wait a couple seconds before the main loop starts
  timeOfLastCheck = millis();
  timeOfLastSave = millis();
}

void loop() {
  // If I press the button (connecting the pin to Ground), run the calibration
  if (digitalRead(CALIBRATE_BUTTON) == LOW) {
    zeroOutCurrentSensor();
    // This loop just waits for me to let go of the button
    while (digitalRead(CALIBRATE_BUTTON) == LOW) {
      delay(50);
    }
  }

  // I only want to update the screen once per second, not a zillion times
  if (millis() - timeOfLastCheck >= 1000) {
    timeOfLastCheck = millis();

    // --- Get all the sensor readings ---

    // Get Voltage
    int rawVoltageReading = analogRead(VOLTAGE_PIN);
    float voltageAtPin = (rawVoltageReading / 4095.0) * ESP32_VOLTAGE;
    float actualBatteryVoltage = voltageAtPin * VOLTAGE_CORRECTION;

    // Get Current (and average a bunch of readings to make it stable)
    long totalCurrentReading = 0;
    for (int i = 0; i < SAMPLES_TO_AVERAGE; i++) {
      totalCurrentReading += analogRead(CURRENT_PIN);
    }
    int avgCurrentReading = totalCurrentReading / SAMPLES_TO_AVERAGE;
    float currentSensorVoltage = (avgCurrentReading / 4095.0) * ESP32_VOLTAGE;
    // The sensor is wired backwards for discharging, so I have to flip the math here
    float amps = (noLoadVoltage - currentSensorVoltage) / CURRENT_SENSITIVITY;

    // If the current is super small, it's probably just noise, so ignore it.
    if (abs(amps) < CURRENT_NOISE_THRESHOLD) {
      amps = 0.0;
    }

    // --- Get Temperature ---
    // Ask the sensor to get the temp
    tempSensor.requestTemperatures(); 
    // Wait a tiny bit for the sensor to finish its measurement
    delay(10); // <--- This is the new line I added
    // Now, grab the temperature value
    float tempC = tempSensor.getTempCByIndex(0);


    // --- Do all the math for the battery level ---

    // Figure out how much power was used since the last check
    float timePassedInHours = 1.0 / 3600.0; // 1 second is 1/3600th of an hour
    float ampsUsed = amps * timePassedInHours;
    batteryJuiceLeft -= ampsUsed;

    // Just in case, make sure the battery level doesn't go below 0 or above full
    if (batteryJuiceLeft < 0) batteryJuiceLeft = 0;
    if (batteryJuiceLeft > BATTERY_FULL_CAPACITY) batteryJuiceLeft = BATTERY_FULL_CAPACITY;

    // Turn the remaining amps into a percentage
    float stateOfChargePercent = (batteryJuiceLeft / BATTERY_FULL_CAPACITY) * 100.0;


    // --- Put everything on the screen ---
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1); // Use the small font for everything
    display.setCursor(0, 0); // Start at the top-left corner

    // Line 1: Voltage
    display.print("V:   ");
    display.print(actualBatteryVoltage, 2); // 2 decimal places
    display.println(" V");
    
    display.setCursor(0, 10); // Move down 10 pixels for the next line
    // Line 2: Current
    display.print("I:   ");
    display.print(amps, 3); // 3 decimal places
    display.println(" A");

    display.setCursor(0, 20); // Move down 10 pixels
    // Line 3: SOC
    display.print("SOC: ");
    display.print(stateOfChargePercent, 1);
    display.println(" %");

    display.setCursor(0, 30); // Move down 10 pixels
    // Line 4: Temperature
    display.print("TMP: ");
    display.print(tempC, 1);
    display.println(" C");

    // Add a static reminder at the bottom for the button
    display.setCursor(0, 56); // Go to the last line
    display.print("GND Pin 5 to Calibrate");


    display.display(); // This actually draws it on the screen

    // Also print to the serial monitor, it's useful for debugging
    Serial.print("V: "); Serial.print(actualBatteryVoltage, 2);
    Serial.print("V | I: "); Serial.print(amps, 3);
    Serial.print("A | SOC: "); Serial.print(stateOfChargePercent, 1);
    Serial.print("% | Temp: "); Serial.print(tempC, 1); Serial.println("C");
  }

  // Save the current battery level to the ESP32's memory every minute
  // so I don't have to do it a million times and wear it out.
  if (millis() - timeOfLastSave >= 60000) {
    timeOfLastSave = millis();
    EEPROM.put(0, batteryJuiceLeft);
    EEPROM.commit(); // This makes sure it actually saves
    Serial.println("Saved battery level to memory.");
  }
}

// This whole function is just for zero-ing out the current sensor
void zeroOutCurrentSensor() {
  Serial.println("\nCalibrating, make sure there's no load...");
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Calibrating...");
  display.display();

  long totalReading = 0;
  for (int i = 0; i < 500; i++) {
    totalReading += analogRead(CURRENT_PIN);
    delay(2);
  }
  int avgReading = totalReading / 500;
  noLoadVoltage = (avgReading / 4095.0) * ESP32_VOLTAGE;

  Serial.print("New zero point voltage is: ");
  Serial.println(noLoadVoltage, 3);
}

