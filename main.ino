#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP8266HTTPClient.h>
#include <LittleFS.h>
#include <time.h>

// ================= CONFIG =================
const char* ssid     = "PUT_YOUR_SSID_HERE";
const char* password = "PUT_YOUR_PASSWORD_HERE";

const int ledGreen = 14; // D5
const int ledRed   = 12; // D6

const unsigned long CHECK_INTERVAL = 30000; // Every 30sec it will check the uptime. You can change this to whatever you like. It is in miliseconds so do the math.
const int MAX_URLS = 15;
const int RETRIES  = 3;
const int HTTP_TIMEOUT = 10000;
// =========================================

AsyncWebServer server(80);

// DATA / money 
struct SiteStatus {
  String url;
  int httpCode;
  bool up;
  String error;
  unsigned long responseTime;
};

SiteStatus sites[MAX_URLS];
int siteCount = 0;

unsigned long lastCheckMillis = 0;
String lastCheckTime = "Never";
bool isChecking = false;

// Serial command buffer
String serialCommand = "";

// FILE SYSTEMmmm...
void saveConfig() {
  File file = LittleFS.open("/list.txt", "w");
  if (!file) return;
  for (int i = 0; i < siteCount; i++) file.println(sites[i].url);
  file.close();
}

void loadConfig() {
  siteCount = 0;
  if (!LittleFS.exists("/list.txt")) return;

  File file = LittleFS.open("/list.txt", "r");
  while (file.available() && siteCount < MAX_URLS) {
    String url = file.readStringUntil('\n');
    url.trim();
    if (url.length() > 5) {
      sites[siteCount].url = url;
      sites[siteCount].up = false;
      sites[siteCount].httpCode = 0;
      sites[siteCount].error = "Not checked";
      sites[siteCount].responseTime = 0;
      siteCount++;
    }
  }
  file.close();
}

// TIME FORMATTING
String getFormattedTime() {
  time_t now = time(nullptr);
  struct tm timeinfo;
  gmtime_r(&now, &timeinfo);
  
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S UTC", &timeinfo);
  return String(buffer);
}

// 67

// ERROR HANDLING :( and :)
String errorFromCode(int code) {
  if (code >= 200 && code < 400) return "OK";
  if (code == 0) return "Connection failed";
  if (code == -1) return "Connection refused";
  if (code == -2) return "Send failed";
  if (code == -3) return "Receive failed";
  if (code == -4) return "Timeout";
  if (code == 400) return "Bad Request";
  if (code == 401) return "Unauthorized";
  if (code == 403) return "Forbidden";
  if (code == 404) return "Not Found";
  if (code == 500) return "Server Error";
  if (code == 502) return "Bad Gateway";
  if (code == 503) return "Service Unavailable";
  if (code > 0) return "HTTP error";
  return "Unknown error";
}

// SITE CHECK Suiiiiii 
void checkSites() {
  isChecking = true;
  WiFiClient client;
  HTTPClient http;
  bool allUp = true;

  for (int i = 0; i < siteCount; i++) {
    bool success = false;
    int code = 0;
    unsigned long startTime = millis();

    for (int attempt = 0; attempt < RETRIES; attempt++) {
      http.setTimeout(HTTP_TIMEOUT);
      http.useHTTP10(true);
      http.begin(client, sites[i].url);
      
      startTime = millis();
      code = http.GET();
      sites[i].responseTime = millis() - startTime;
      
      http.end();

      if (code >= 200 && code < 400) {
        success = true;
        break;
      }
      delay(200);
    }

    sites[i].httpCode = code;
    sites[i].up = success;
    sites[i].error = errorFromCode(code);

    if (!success) allUp = false;
  }
// no way you are reading this
  digitalWrite(ledGreen, allUp ? HIGH : LOW);
  digitalWrite(ledRed,   allUp ? LOW  : HIGH);

  lastCheckTime = getFormattedTime();
  isChecking = false;
}

// ================= SERIAL =================
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {
      serialCommand.trim();
      if (serialCommand.equalsIgnoreCase("ip")) {
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
      } else if (serialCommand.equalsIgnoreCase("check")) {
        Serial.println("Running manual check...");
        checkSites();
        Serial.println("Check complete!");
      }
      serialCommand = "";
    } else {
      serialCommand += c;
    }
  }
}

