#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <Adafruit_Fingerprint.h>

#define LED_PIN 2
#define RELAY_PIN 15
#define FP_TX_PIN 4
#define FP_RX_PIN 5
#define PORT 4848
#define DELAY_MILLIS 1500
#define BAUD_RATE 115200
#define VERSION "1.0"


static const String PATH = "v1";

String _apSsid = "DigiPiratePro";
String _apPass = "";

String _wifiSsid = "VinStudios Network";
String _wifiPass = "xoxoxoxox";

bool _ledState = false;
bool _relayState = false;
unsigned long _previousMillis = 0;
unsigned long _sensorMillis = 0;


uint8_t _clientId = 0;
int _fingerId = 0;

String _pass = "1234";
String _token = "";
bool _sensorIsAvailable = false;
bool _sensorIsEnable = true;

SoftwareSerial serial(FP_TX_PIN, FP_RX_PIN);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serial);
AsyncWebServer server(PORT);
AsyncWebSocket ws("/" + PATH);

static const struct SESSION {
  static const uint8_t init_main = 0;
  static const uint8_t init_wifi = 1;
  static const uint8_t ready = 2;
  static const uint8_t sensor_verify = 3;
  static const uint8_t sensor_info = 4;
  static const uint8_t sensor_download = 5;
  static const uint8_t sensor_enroll = 6;
  static const uint8_t sensor_delete = 7;
} SESSION;

uint8_t _session = SESSION.init_main;

String json(String status, String type, String message, String data = "\"\"") {
  return "{\"status\": \"" + status + "\", \"type\": \"" + type + "\", \"message\": \"" + message + "\", \"data\": " + data + "}";
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, uint8_t id) {

  AwsFrameInfo *info = (AwsFrameInfo*)arg;

  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT) {

    data[len] = 0;
    String msg = (char*)data;
    int index = msg.indexOf("=");

    Serial.println(msg);

    if (index == -1) {
      ws.text(id, json("error", "unknown", "invalid data", msg));
      return;
    }

    String type = msg.substring(0, index);
    String param = msg.substring(index + 1);

    if (type == "login") {
      login(id, param);
    } else if (type == "sensor" ) {
      sensor(param);

    } else if (type == "relay") {
      relay(param);
    } else if (type == "esp") {
      esp(param);
    } else {
      ws.text(id, json("error", type, "request not found", "\"" + param + "\""));
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
  switch (type) {
    case WS_EVT_CONNECT:
      Serial.printf("Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
      ws.text(client->id(), json("success", "connect", "connected"));
      break;
    case WS_EVT_DISCONNECT:
      if (client->id() == _clientId) {
        _clientId = 0;
        _token = "";
        _session = SESSION.ready;
      }
      Serial.print("Client id: "); Serial.println(_clientId);
      Serial.printf("Client #%u disconnected\n", client->id());
      break;
    case WS_EVT_DATA:
      handleWebSocketMessage(arg, data, len, client->id());
      break;
    case WS_EVT_PONG:
      break;
    case WS_EVT_ERROR:
      break;
  }
}

void setup() {

  delay(2000);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  Serial.begin(BAUD_RATE);

  finger.begin(57600);
  _sensorIsAvailable = finger.verifyPassword();

  if (_sensorIsAvailable) {
    Serial.println("Found sensor!");
    // Serial.println(F("Reading fingerprint sensor parameters"));
    // finger.getParameters();
    // Serial.println("Getting fingerprint template counts");
    // finger.getTemplateCount();
    // Serial.println("Sensor initialize done!");
  } else {
    Serial.println("Sensor is nor available");
  }

  if (SPIFFS.begin()) {
    Serial.println("SPIFFS mounted");
    _pass = getConfig("/pass.txt", _pass);
    _wifiSsid = getConfig("/wifi_ssid.txt", _wifiSsid);
    _wifiPass = getConfig("/wifi_pass.txt", _wifiPass);
  }

  
  Serial.print("Pass: "); Serial.println(_pass);
  // Serial.print("Wifi SSID: "); Serial.println(_wifiSsid);
  // Serial.print("Wifi Pass: "); Serial.println(_wifiPass);

   WiFi.mode(WIFI_AP_STA);
   WiFi.softAP(_apSsid, _apPass);
   Serial.print("AP ip address: "); Serial.println(WiFi.softAPIP());

  // if (_wifiSsid != "") {
  //   Serial.print("Connecting to wifi ");
  //   WiFi.begin(_wifiSsid, _wifiPass);
  //   while (WiFi.status() != WL_CONNECTED) {
  //     delay(300);
  //     Serial.print(".");
  //   }
  //   Serial.println();
  //   Serial.print("Connected! IP address: ");
  //   Serial.println(WiFi.localIP());
  // }

  ws.onEvent(onEvent);
  server.addHandler(&ws);
  server.begin();
  _session = SESSION.ready;
  Serial.println("Ready!");
  Serial.println();
}

void loop() {

  unsigned long ms = millis();
  if (ms - _previousMillis >= 1000) {
    
    if (_sensorIsAvailable) {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState);
    }

    _previousMillis = ms;
  }

  if (ms - _sensorMillis >= 150) {
    if (!_sensorIsAvailable) {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState);
    }
    _sensorMillis = ms;
  }

  switch (_session) {
    case SESSION.ready:
      if (_sensorIsAvailable && _sensorIsEnable) {
        listenToSensor();
      }
      break;

    case SESSION.sensor_download:
      delay(DELAY_MILLIS);
      downloadFingerprintTemplate();
      break;

    case SESSION.sensor_verify:
      delay(DELAY_MILLIS);
      _sensorIsAvailable = finger.verifyPassword();
      ws.text(_clientId, json("success", "sensor", "available", _sensorIsAvailable ? "\"1\"" : "\"0\""));
      _session = SESSION.ready;
      break;

    case SESSION.sensor_info:
      delay(DELAY_MILLIS);
      getSensorInfo();
      break;

    case SESSION.sensor_enroll:
      delay(DELAY_MILLIS);
      while (!enrollFingerprint()) {
        if (_clientId == 0) {
          break;
        }
      }
      _session = SESSION.ready;
      break;

     case SESSION.sensor_delete:
      delay(DELAY_MILLIS);
      deleteFingerprint();
      break; 

    default:
      break;
  }

  //  ws.cleanupClients();

}

