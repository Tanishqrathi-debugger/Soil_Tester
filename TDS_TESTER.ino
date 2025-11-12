/* ESP32 TDS real-time server
   Serves a webpage and a JSON endpoint returning latest TDS reading.

   Notes:
   - Uses ADC1 channel (GPIO34).
   - Adjust calibrationFactor after calibration with known solution.
   - Use analogSetPinAttenuation(pin, ADC_11db) to allow full-scale ~0-3.3V reading.
*/

#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "WIFI_JAM";
const char* password = "12345678";

WebServer server(80);

// --- TDS sensor settings ---
const int TDS_PIN = 35;         // ADC1_CH6 (input-only)
const int ADC_BITS = 4095;      // 12-bit ADC
const float VREF = 3.3;         // approximate ESP32 reference with 11dB attenuation
const float calibrationFactor = 0.5; // start ~0.5 - adjust after calibration

// smoothing
const int SMOOTH_N = 8;
float smoothingBuffer[SMOOTH_N];
int smoothingIndex = 0;
bool smoothingFilled = false;

unsigned long lastReadMs = 0;
float lastVoltage = 0.0;
float lastTDS = 0.0;

String webpage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <title>ESP32 TDS Monitor</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 20px; }
    .card { border-radius: 8px; padding: 16px; box-shadow: 0 2px 6px rgba(0,0,0,0.12); max-width:500px; }
    #value { font-size: 2.6em; margin:10px 0; }
    #chart { width:100%; height:120px; border:1px solid #ddd; }
  </style>
</head>
<body>
  <div class="card">
    <h2>ESP32 TDS Monitor</h2>
    <div>Live TDS (ppm): <div id="value">--</div></div>
    <canvas id="chart"></canvas>
    <div style="font-size:0.9em;color:#666;margin-top:8px">Voltage: <span id="voltage">--</span> V · Raw: <span id="raw">--</span></div>
  </div>

<script>
let values = [];
const maxPoints = 60;

function addPoint(v) {
  values.push(v);
  if (values.length > maxPoints) values.shift();
  draw();
}

function draw() {
  const canvas = document.getElementById('chart');
  canvas.width = canvas.clientWidth;
  canvas.height = canvas.clientHeight;
  const ctx = canvas.getContext('2d');
  ctx.clearRect(0,0,canvas.width,canvas.height);
  if (values.length === 0) return;
  const w = canvas.width, h = canvas.height;
  const max = Math.max(...values) * 1.1;
  const min = Math.min(...values);
  ctx.beginPath();
  for (let i=0;i<values.length;i++){
    const x = (i / (maxPoints-1)) * w;
    const y = h - ((values[i] - min) / ( (max-min)||1 )) * h;
    if (i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
  }
  ctx.strokeStyle = '#007acc';
  ctx.lineWidth = 2;
  ctx.stroke();
}

async function fetchTDS(){
  try {
    const res = await fetch('/tds');
    const j = await res.json();
    document.getElementById('value').textContent = j.tds.toFixed(1);
    document.getElementById('voltage').textContent = j.voltage.toFixed(3);
    document.getElementById('raw').textContent = j.raw;
    addPoint(j.tds);
  } catch(e){
    console.log('fetch error', e);
  }
}

setInterval(fetchTDS, 1000);
window.addEventListener('resize', draw);
fetchTDS();
</script>
</body>
</html>
)rawliteral";

void handleRoot(){
  server.send(200, "text/html", webpage);
}

void handleTDS(){
  // return last reading as JSON
  String json = "{";
  json += "\"tds\":" + String(lastTDS, 2) + ",";
  json += "\"voltage\":" + String(lastVoltage, 3) + ",";
  json += "\"raw\":" + String((int)analogRead(TDS_PIN)) + ",";
  json += "\"time\":" + String(millis());
  json += "}";
  server.send(200, "application/json", json);
}

// read ADC, smooth, convert to voltage and TDS (ppm) using commonly used polynomial
void readTDS(){
  int raw = analogRead(TDS_PIN);         // 0 - 4095
  // smoothing buffer
  smoothingBuffer[smoothingIndex++] = raw;
  if (smoothingIndex >= SMOOTH_N) { smoothingIndex = 0; smoothingFilled = true; }

  float sum = 0; int count = smoothingFilled ? SMOOTH_N : smoothingIndex;
  for (int i=0;i<count;i++) sum += smoothingBuffer[i];
  float avgRaw = (count>0) ? (sum / count) : raw;

  float voltage = (avgRaw / (float)ADC_BITS) * VREF; // voltage in volts (approx)
  lastVoltage = voltage;

  // Convert voltage -> TDS (ppm) using standard polynomial used with Gravity TDS sensor (approx)
  // Note: This polynomial expects voltage measured across the probe after proper scaling.
  // Typical polynomial (example): tds(ppm) = (133.42 * V^3 - 255.86 * V^2 + 857.39 * V) * calibrationFactor
  float v = voltage;
  float tdsVal = (133.42 * v * v * v - 255.86 * v * v + 857.39 * v) * calibrationFactor;

  // safety clamp
  if (tdsVal < 0) tdsVal = 0;

  lastTDS = tdsVal;
}

void setup(){
  Serial.begin(115200);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 25){
    delay(400);
    Serial.print(".");
    tries++;
  }
  if (WiFi.status() == WL_CONNECTED){
    Serial.println();
    Serial.print("Connected! IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("WiFi connect failed - AP fallback not implemented. Check credentials.");
  }

  // ADC pin config
  analogSetPinAttenuation(TDS_PIN, ADC_11db); // allow reading up to ~3.3V
  analogReadResolution(12); // 12-bit (0-4095)

  // zero smoothing buffer
  for (int i=0;i<SMOOTH_N;i++) smoothingBuffer[i] = 0;

  // web endpoints
  server.on("/", handleRoot);
  server.on("/tds", handleTDS);
  server.begin();
  Serial.println("HTTP server started");
  lastReadMs = millis();
}

void loop(){
  unsigned long now = millis();
  // sample every 400 ms
  if (now - lastReadMs >= 400) {
    readTDS();
    lastReadMs = now;
  }
  server.handleClient();
}