/* ESP32 pH monitor: reads analog from pH sensor module and serves a webpage.
   Webpage polls /ph every 2 seconds and draws a small history chart.

   Wiring:
     Module VCC -> 3.3V
     Module GND -> GND
     Module AOUT -> GPIO 34 (or any ADC1 pin)

   IMPORTANT: Calibrate neutralVoltage and slope for your hardware (see notes below).
*/

#include <WiFi.h>
#include <WebServer.h>

const char* ssid     = "WIFI_JAM";
const char* password = "12345678";

WebServer server(80);

// ---------- ADC & calibration settings ----------
const int PH_PIN = 34;            // ADC pin
const int ADC_WIDTH = 12;         // 12-bit: 0-4095
const int ADC_MAX = (1 << ADC_WIDTH) - 1;
const float VREF = 3.3;           // ESP32 reference (3.3V)

float neutralVoltage = 2.50;      // voltage at pH 7.0 (example) — calibrate this
float slopeVoltagePerPH = 0.177;  // expected V change per pH unit (module dependent) ~0.177 -> if amplifier gain ~3
                                   // pH = 7 + (neutralVoltage - measuredVoltage) / slopeVoltagePerPH

// Basic smoothing
const int SAMPLE_COUNT = 8;

// ---------- web page (client) ----------
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html>
<head>
<meta charset="utf-8">
<title>ESP32 pH Monitor</title>
<style>
  body{font-family: Arial, Helvetica, sans-serif; text-align:center; margin:20px;}
  #phVal{font-size:2.4rem; margin:10px;}
  canvas{border:1px solid #ddd; width:90%; max-width:600px; height:160px;}
</style>
</head>
<body>
  <h2>ESP32 pH Monitor</h2>
  <div>pH: <span id="phVal">--</span></div>
  <div>Voltage: <span id="voltVal">--</span> V</div>
  <canvas id="chart"></canvas>
<script>
let history = [];
const maxPoints = 40;
const canvas = document.getElementById('chart');
const ctx = canvas.getContext('2d');

function fetchPH(){
  fetch('/ph').then(r=>r.json()).then(j=>{
    document.getElementById('phVal').innerText = j.pH.toFixed(2);
    document.getElementById('voltVal').innerText = j.voltage.toFixed(3);
    history.push(j.pH);
    if(history.length>maxPoints) history.shift();
    drawChart();
  }).catch(err=>console.error(err));
}

function drawChart(){
  const w = canvas.clientWidth;
  const h = canvas.clientHeight;
  canvas.width = w;
  canvas.height = h;
  ctx.clearRect(0,0,w,h);
  if(history.length<2) return;
  // find min/max
  let min = Math.min(...history);
  let max = Math.max(...history);
  // pad
  if(max - min < 0.5){ max += 0.25; min -= 0.25; }
  ctx.beginPath();
  history.forEach((v,i)=>{
    const x = i*(w/(maxPoints-1));
    const y = h - ((v - min)/(max-min))*h;
    if(i===0) ctx.moveTo(x,y); else ctx.lineTo(x,y);
  });
  ctx.stroke();
  // draw baseline pH 7 line if in range
  if(7>=min && 7<=max){
    const y7 = h - ((7-min)/(max-min))*h;
    ctx.setLineDash([4,4]);
    ctx.beginPath(); ctx.moveTo(0,y7); ctx.lineTo(w,y7); ctx.stroke();
    ctx.setLineDash([]);
  }
}

setInterval(fetchPH, 2000);
fetchPH();
</script>
</body>
</html>
)rawliteral";

// ---------- server handlers ----------
void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handlePH() {
  // read smoothed ADC
  long sum = 0;
  for(int i=0;i<SAMPLE_COUNT;i++){
    sum += analogRead(PH_PIN);
    delay(5);
  }
  float raw = (float)sum / SAMPLE_COUNT;
  float voltage = (raw / ADC_MAX) * VREF;
  float pH = 7.0 + (neutralVoltage - voltage) / slopeVoltagePerPH;

  // Clip pH to reasonable range
  if(pH < 0.0) pH = 0.0;
  if(pH > 14.0) pH = 14.0;

  String json = "{\"pH\":" + String(pH,3) + ",\"voltage\":" + String(voltage,4) + "}";
  server.send(200, "application/json", json);
}

void setup() {
  Serial.begin(115200);
  delay(100);

  // ADC settings
  analogReadResolution(ADC_WIDTH);            // 12-bit
  // Give full range (0-3.3V) if using ADC1 pin - set attenuation
  analogSetPinAttenuation(PH_PIN, ADC_11db); // ADC_0db/2.5db/6db/11db -> 11db gives ~0-3.3V range

  // WiFi
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to WiFi '%s' ...\n", ssid);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(300);
    Serial.print('.');
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connect failed. AP mode fallback not implemented here.");
  }

  server.on("/", handleRoot);
  server.on("/ph", handlePH);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}