void listenToSensor() {
  uint8_t p = finger.getImage();
  switch (p) {
    case FINGERPRINT_OK: Serial.println("Image taken"); break;
    case FINGERPRINT_NOFINGER:
      return;
    case FINGERPRINT_PACKETRECIEVEERR:
//            Serial.println("Communication error 1");
      return;
    case FINGERPRINT_IMAGEFAIL: Serial.println("Imaging error");
      return;
    default: Serial.println("Unknown error 1"); return;
  }

  // OK success!

  p = finger.image2Tz();
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy");
      return;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error 2");
      return;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features");
      return;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features");
      return;
    default:
      Serial.println("Unknown error 2");
      return;
  }

  // OK converted!
  p = finger.fingerSearch();
  if (p == FINGERPRINT_OK) {
    Serial.println("Found a print match!");
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    return;
  } else if (p == FINGERPRINT_NOTFOUND) {
    Serial.println("Did not find a match");
    return;
  } else {
    Serial.println("Unknown error");
    return;
  }

  // found a match!
  Serial.print("Found ID #"); Serial.print(finger.fingerID);
  Serial.print(" with confidence of "); Serial.println(finger.confidence);
  _relayState =!_relayState;
  digitalWrite(RELAY_PIN, _relayState);
  String data = _relayState ? "1" : "0";
  if (_clientId != 0) {
    ws.text(_clientId, json("success", "relay", "state", "\"" + data + "\""));
  }
  delay(1000);
  return;// finger.fingerID;
}

