#include <EEPROM.h>

// Configuration
const int EEPROM_ADDR = 0;  // EEPROM address for our value
const unsigned int DEFAULT_VALUE = 100;  // Default if uninitialized
unsigned int persistentValue;  // Variable to persist

void setup() {
  Serial.begin(9600);
  
  // Read stored value from EEPROM
  EEPROM.get(EEPROM_ADDR, persistentValue);
  
  // Check if EEPROM is uninitialized (initial state: 0xFFFF)
  if (persistentValue == 0xFFFF) {
    persistentValue = DEFAULT_VALUE;
    EEPROM.put(EEPROM_ADDR, persistentValue);
    Serial.print("Initialized to default: ");
  } else {
    Serial.print("Current value: ");
  }
  Serial.println(persistentValue);
  Serial.println("Send new value (0-65534) via Serial...");
}

void loop() {
  if (Serial.available() > 0) {
    long input = Serial.parseInt();  // Read integer from Serial
    // Flush remaining characters (like newline)
    while (Serial.available()) Serial.read();

    // Validate input range
    if (input < 0) {
      Serial.println("Error: Negative value!");
    } else if (input > 65534) {
      Serial.println("Error: Max value is 65534!");
    } else {
      unsigned int newValue = (unsigned int)input;
      
      // Only update if value changed
      if (newValue != persistentValue) {
        persistentValue = newValue;
        EEPROM.put(EEPROM_ADDR, persistentValue);
        Serial.print("Updated to: ");
        Serial.println(persistentValue);
      } else {
        Serial.println("Value unchanged.");
      }
    }
  }
}