// '.''.'.''.'.'..'> WEB UIII <.'.'..'.''.''.'
String buildPage() {
  String html;
  html += R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta charset="UTF-8">
<title>ESP8266 Uptime Monitor</title>
<style>
* {
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}

body {
  font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, sans-serif;
  background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
  color: #e4e4e7;
  min-height: 100vh;
  padding: 20px;
}

.container {
  max-width: 1100px;
  margin: 0 auto;
  background: #1f2937;
  padding: 30px;
  border-radius: 12px;
  box-shadow: 0 8px 32px rgba(0, 0, 0, 0.4);
}

.header {
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 25px;
  padding-bottom: 20px;
  border-bottom: 2px solid #374151;
}

h1 {
  font-size: 28px;
  color: #60a5fa;
  font-weight: 600;
}

.status-badge {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 8px 16px;
  background: #374151;
  border-radius: 8px;
  font-size: 14px;
}

.status-indicator {
  width: 10px;
  height: 10px;
  border-radius: 50%;
  animation: pulse 2s infinite;
}

.status-indicator.checking {
  background: #fbbf24;
}

.status-indicator.ready {
  background: #10b981;
}

@keyframes pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.5; }
}

.info-bar {
  display: flex;
  gap: 20px;
  margin-bottom: 25px;
  flex-wrap: wrap;
}

.info-card {
  flex: 1;
  min-width: 200px;
  background: #374151;
  padding: 15px;
  border-radius: 8px;
  border-left: 4px solid #60a5fa;
}

.info-label {
  font-size: 12px;
  color: #9ca3af;
  text-transform: uppercase;
  letter-spacing: 0.5px;
  margin-bottom: 5px;
}

.info-value {
  font-size: 18px;
  font-weight: 600;
  color: #e4e4e7;
}

.add-section {
  background: #374151;
  padding: 20px;
  border-radius: 8px;
  margin-bottom: 25px;
}

.add-section h2 {
  font-size: 18px;
  margin-bottom: 15px;
  color: #e4e4e7;
}

textarea {
  width: 100%;
  padding: 12px;
  font-size: 14px;
  background: #1f2937;
  border: 1px solid #4b5563;
  border-radius: 6px;
  color: #e4e4e7;
  font-family: 'Courier New', monospace;
  resize: vertical;
}

textarea:focus {
  outline: none;
  border-color: #60a5fa;
}

textarea::placeholder {
  color: #6b7280;
}