bool enrollFingerprint() {

  ws.text(_clientId, json("success", "sensor", "enroll", "\"enrolling\""));
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Place your finger to the sensor\""));
  int p = -1;
  Serial.print("Enrolling fingerprint for ID #"); Serial.println(_fingerId);
  Serial.println("Please place your finger");
  while (p != FINGERPRINT_OK && _clientId != 0 && _session  == SESSION.sensor_enroll) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprint scanned OK\""));
        break;
      case FINGERPRINT_NOFINGER:
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error 1");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error 1");
        break;
      default:
        Serial.println("Unknown error 1");
        break;
    }
  }

  if (_clientId == 0 || _session != SESSION.sensor_enroll) {
    return true;
  }

  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Reading fingerprint...\""));
  delay(500);
  //   OK success!
  
  p = finger.image2Tz(1);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprint read OK\""));
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy 2");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Image too messy\""));
      return true;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error 2");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Communication errory\""));
      return true;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features 2");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Could not find fingerprint features\""));
      return true;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features 2");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Could not find fingerprint features\""));
      return true;
    default:
      Serial.println("Unknown error 2");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Unknown error\""));
      return true;
  }

  //  delay(1000);
  //  Serial.println("Remove finger");
  //  ws.text(_clientId, json("success", "sensor", "enroll", "\"Remove your finger to the sensor\""));
  delay(500);

  p = 0;
  while (p != FINGERPRINT_NOFINGER && _clientId != 0 && _session == SESSION.sensor_enroll) {
    p = finger.getImage();
  }

  if (_clientId == 0 || _session != SESSION.sensor_enroll) {
    return true;
  }

  p = -1;
  Serial.println("Place same finger again");
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Place same finger to the sensor\""));
  while (p != FINGERPRINT_OK && _clientId != 0 && _session == SESSION.sensor_enroll) {
    p = finger.getImage();
    switch (p) {
      case FINGERPRINT_OK:
        Serial.println("Image taken");
        ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprint scanned OK\""));
        break;
      case FINGERPRINT_NOFINGER:
        break;
      case FINGERPRINT_PACKETRECIEVEERR:
        Serial.println("Communication error 3");
        break;
      case FINGERPRINT_IMAGEFAIL:
        Serial.println("Imaging error 3");
        break;
      default:
        Serial.println("Unknown error 3");
        break;
    }
  }

  if (_clientId == 0 || _session != SESSION.sensor_enroll) {
    return true;
  }

  // OK success!
  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Reading fingerprint...\""));
  delay(500);

  p = finger.image2Tz(2);
  switch (p) {
    case FINGERPRINT_OK:
      Serial.println("Image converted");
      ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprint read OK\""));
      break;
    case FINGERPRINT_IMAGEMESS:
      Serial.println("Image too messy 4");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"image too messy\""));
      return true;
    case FINGERPRINT_PACKETRECIEVEERR:
      Serial.println("Communication error 4");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"communication error\""));
      return true;
    case FINGERPRINT_FEATUREFAIL:
      Serial.println("Could not find fingerprint features 4");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Could not find fingerprint features\""));
      return true;
    case FINGERPRINT_INVALIDIMAGE:
      Serial.println("Could not find fingerprint features 4");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Could not find fingerprint features\""));
      return true;
    default:
      Serial.println("Unknown error 4");
      ws.text(_clientId, json("error", "sensor", "enroll", "\"Unknown error\""));
      return true;
  }

  // OK converted!
  delay(500);
    ws.text(_clientId, json("success", "sensor", "enroll", "\"Validating fingerprints...\""));
  Serial.print("Creating model for #");  Serial.println(_fingerId);
    delay(500);

  p = finger.createModel();
  if (p == FINGERPRINT_OK) {
    Serial.println("Prints matched!");
    ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprints matched!\""));
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error 5");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Communication error\""));
    return true;
  } else if (p == FINGERPRINT_ENROLLMISMATCH) {
    Serial.println("Fingerprints did not match 5");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Fingerprints did not match.\\nPlease try again\""));

    return true;
  } else {
    Serial.println("Unknown error 5");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Unknown error\""));
    return true;
  }

  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Saving fingerprint...\""));
  delay(500);

  p = finger.storeModel(_fingerId);
  if (p == FINGERPRINT_OK) {
    Serial.println("Stored!");
    ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprint saved!\""));
    _fingerId = 0;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Communication error\""));
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not store in that location");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Could not store in that location\""));
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Error writing to flash\""));
  } else {
    Serial.println("Unknown error 6");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Unknown error\""));
  }

  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"done\""));
  Serial.println("Fingerprint enroll done!");
  //  _session = SESSION.ready;
  return true;
}

String getConfig(String f, String data) {
  String value = data;
  File file = SPIFFS.open(f, "r");
  if (file) {
    Serial.print(F("Reading "));
    Serial.print(f);
    Serial.print(F(" file... "));

    String text = "";
    while (file.available()) {
      char x = file.read();
      if (x == '\n') {
        break;
      } else {
        text += x;
      }
    }

    if (text != "") {
      value = text;
    }
  }

  file.close();
  return value;
}

