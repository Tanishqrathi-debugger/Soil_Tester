#include <WiFi.h>
#include <WebServer.h>

// --- WiFi Credentials ---
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// --- Sensor Settings ---
const int MOISTURE_SENSOR_PIN = 34; // Use an ADC-enabled pin like GPIO 34
const int WET_VALUE = 1600;        // Calibrated sensor reading when in water (or fully saturated)
const int DRY_VALUE = 3900;        // Calibrated sensor reading when completely dry (in air)
const int MOISTURE_ALERT_THRESHOLD = 30; // Alert if moisture is below 30%

// --- Web Server Setup ---
WebServer server(80);

int moisturePercentage = 0;

// Function to read and calculate moisture percentage
void readMoisture() {
  int rawValue = analogRead(MOISTURE_SENSOR_PIN);
  
  // Map the raw sensor reading to a percentage (0-100)
  // map(value, fromLow, fromHigh, toLow, toHigh)
  int mappedValue = map(rawValue, DRY_VALUE, WET_VALUE, 0, 100);
  
  // Constrain the value to be within 0 and 100
  moisturePercentage = constrain(mappedValue, 0, 100);
  
  Serial.print("Raw Value: ");
  Serial.print(rawValue);
  Serial.print(" | Moisture %: ");
  Serial.println(moisturePercentage);
}

// Function to serve the main HTML page
void handleRoot() {
  readMoisture(); // Read sensor before generating the page
  
  String html = "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='5'>";
  html += "<title>ESP32 Soil Moisture Monitor</title>";
  html += "<style>body{text-align: center; font-family: sans-serif;} h1{color: #007BFF;} .alert{color: red; font-weight: bold;} .ok{color: green; font-weight: bold;}</style>";
  html += "</head><body>";
  
  // Display the current moisture value
  html += "<h1>Soil Moisture Level</h1>";
  html += "<h2>" + String(moisturePercentage) + "%</h2>";
  
  // Display the alert if moisture is low
  if (moisturePercentage < MOISTURE_ALERT_THRESHOLD) {
    html += "<p class='alert'>🚨 LOW MOISTURE ALERT: Below " + String(MOISTURE_ALERT_THRESHOLD) + "%! Please water the plant.</p>";
  } else {
    html += "<p class='ok'>✅ Moisture is Optimal.</p>";
  }

  html += "<p><em>Last Updated: " + String(millis()/1000) + "s ago</em></p>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  pinMode(MOISTURE_SENSOR_PIN, INPUT);

  // --- Connect to WiFi ---
  WiFi.begin(ssid, password);
  Serial.println("\nConnecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi connected.");
  Serial.print("Web Server IP address: ");
  Serial.println(WiFi.localIP());

  // --- Start Web Server ---
  server.on("/", handleRoot);
  server.begin();
}

void loop() {
  server.handleClient();
  // The sensor reading and page refresh are handled inside handleRoot()
}