button {
  margin-top: 10px;
  padding: 10px 20px;
  border: none;
  border-radius: 6px;
  background: linear-gradient(135deg, #3b82f6 0%, #2563eb 100%);
  color: white;
  cursor: pointer;
  font-size: 14px;
  font-weight: 500;
  transition: all 0.3s ease;
}

button:hover {
  transform: translateY(-2px);
  box-shadow: 0 4px 12px rgba(59, 130, 246, 0.4);
}

button:active {
  transform: translateY(0);
}

.table-wrapper {
  overflow-x: auto;
  background: #374151;
  border-radius: 8px;
}

table {
  width: 100%;
  border-collapse: collapse;
}

thead {
  background: #1f2937;
}

th {
  padding: 15px;
  text-align: left;
  font-size: 13px;
  font-weight: 600;
  color: #9ca3af;
  text-transform: uppercase;
  letter-spacing: 0.5px;
}

td {
  padding: 15px;
  border-top: 1px solid #4b5563;
}

tr:hover {
  background: #2d3748;
}

.url {
  font-family: 'Courier New', monospace;
  font-size: 13px;
  color: #60a5fa;
  word-break: break-all;
}

.status-up {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #10b981;
  font-weight: 600;
  font-size: 13px;
}

.status-down {
  display: inline-flex;
  align-items: center;
  gap: 6px;
  color: #ef4444;
  font-weight: 600;
  font-size: 13px;
}

.status-up::before {
  content: "●";
  font-size: 18px;
}

.status-down::before {
  content: "●";
  font-size: 18px;
}

.http-code {
  font-family: 'Courier New', monospace;
  font-weight: 600;
  padding: 4px 8px;
  border-radius: 4px;
  font-size: 12px;
}

.http-success {
  background: #064e3b;
  color: #6ee7b7;
}

.http-error {
  background: #7f1d1d;
  color: #fca5a5;
}

.response-time {
  color: #9ca3af;
  font-size: 13px;
}

a.remove {
  color: #ef4444;
  text-decoration: none;
  font-weight: 600;
  font-size: 13px;
  transition: color 0.3s ease;
}

a.remove:hover {
  color: #dc2626;
}

.footer {
  margin-top: 25px;
  padding-top: 20px;
  border-top: 1px solid #374151;
  text-align: center;
  font-size: 12px;
  color: #6b7280;
}

.empty-state {
  text-align: center;
  padding: 60px 20px;
  color: #9ca3af;
}

.empty-state svg {
  width: 64px;
  height: 64px;
  margin-bottom: 16px;
  opacity: 0.5;
}

@media (max-width: 768px) {
  .container {
    padding: 20px;
  }
  
  h1 {
    font-size: 22px;
  }
  
  .header {
    flex-direction: column;
    align-items: flex-start;
    gap: 15px;
  }
  
  .info-bar {
    flex-direction: column;
  }
  
  table {
    font-size: 13px;
  }
  
  th, td {
    padding: 10px 8px;
  }
}
</style>
</head>
<body>
<div class="container">

<div class="header">
  <h1>🐏 Uptime Monitor</h1>
  <div class="status-badge">
    <div class="status-indicator )rawliteral";
  
  html += isChecking ? "checking" : "ready";
  
  html += R"rawliteral("></div>
    <span>)rawliteral";
  
  html += isChecking ? "Checking..." : "Ready";
  
  html += R"rawliteral(</span>
  </div>
</div>

<div class="info-bar">
  <div class="info-card">
    <div class="info-label">Monitored Sites</div>
    <div class="info-value">)rawliteral";
  
  html += String(siteCount);
  
  html += R"rawliteral(</div>
  </div>
  <div class="info-card">
    <div class="info-label">Last Check</div>
    <div class="info-value">)rawliteral";
  
  html += lastCheckTime;
  
  html += R"rawliteral(</div>
  </div>
  <div class="info-card">
    <div class="info-label">Check Interval</div>
    <div class="info-value">)rawliteral";
  
  html += String(CHECK_INTERVAL / 1000) + "s";
  
  html += R"rawliteral(</div>
  </div>
</div>

<div class="add-section">
  <h2>Add URLs to Monitor</h2>
  <form action="/add" method="POST">
    <textarea name="urls" rows="3" placeholder="http://192.168.x.x:xxxx
http://site.com
Or comma-separated: http://site1.com, http://site2.com"></textarea>
    <button type="submit">🧇 Add URLs</button>
  </form>
</div>

<div class="table-wrapper">
)rawliteral";

  if (siteCount == 0) {
    html += R"rawliteral(
<div class="empty-state">
  <svg fill="none" stroke="currentColor" viewBox="0 0 24 24">
    <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z"></path>
  </svg>
  <p>No URLs being monitored yet.<br>Add some URLs above to get started!</p>
</div>
)rawliteral";
  } else {
    html += R"rawliteral(
<table>
<thead>
<tr>
  <th>URL</th>
  <th>Status</th>
  <th>HTTP Code</th>
  <th>Response Time</th>
  <th>Details</th>
  <th>Action</th>
</tr>
</thead>
<tbody>
)rawliteral";

    for (int i = 0; i < siteCount; i++) {
      html += "<tr>";
      html += "<td class='url'>" + sites[i].url + "</td>";
      
      html += "<td>";
      html += sites[i].up
        ? "<span class='status-up'>UP</span>"
        : "<span class='status-down'>DOWN</span>";
      html += "</td>";
      
      html += "<td><span class='http-code ";
      html += (sites[i].httpCode >= 200 && sites[i].httpCode < 400) ? "http-success" : "http-error";
      html += "'>" + String(sites[i].httpCode) + "</span></td>";
      
      html += "<td class='response-time'>";
      if (sites[i].responseTime > 0) {
        html += String(sites[i].responseTime) + " ms";
      } else {
        html += "—";
      }
      html += "</td>";
      
      html += "<td>" + sites[i].error + "</td>";
      html += "<td><a class='remove' href='/remove?id=" + String(i) + "'>✕ Remove</a></td>";
      html += "</tr>";
    }

    html += R"rawliteral(