void login(uint8_t id, String param) {

  int index = param.lastIndexOf("?");
  if (index == -1) {
    ws.text(id, json("error", "login", "authentication failed"));
  } else {
    String pass = param.substring(0, index);
    if (pass == _pass) {
      String token = param.substring(index + 1);
      if (token.length() == 36) {
        if (_clientId != 0) {
          ws.text(id, json("success", "login", "user is already login"));
        } else {
          _clientId = id;
          _token = token;
          ws.text(id, json("success", "login", "success"));
        }
      } else {
        ws.text(id, json("error", "login", "authentication invalid"));
      }
    } else {
      ws.text(id, json("success", "login", "invalid password"));
    }
  }
}

void sensor(String param) {

  int index = param.lastIndexOf("?");
  String request = "";
  if (index != -1) {
    request = param.substring(0, index);
  } else {
    request = param;
  }

  if (request != "") {

    if (request == "available") {
      _session = SESSION.sensor_verify;

    } else if (request == "info") {
      _session = SESSION.sensor_info;

    } else if (request == "state") {
      if (index == -1) {
        String data = _sensorIsEnable ? "1" : "0";      
        ws.text(_clientId, json("success", "sensor", "state", "\"" + data + "\""));
      } else {
        String path = param.substring(index + 1);
        if (path == "0") {
          _sensorIsEnable = false;
          ws.text(_clientId, json("success", "sensor", "state", "\"0\""));
        } else if (path == "1") {
          _sensorIsEnable = true;
          ws.text(_clientId, json("success", "sensor", "state", "\"1\""));
        }
      }
    } else if (request == "download") {
      if (_sensorIsAvailable) {
        _session = SESSION.sensor_download;
      } else {
        ws.text(_clientId, json("error", "sensor", "sensor is not available"));
      }

    } else if (request == "enroll") {
      if (index == -1) {
        ws.text(_clientId, json("error", "sensor", "enroll", "\"fingerprint id not found\""));
      } else {
        String path = param.substring(index + 1);
        if (path == "cancel") {
          ws.text(_clientId, json("success", "sensor", "enroll", "\"cancel\""));
          _session = SESSION.ready;
        } else {
          _fingerId = path.toInt();
          if (_fingerId > 0) {
            _session = SESSION.sensor_enroll;
          } else {
            ws.text(_clientId, json("error", "sensor", "enroll", "\"invalid fingerprint id\""));
          }
        }

      }
    } else if (request == "delete") {
      String path = param.substring(index + 1);
      _fingerId = path.toInt();
      if (_fingerId <= 0) {
        ws.text(_clientId, json("error", "sensor", "delete", "\"invalid fingerprint id\""));
      } else {
        _session = SESSION.sensor_delete;
      }

    }

  } else {
    ws.text(_clientId, json("error", "sensor", "invalid data", "\"" + param + "\""));

  }
}

void relay(String param) {

  int index = param.lastIndexOf("?");
  String request = "";
  if (index != -1) {
    request = param.substring(0, index);
  } else {
    request = param;
  }
  
  if (request != "") {
    
    if (request == "state") {
      Serial.print("index ");Serial.println(index);
      if (index == -1) {
        String data = _relayState ? "1" : "0";
        ws.text(_clientId, json("success", "relay", "state", "\"" + data + "\""));
      } else {
        String path = param.substring(index + 1);
        if (path == "0") {
          _relayState = false;
          ws.text(_clientId, json("success", "relay", "state", "\"0\""));
        } else if (path == "1") {
          _relayState = true;
          ws.text(_clientId, json("success", "relay", "state", "\"1\""));
        }
      }
      digitalWrite(RELAY_PIN, _relayState);
    }
    
  } else {
    ws.text(_clientId, json("error", "relay", "invalid data", "\"" + param + "\""));
  }
}

void esp(String value) {
  if (value != "") {
    if (value == "restart") {
      ws.text(_clientId, json("success", "esp", value));
      ESP.restart();
    }
  } else {
    ws.text(_clientId, json("error", "esp", "invalid data"));
  }
}

void getSensorInfo() {

  if (_sensorIsAvailable) {
    finger.getParameters();
    finger.getTemplateCount();

    String data = "{"
                  "\"status_reg\": \"" + String(finger.status_reg, HEX) + "\","
                  "\"system_id\": \"" + String(finger.system_id, HEX) + "\","
                  "\"capacity\": \"" + String(finger.capacity) + "\","
                  "\"security_level\": \"" + String(finger.security_level) + "\","
                  "\"device_addr\": \"" + String(finger.device_addr, HEX) + "\","
                  "\"packet_len\": \"" + String(finger.packet_len) + "\","
                  "\"baud_rate\": \"" + String(finger.baud_rate) + "\","
                  "\"template_count\": \"" + String(finger.templateCount) + "\""
                  "}";

    ws.text(_clientId, json("success", "sensor", "info", data));

  }

  _session = SESSION.ready;
}

