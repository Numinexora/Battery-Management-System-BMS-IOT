#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// --- OLED Display Configuration ---
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_SDA 21      // Correct I2C pin (GPIO 34 is input-only)
#define OLED_SCL 22      // Correct I2C pin (GPIO 35 is input-only)
#define OLED_RESET -1    // Reset pin # (or -1 if sharing Arduino reset pin)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// --- Sensor and Control Pin Definitions ---
const int VOLTAGE_SENSOR_PIN = 13; // S pin of the voltage sensor
const int CURRENT_SENSOR_PIN = 4;  // OUT pin of the current sensor
const int CALIBRATE_PIN = 5;       // Pin to trigger re-calibration when connected to GND

// --- Battery Characteristics ---
const float BATTERY_CAPACITY_Ah = 12.064;

// --- Calibration Constants ---
const float VOLTAGE_DIVIDER_RATIO = 5.423; // Your calibrated value
const float ACS712_SENSITIVITY = 0.185;    // Standard datasheet value
const float ADC_REFERENCE_VOLTAGE = 3.3;

// --- Global Variables ---
float zeroCurrentVoltage = 0;
float remainingCapacity_Ah = BATTERY_CAPACITY_Ah;
unsigned long lastTime = 0;

// --- Averaging and Dead Zone ---
// Increased samples for better smoothing of noisy readings
#define CURRENT_AVG_SAMPLES 400
// Lowered dead zone to prioritize seeing the small motor current over hiding all noise
const float CURRENT_DEAD_ZONE = 0.04; 

// Function declaration for on-demand calibration
void recalibrateCurrentSensor();

void setup() {
  Serial.begin(115200);

  // Set up the calibration button pin with an internal pull-up resistor.
  pinMode(CALIBRATE_PIN, INPUT_PULLUP);

  // Initialize I2C with custom pins for the OLED
  Wire.begin(OLED_SDA, OLED_SCL);

  // Initialize the OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3C for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't proceed, loop forever
  }

  // Show initial startup message on OLED
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Calibrating Sensor...");
  display.println("Ground pin 5");
  display.println("to re-calibrate.");
  display.display();

  analogReadResolution(12);

  // Perform the initial calibration on startup
  recalibrateCurrentSensor();

  delay(2000); // Wait 2 seconds before starting the main loop
  lastTime = millis();
}

void loop() {
  // Check if the calibration pin is connected to ground (button pressed)
  if (digitalRead(CALIBRATE_PIN) == LOW) {
    recalibrateCurrentSensor();
    // Wait while the button is held down to prevent rapid re-calibrations
    while(digitalRead(CALIBRATE_PIN) == LOW) {
      delay(50);
    }
  }

  // Update readings every 1000 milliseconds (1 second)
  if (millis() - lastTime >= 1000) {
    lastTime = millis();

    // --- Calculate time elapsed in hours ---
    float deltaTime_hours = 1000.0 / 3600000.0;

    // --- Read Voltage ---
    int voltage_adc_value = analogRead(VOLTAGE_SENSOR_PIN);
    float pin_voltage = (voltage_adc_value / 4095.0) * ADC_REFERENCE_VOLTAGE;
    float battery_voltage = pin_voltage * VOLTAGE_DIVIDER_RATIO;

    // --- Read and Average Current ---
    long current_adc_sum = 0;
    for (int i = 0; i < CURRENT_AVG_SAMPLES; i++) {
      current_adc_sum += analogRead(CURRENT_SENSOR_PIN);
    }
    int avg_current_adc = current_adc_sum / CURRENT_AVG_SAMPLES;
    
    float sensor_voltage = (avg_current_adc / 4095.0) * ADC_REFERENCE_VOLTAGE;
    
    // Ensure the current is positive for discharging
    float current = (zeroCurrentVoltage - sensor_voltage) / ACS712_SENSITIVITY;

    // --- DIAGNOSTIC: Dead zone temporarily disabled to see raw current change ---
    // if (abs(current) < CURRENT_DEAD_ZONE) {
    //   current = 0.0;
    // }

    // --- Coulomb Counting Calculation ---
    float chargeConsumed_Ah = current * deltaTime_hours;
    remainingCapacity_Ah -= chargeConsumed_Ah;

    // Clamp the value between 0 and the max capacity
    if (remainingCapacity_Ah < 0) remainingCapacity_Ah = 0;
    if (remainingCapacity_Ah > BATTERY_CAPACITY_Ah) remainingCapacity_Ah = BATTERY_CAPACITY_Ah;

    // --- Calculate SOC ---
    float soc = (remainingCapacity_Ah / BATTERY_CAPACITY_Ah) * 100.0;
    
    // --- Update the OLED Display ---
    display.clearDisplay(); // Clear the buffer to remove old data

    display.setTextSize(2); // Use a larger font for the main data
    display.setCursor(0, 0);

    // Display Voltage
    display.print("V: ");
    display.println(battery_voltage, 2);

    // Display Current
    display.print("I: ");
    display.println(current, 3);

    // Display State of Charge
    display.print("SOC:");
    display.print(soc, 1);
    display.println("%");

    display.display(); // Push the data from the buffer to the screen
  }
}

// This function can now be called anytime to re-zero the current sensor
void recalibrateCurrentSensor() {
  Serial.println("\n>>> Calibrating current sensor... Make sure no current is flowing. <<<");
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Calibrating...");
  display.display();
  
  long adc_sum = 0;
  // Use more samples for a more accurate calibration
  for (int i = 0; i < 1000; i++) { // Increased from 500
    adc_sum += analogRead(CURRENT_SENSOR_PIN);
    delay(1);
  }
  int avg_adc = adc_sum / 1000;
  zeroCurrentVoltage = (avg_adc / 4095.0) * ADC_REFERENCE_VOLTAGE;
  
  Serial.print("New Zero Current Voltage (Offset): ");
  Serial.println(zeroCurrentVoltage, 3);
  Serial.println(">>> Calibration complete. <<<\n");
}