</tbody>
</table>
)rawliteral";
  }

  html += R"rawliteral(
</div>

<div class="footer">
  ESP8266 Uptime Monitor • HTTP Monitoring • 
  <span style="color: #10b981;">●</span> Green LED = All Up • 
  <span style="color: #ef4444;">●</span> Red LED = Issues Detected
  <span style="color: gray;">#</span> #Under Work.
</div>

</div>

<script>
// Auto-refresh every 30 seconds
setTimeout(() => location.reload(), 30000);
</script>

</body>
</html>
)rawliteral";

  return html;
}

// SETUP
void setup() {
  Serial.begin(115200);
  Serial.println("\nESP8266 Uptime Monitor - Starting...");

  pinMode(ledGreen, OUTPUT);
  pinMode(ledRed, OUTPUT);
  
  // Initial LED state
  digitalWrite(ledGreen, LOW);
  digitalWrite(ledRed, LOW);

  // Initialize filesystem
  if (!LittleFS.begin()) {
    Serial.println("LittleFS Mount Failed");
  } else {
    Serial.println("LittleFS Mounted Successfully");
  }
  
  loadConfig();
  Serial.printf("Loaded %d URLs from config\n", siteCount);

  // Connect to WiFi
  Serial.printf("Connecting to WiFi: %s\n", ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    
    // Configure time
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("Waiting for time sync...");
    
    int timeAttempts = 0;
    while (time(nullptr) < 100000 && timeAttempts < 10) {
      delay(500);
      timeAttempts++;
    }
    
    if (time(nullptr) > 100000) {
      Serial.println("Time synced successfully");
    }
  } else {
    Serial.println("\nWiFi Connection Failed!");
  }

  // Setup web server routes
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", buildPage());
  });

  server.on("/add", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("urls", true)) {
      request->redirect("/");
      return;
    }

    String input = request->getParam("urls", true)->value();
    input.replace('\n', ',');
    input.replace('\r', ',');

    int start = 0;
    while (siteCount < MAX_URLS) {
      int end = input.indexOf(',', start);
      String url = (end == -1) ? input.substring(start) : input.substring(start, end);
      url.trim();

      if (url.length() > 5 && (url.startsWith("http://"))) {
        // Check for duplicates
        bool duplicate = false;
        for (int i = 0; i < siteCount; i++) {
          if (sites[i].url == url) {
            duplicate = true;
            break;
          }
        }
        
        if (!duplicate) {
          sites[siteCount].url = url;
          sites[siteCount].up = false;
          sites[siteCount].httpCode = 0;
          sites[siteCount].error = "Not checked";
          sites[siteCount].responseTime = 0;
          siteCount++;
        }
      }
      
      if (end == -1) break;
      start = end + 1;
    }

    saveConfig();
    request->redirect("/");
  });
  // y r u rdig tis?

  server.on("/remove", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("id")) {
      request->redirect("/");
      return;
    }

    int id = request->getParam("id")->value().toInt();
    if (id >= 0 && id < siteCount) {
      for (int i = id; i < siteCount - 1; i++) {
        sites[i] = sites[i + 1];
      }
      siteCount--;
      saveConfig();
    }
    request->redirect("/");
  });

  server.begin();
  Serial.println("Web server started");
  Serial.println("Type 'ip' for IP address or 'check' to run manual check");
  
  // Run initial check after a short delay
  delay(2000);
  checkSites();
}

// LOOP
void loop() {
  handleSerial();

  if (millis() - lastCheckMillis >= CHECK_INTERVAL) {
    lastCheckMillis = millis();
    checkSites();
    Serial.println("Periodic check completed");
  }
}

// that way looooong af.

// Made with blood, sweat and tears by yr daddy. (jk)

// Written by AI. (Actual Intel.... *shutdown