void deleteFingerprint() {
  int p = finger.deleteModel(_fingerId);
  if (p == FINGERPRINT_OK) {
    Serial.println("Deleted!");
    ws.text(_clientId, json("success", "sensor", "delete", "\"deleted\""));
    _fingerId = 0;
  } else if (p == FINGERPRINT_PACKETRECIEVEERR) {
    Serial.println("Communication error");
    ws.text(_clientId, json("error", "sensor", "delete", "\"Communication error\""));
  } else if (p == FINGERPRINT_BADLOCATION) {
    Serial.println("Could not delete in that location");
    ws.text(_clientId, json("error", "sensor", "delete", "\"Could not delete in that location\""));
  } else if (p == FINGERPRINT_FLASHERR) {
    Serial.println("Error writing to flash");
    ws.text(_clientId, json("error", "sensor", "delete", "\"Error writing to flash\""));
  } else {
    Serial.print("Unknown error: 0x"); Serial.println(p, HEX);
    ws.text(_clientId, json("error", "sensor", "delete", "\"Unknown error: 0x" + String(p, HEX) + "\""));
  }
  _session = SESSION.ready;
}

uint8_t downloadFingerprintTemplate() {

  Serial.println("Downloading fingerprint templates...");
  finger.getParameters();
  finger.getTemplateCount();
  uint8_t count = 0;

  for (uint8_t id = 1; id <= finger.capacity; id++) {
    Serial.print("Looading ID: "); Serial.println(id);
    uint8_t p = finger.loadModel(id);

    if (p == FINGERPRINT_OK) {
      Serial.print("Getting ID: "); Serial.println(id);
      p = finger.getModel();

      if ( p != FINGERPRINT_OK) {
        Serial.print("Fingerprint error ID: "); Serial.println(id);

        String data = "{"
                      "\"id\": \"" + String(id) + "\", "
                      "\"error\": \"" + String(p) + "\""
                      "}";

        ws.text(_clientId, json("success", "sensor", "template", data));

      } else {
        uint8_t bytesReceived[534];
        memset(bytesReceived, 0xff, 534);
        uint32_t starttime = millis();
        int i = 0;
        while (i < 534 && (millis() - starttime) < 20000) {
          if (serial.available()) {
            bytesReceived[i++] = serial.read();
          }
        }

        Serial.print(i); Serial.println(" bytes read.");

        uint8_t fingerTemplate[512];
        memset(fingerTemplate, 0xff, 512);

        int uindx = 9, index = 0;
        while (index < 534) {
          while (index < uindx) ++index;
          uindx += 256;
          while (index < uindx) {
            fingerTemplate[index++] = bytesReceived[index];
          }
          uindx += 2;
          while (index < uindx) ++index;
          uindx = index + 9;
        }
        String packet = "";
        for (int i = 0; i < 512; ++i) {
          packet += *printHex(fingerTemplate[i], 2);
        }
        Serial.println("Finger print packet:");
        Serial.println(packet);

        String data = "{"
                      "\"id\": \"" + String(id) + "\", "
                      "\"packet\": \"" + packet + "\""
                      "}";

        ws.text(_clientId, json("success", "sensor", "template", data));
      }

      count++;
    } else {

//      if (p == FINGERPRINT_PACKETRECIEVEERR) {
//        String data = "{"
//                      "\"id\": \"" + String(id) + "\", "
//                      "\"packet\": \"(communication error)\""
//                      "}";
//
//        ws.text(_clientId, json("success", "sensor", "template", data));
//        
//      } else {
//        Serial.print("Error reading: "); Serial.println(p);
//      }
      Serial.print("Error reading: "); Serial.println(p);
      
    }

    if (count >= finger.templateCount) {
      break;
    }
  }

  ws.text(_clientId, json("success", "sensor", "download", "\"done\""));

  Serial.println("\nFingerprint template downloaded.");
  _session = SESSION.ready;

}

char* printHex(int num, int precision) {
  char tmp[16];
  char format[128];

  sprintf(format, "%%.%dX", precision);

  sprintf(tmp, format, num);
  return tmp;
}
