#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <FS.h>
#include <Adafruit_Fingerprint.h>

#define LED_PIN 2
#define RELAY_PIN 4
#define RELAY2_PIN 5
#define FP_TX_PIN 13
#define FP_RX_PIN 12
#define PORT 4848
#define DELAY_MILLIS 1500
#define BAUD_RATE 9600

static const String VERSION = "1.0";
static const String PATH = "v1";

String _apSsid = "FingerprintSwitch v" + VERSION;
String _apPass = "";

String _wifiSsid = "";
String _wifiPass = "!";

bool _ledState = false;
bool _relayState = true;

unsigned long _previousMillis = 0;
unsigned long _sensorFailedMillis = 0;
unsigned long _sensorMillis = 0;

uint8_t _clientId = 0;
int _fingerId = 0;

String _pass = "1234";
String _token = "";

bool _sensorIsAvailable = false;
bool _sensorIsEnable = true;
bool _espSet = false;
bool initialize = false;
// bool _sensorLogin = false;
int _engineStart = 0;
int _confidence = 50;

SoftwareSerial serial(FP_TX_PIN, FP_RX_PIN);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&serial);
AsyncWebServer server(PORT);
AsyncWebSocket ws("/" + PATH);

static const struct SESSION
{
  static const uint8_t init_main = 0;
  static const uint8_t init_wifi = 1;
  static const uint8_t ready = 2;
  static const uint8_t sensor_verify = 3;
  static const uint8_t sensor_info = 4;
  static const uint8_t sensor_download = 5;
  static const uint8_t sensor_enroll = 6;
  static const uint8_t sensor_delete = 7;
  static const uint8_t sensor_empty = 8;
  static const uint8_t sensor_busy = 9;
  // static const uint8_t sensor_login = 9;
} SESSION;

uint8_t _session = SESSION.init_main;

String json(String status, String type, String message, String data = "\"\"")
{
  return "{\"status\": \"" + status + "\", \"type\": \"" + type + "\", \"message\": \"" + message + "\", \"data\": " + data + "}";
}

void handleWebSocketMessage(void *arg, uint8_t *data, size_t len, uint8_t id)
{

  AwsFrameInfo *info = (AwsFrameInfo *)arg;

  if (info->final && info->index == 0 && info->len == len && info->opcode == WS_TEXT)
  {

    data[len] = 0;
    String msg = (char *)data;
    int index = msg.indexOf("=");

    Serial.println(msg);

    if (index == -1)
    {
      ws.text(id, json("error", "unknown", "invalid data", msg));
      return;
    }

    String type = msg.substring(0, index);
    String param = msg.substring(index + 1);

    if (type == "login")
    {
      login(id, param);
    }
    else if (type == "sensor")
    {
      sensor(param);
    }
    else if (type == "relay")
    {
      relay(param);
    }
    else if (type == "esp")
    {
      esp(param);
    }
    else
    {
      ws.text(id, json("error", type, "request not found", "\"" + param + "\""));
    }
  }
}

void onEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  switch (type)
  {
  case WS_EVT_CONNECT:
    Serial.printf("Client #%u connected from %s\n", client->id(), client->remoteIP().toString().c_str());
    ws.text(client->id(), json("success", "connect", "connected"));
    break;
  case WS_EVT_DISCONNECT:
    if (client->id() == _clientId)
    {
      _clientId = 0;
      _token = "";
      _session = SESSION.ready;
    }
    Serial.print("Client id: ");
    Serial.println(_clientId);
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

void setup()
{

  delay(1000);
  digitalWrite(FP_TX_PIN, LOW);
  digitalWrite(FP_RX_PIN, LOW);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RELAY_PIN, OUTPUT);
  pinMode(RELAY2_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, _relayState);
  digitalWrite(RELAY2_PIN, HIGH);

  Serial.begin(BAUD_RATE);

  finger.begin(57600);
  _sensorIsAvailable = finger.verifyPassword();

  if (_sensorIsAvailable)
  {
    finger.LEDcontrol(3, 0, 2, 1);
    Serial.println("\nSensor ready!");
  }
  else
  {
    Serial.println("\nSensor is not available");
  }

  _session = SESSION.ready;
}

void loop()
{

  unsigned long ms = millis();
  if (ms - _previousMillis >= 1000)
  {

    if (_sensorIsAvailable)
    {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState);
    }

    _previousMillis = ms;
  }

  if (ms - _sensorFailedMillis >= 150)
  {
    if (!_sensorIsAvailable)
    {
      _ledState = !_ledState;
      digitalWrite(LED_PIN, _ledState);
    }
    _sensorFailedMillis = ms;
  }

  if (ms - _sensorMillis >= 100)
  {

    switch (_session)
    {
    case SESSION.ready:
      if (_sensorIsAvailable && _sensorIsEnable)
      {
        listenToSensor();
      }
      break;

    case SESSION.sensor_download:
      digitalWrite(LED_PIN, HIGH);
      delay(DELAY_MILLIS);
      downloadFingerprintTemplate();
      break;

    case SESSION.sensor_verify:
      digitalWrite(LED_PIN, HIGH);
      delay(DELAY_MILLIS);
      _sensorIsAvailable = finger.verifyPassword();
      ws.text(_clientId, json("success", "sensor", "available", _sensorIsAvailable ? "\"1\"" : "\"0\""));
      _session = SESSION.ready;
      break;

    case SESSION.sensor_info:
      digitalWrite(LED_PIN, HIGH);
      delay(DELAY_MILLIS);
      getSensorInfo();
      break;

    case SESSION.sensor_enroll:
      digitalWrite(LED_PIN, HIGH);
      delay(DELAY_MILLIS);
      while (!enrollFingerprint())
      {
        if (_clientId == 0)
        {
          break;
        }
      }
      _session = SESSION.ready;
      break;

    case SESSION.sensor_delete:
      digitalWrite(LED_PIN, HIGH);
      delay(DELAY_MILLIS);
      deleteFingerprint();
      break;

    case SESSION.sensor_empty:
      digitalWrite(LED_PIN, HIGH);
      delay(DELAY_MILLIS);
      emptyFingerprint();

    default:
      break;
    }

    _sensorMillis = ms;
  }

  //  ws.cleanupClients();

  if (!initialize)
  {
    initialize = true;

    if (SPIFFS.begin())
    {
      Serial.println("SPIFFS mounted");
      _apSsid = getConfig("/ap_ssid.txt", _apSsid);
      _apPass = getConfig("/ap_pass.txt", _apPass);
      _pass = getConfig("/pass.txt", _pass);
      _wifiSsid = getConfig("/wifi_ssid.txt", _wifiSsid);
      _wifiPass = getConfig("/wifi_pass.txt", _wifiPass);
      _engineStart = getConfig("/engine_start.txt", String(_engineStart)).toInt();
      int confidence = getConfig("/confidence.txt", String(_confidence)).toInt();
      _confidence = confidence < 10 ? _confidence : confidence;
    }

    Serial.print("Engine delay: ");
    Serial.println(_engineStart);
    Serial.print("Fingerprint confidence: ");
    Serial.println(_confidence);
    Serial.print("Admin pass: ");
    Serial.println(_pass);
    Serial.print("AP ssid: ");
    Serial.println(_apSsid);
    Serial.print("AP pass: ");
    Serial.println(_apPass);
    Serial.print("Wifi SSID: ");
    Serial.println(_wifiSsid);
    Serial.print("Wifi Pass: ");
    Serial.println(_wifiPass);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(_apSsid, _apPass);
    Serial.print("Please connect to ");
    Serial.println(WiFi.softAPIP());

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
    // _session = SESSION.ready;
    Serial.println("Server ready!");
    Serial.println();
  }
}

void listenToSensor()
{
  uint8_t p = finger.getImage();
  switch (p)
  {
  case FINGERPRINT_OK:
    Serial.println("Image taken");
    break;
  case FINGERPRINT_NOFINGER:
    return;
  case FINGERPRINT_PACKETRECIEVEERR:
    //            Serial.println("Communication error 1");
    return;
  case FINGERPRINT_IMAGEFAIL:
    Serial.println("Imaging error");
    return;
  default:
    Serial.println("Unknown error 1");
    return;
  }

  // OK success!

  p = finger.image2Tz();
  switch (p)
  {
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
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Found a print match!");
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    return;
  }
  else if (p == FINGERPRINT_NOTFOUND)
  {
    finger.LEDcontrol(2, 20, 1, 3);
    delay(500);
    finger.LEDcontrol(3, 0, _relayState ? 2 : 3, 1);
    Serial.println("Did not find a match");
    if (_clientId > 0)
    {
      ws.text(_clientId, json("success", "login", "denied", "\"Fingerprint not found\""));
    }
    return;
  }
  else
  {
    Serial.println("Unknown error");
    return;
  }

  // found a match!
  Serial.print("Found ID #");
  Serial.print(finger.fingerID);
  uint16_t confidence = finger.confidence;
  Serial.print(" with confidence of ");
  Serial.println(confidence);

  if (confidence >= _confidence)
  {
    // finger.LEDcontrol(2, 20, _relayState ? 2 : 3, 3);
    _relayState = !_relayState;
    digitalWrite(RELAY_PIN, _relayState);
    finger.LEDcontrol(3, 0, _relayState ? 2 : 3, 1);
    String data = _relayState ? "1" : "0";
    if (_clientId > 0)
    {
      ws.text(_clientId, json("success", "relay", "state", "\"" + data + "\""));
      ws.text(_clientId, json("success", "login", "grant", "\"Fingerprint match found\""));
    }
    delay(500);
    if (!_relayState && _engineStart >= 1000 && _engineStart < 5000)
    {
      Serial.print("Engine starts within ");
      Serial.print(_engineStart);
      Serial.println(" milliseconds");
      digitalWrite(RELAY2_PIN, LOW);
      delay(_engineStart);
      digitalWrite(RELAY2_PIN, HIGH);
      Serial.print("Starter off");
    }
  }
  else
  {
    if (_clientId > 0)
    {
      ws.text(_clientId, json("success", "login", "denied", "\"Fingerprint minimum confidence not reached\""));
    }
    finger.LEDcontrol(2, 20, _relayState ? 2 : 3, 3);
    delay(500);
    finger.LEDcontrol(3, 0, _relayState ? 2 : 3, 1);
  }

  return; // finger.fingerID;
}

bool enrollFingerprint()
{

  ws.text(_clientId, json("success", "sensor", "enroll", "\"enrolling\""));
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Place your finger to the sensor\""));
  int p = -1;
  Serial.print("Enrolling fingerprint for ID #");
  Serial.println(_fingerId);
  Serial.println("Please place your finger");
  while (p != FINGERPRINT_OK && _clientId != 0 && _session == SESSION.sensor_enroll)
  {
    p = finger.getImage();
    switch (p)
    {
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

  if (_clientId == 0 || _session != SESSION.sensor_enroll)
  {
    return true;
  }

  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Reading fingerprint...\""));
  delay(500);
  //   OK success!

  p = finger.image2Tz(1);
  switch (p)
  {
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
  while (p != FINGERPRINT_NOFINGER && _clientId != 0 && _session == SESSION.sensor_enroll)
  {
    p = finger.getImage();
  }

  if (_clientId == 0 || _session != SESSION.sensor_enroll)
  {
    return true;
  }

  p = -1;
  Serial.println("Place same finger again");
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Place same finger to the sensor\""));
  while (p != FINGERPRINT_OK && _clientId != 0 && _session == SESSION.sensor_enroll)
  {
    p = finger.getImage();
    switch (p)
    {
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

  if (_clientId == 0 || _session != SESSION.sensor_enroll)
  {
    return true;
  }

  // OK success!
  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Reading fingerprint...\""));
  delay(500);

  p = finger.image2Tz(2);
  switch (p)
  {
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
  Serial.print("Creating model for #");
  Serial.println(_fingerId);
  delay(500);

  p = finger.createModel();
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Prints matched!");
    ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprints matched!\""));
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error 5");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Communication error\""));
    return true;
  }
  else if (p == FINGERPRINT_ENROLLMISMATCH)
  {
    Serial.println("Fingerprints did not match 5");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Fingerprints did not match.\\nPlease try again\""));

    return true;
  }
  else
  {
    Serial.println("Unknown error 5");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Unknown error\""));
    return true;
  }

  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"Saving fingerprint...\""));
  delay(500);

  p = finger.storeModel(_fingerId);
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Stored!");
    ws.text(_clientId, json("success", "sensor", "enroll", "\"Fingerprint saved!\""));
    _fingerId = 0;
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Communication error\""));
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not store in that location");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Could not store in that location\""));
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Error writing to flash\""));
  }
  else
  {
    Serial.println("Unknown error 6");
    ws.text(_clientId, json("error", "sensor", "enroll", "\"Unknown error\""));
  }

  delay(500);
  ws.text(_clientId, json("success", "sensor", "enroll", "\"done\""));
  Serial.println("Fingerprint enroll done!");
  //  _session = SESSION.ready;
  return true;
}

void emptyFingerprint()
{
  int result = finger.emptyDatabase();
  if (result == FINGERPRINT_OK)
  {
    ws.text(_clientId, json("success", "sensor", "delete-all", "\"All fingerprints are deleted.\""));
  }
  else
  {
    ws.text(_clientId, json("error", "sensor", "delete-all", "\"internal error. Delete all failed\""));
  }
  _session = SESSION.ready;
}

String getConfig(String f, String data)
{
  String value = data;
  File file = SPIFFS.open(f, "r");
  if (file)
  {
    Serial.print(F("Reading "));
    Serial.print(f);
    Serial.println(F(" file... "));

    String text = "";
    while (file.available())
    {
      char x = file.read();
      if (x == '\n')
      {
        break;
      }
      else
      {
        text += x;
      }
    }

    if (text != "")
    {
      value = text;
    }
  }

  file.close();
  return value;
}

void setConfig(String f, String data)
{
  _espSet = true;
  File file = SPIFFS.open(f, "w");
  if (!file)
  {
    Serial.println("Cannot open file for writing");
    ws.text(_clientId, json("error", "esp", "set", "\"internal error. Cannot save your changes\""));
  }
  else
  {
    Serial.println("Writing files...");

    int result = file.print(data);

    if (result == 0)
    {
      Serial.println("Failed to write");
      ws.text(_clientId, json("error", "esp", "set", "\"Internal error. Failed to save changes\""));
    }
    else
    {
      Serial.println("Files written");
      ws.text(_clientId, json("success", "esp", "set", "\"done\""));

      if (f == "/pass.txt")
      {
        _pass = data;
      }
    }
  }
  file.close();
  _espSet = false;
}

void login(uint8_t id, String param)
{

  int index = param.lastIndexOf("?");
  if (index == -1)
  {
    ws.text(id, json("error", "login", "authentication failed"));
  }
  else
  {
    String pass = param.substring(0, index);
    if (pass == _pass)
    {
      String token = param.substring(index + 1);
      if (token.length() == 36)
      {
        if (_clientId != 0)
        {
          ws.text(id, json("success", "login", "user is already login"));
        }
        else
        {
          _clientId = id;
          _token = token;
          ws.text(id, json("success", "login", "success"));
        }
      }
      else
      {
        ws.text(id, json("error", "login", "authentication invalid"));
      }
    }
    else
    {
      ws.text(id, json("success", "login", "invalid password"));
    }
  }
}

void sensor(String param)
{

  int index = param.lastIndexOf("?");
  String request = "";
  if (index != -1)
  {
    request = param.substring(0, index);
  }
  else
  {
    request = param;
  }

  if (request == "")
  {
    ws.text(_clientId, json("error", "sensor", "invalid data", "\"" + param + "\""));
  }
  else
  {

    if (request == "available")
    {
      _session = SESSION.sensor_verify;
    }
    else if (request == "info")
    {
      _session = SESSION.sensor_info;
    }
    else if (request == "state")
    {
      if (index == -1)
      {
        String data = _sensorIsEnable ? "1" : "0";
        if (_clientId > 0)
        {
          ws.text(_clientId, json("success", "sensor", "state", "\"" + data + "\""));
        }
        else
        {
          // if (_token == "") {
          //   _token = param.substring(index + 1);
          //   ws.textAll(json("success", "sensor", data));
          // }
        }
      }
      else
      {
        String path = param.substring(index + 1);
        if (path == "0")
        {
          _sensorIsEnable = false;
          ws.text(_clientId, json("success", "sensor", "state", "\"0\""));
        }
        else if (path == "1")
        {
          _sensorIsEnable = true;
          ws.text(_clientId, json("success", "sensor", "state", "\"1\""));
        }
        else
        {
          ws.text(_clientId, json("error", "sensor", "invalid state"));
        }
      }
    }
    else if (request == "download")
    {
      if (_sensorIsAvailable)
      {
        _session = SESSION.sensor_download;
      }
      else
      {
        ws.text(_clientId, json("error", "sensor", "sensor is not available"));
      }
    }
    else if (request == "enroll")
    {
      if (index == -1)
      {
        ws.text(_clientId, json("error", "sensor", "enroll", "\"fingerprint id not found\""));
      }
      else
      {
        String path = param.substring(index + 1);
        if (path == "cancel")
        {
          ws.text(_clientId, json("success", "sensor", "enroll", "\"cancel\""));
          _session = SESSION.ready;
        }
        else
        {
          _fingerId = path.toInt();
          if (_fingerId > 0)
          {
            _session = SESSION.sensor_enroll;
          }
          else
          {
            ws.text(_clientId, json("error", "sensor", "enroll", "\"invalid fingerprint id\""));
          }
        }
      }
    }
    else if (request == "delete")
    {
      String path = param.substring(index + 1);
      _fingerId = path.toInt();
      if (_fingerId <= 0)
      {
        ws.text(_clientId, json("error", "sensor", "delete", "\"invalid fingerprint id\""));
      }
      else
      {
        _session = SESSION.sensor_delete;
      }
    }
    else if (request == "delete-all")
    {
      _session = SESSION.sensor_empty;
    }
    else if (request == "confidence")
    {

      String param2 = param.substring(index + 1);
      int index2 = param2.indexOf("=");
    }
    else
    {
      ws.text(_clientId, json("error", "sensor", "invalid type", "\"" + request + "\""));
    }
  }
}

void relay(String param)
{

  int index = param.lastIndexOf("?");
  String request = "";
  if (index != -1)
  {
    request = param.substring(0, index);
  }
  else
  {
    request = param;
  }

  if (request == "")
  {
    ws.text(_clientId, json("error", "relay", "invalid data", "\"" + param + "\""));
  }
  else
  {

    if (request == "state")
    {
      if (index == -1)
      {
        String data = _relayState ? "1" : "0";
        ws.text(_clientId, json("success", "relay", "state", "\"" + data + "\""));
      }
      else
      {
        String path = param.substring(index + 1);
        if (path == "0")
        {
          _relayState = false;
          ws.text(_clientId, json("success", "relay", "state", "\"0\""));
        }
        else if (path == "1")
        {
          _relayState = true;
          ws.text(_clientId, json("success", "relay", "state", "\"1\""));
        }
        digitalWrite(RELAY_PIN, _relayState);
        if (_sensorIsAvailable)
        {
          finger.LEDcontrol(3, 0, _relayState ? 2 : 3, 1);
        }
      }
    }
  }
}

void esp(String param)
{

  int index = param.lastIndexOf("?");
  String request = "";

  if (index != -1)
  {
    request = param.substring(0, index);
  }
  else
  {
    request = param;
  }

  if (request == "")
  {
    ws.text(_clientId, json("error", "esp", "invalid data", "\"" + param + "\""));
  }
  else
  {

    if (request == "info")
    {

      String data = "{"
                    "\"version\": \"" +
                    VERSION + "\","
                              "\"pass\": \"" +
                    _pass + "\","
                            "\"ap_ssid\": \"" +
                    _apSsid + "\","
                              "\"ap_pass\": \"" +
                    _apPass + "\","
                              "\"ap_ip\": \"" +
                    WiFi.softAPIP().toString() + "\","
                                                 "\"wifi_ssid\": \"" +
                    WiFi.SSID() + "\","
                                  "\"wifi_pass\": \"" +
                    _wifiPass + "\","
                                "\"wifi_ip\": \"" +
                    WiFi.localIP().toString() + "\","
                                                "\"wifi_gateway\": \"" +
                    WiFi.gatewayIP().toString() + "\","
                                                  "\"wifi_subnet\": \"" +
                    WiFi.subnetMask().toString() + "\","
                                                   "\"wifi_mac\": \"" +
                    WiFi.macAddress() + "\","
                                        "\"wifi_channel\": \"" +
                    WiFi.channel() + "\","
                                     "\"wifi_state\": \"" +
                    WiFi.isConnected() + "\""
                                         "}";

      ws.text(_clientId, json("success", "esp", "info", data));
    }
    else if (request == "set")
    {
      String param2 = param.substring(index + 1);

      int index2 = param2.indexOf("=");

      if (index2 == -1)
      {
        ws.text(_clientId, json("error", "esp", "set", "invalid second parameter"));
      }
      else
      {
        String request2 = param2.substring(0, index2);

        if (request2 == "")
        {
          ws.text(_clientId, json("error", "esp", "set", "invalid request"));
        }
        else
        {

          String value = param2.substring(index2 + 1);
          if (request2 == "pass")
          {
            while (_espSet)
            {
            }
            setConfig("/pass.txt", value);
          }
          else if (request2 == "ap-ssid")
          {
            while (_espSet)
            {
            }
            setConfig("/ap_ssid.txt", value);
          }
          else if (request2 == "ap-pass")
          {
            while (_espSet)
            {
            }
            setConfig("/ap_pass.txt", value);
          }
          else if (request2 == "wifi-ssid")
          {
            while (_espSet)
            {
            }
            setConfig("/wifi_ssid.txt", value);
          }
          else if (request2 == "wifi-pass")
          {
            while (_espSet)
            {
            }
            setConfig("/wifi_pass.txt", value);
          }
          else
          {
            ws.text(_clientId, json("error", "esp", "set", "invalid request"));
          }
        }
      }
    }
    else if (request == "restart")
    {
      ws.text(_clientId, json("success", "esp", "restart"));
      ESP.restart();
    }
  }
}

void getSensorInfo()
{

  if (_sensorIsAvailable)
  {
    finger.getParameters();
    finger.getTemplateCount();

    String data = "{"
                  "\"status_reg\": \"" +
                  String(finger.status_reg, HEX) + "\","
                                                   "\"system_id\": \"" +
                  String(finger.system_id, HEX) + "\","
                                                  "\"capacity\": \"" +
                  String(finger.capacity) + "\","
                                            "\"security_level\": \"" +
                  String(finger.security_level) + "\","
                                                  "\"device_addr\": \"" +
                  String(finger.device_addr, HEX) + "\","
                                                    "\"packet_len\": \"" +
                  String(finger.packet_len) + "\","
                                              "\"baud_rate\": \"" +
                  String(finger.baud_rate) + "\","
                                             "\"template_count\": \"" +
                  String(finger.templateCount) + "\""
                                                 "}";

    ws.text(_clientId, json("success", "sensor", "info", data));
  }

  _session = SESSION.ready;
}

void deleteFingerprint()
{
  int p = finger.deleteModel(_fingerId);
  if (p == FINGERPRINT_OK)
  {
    Serial.println("Deleted!");
    ws.text(_clientId, json("success", "sensor", "delete", "\"deleted\""));
    _fingerId = 0;
  }
  else if (p == FINGERPRINT_PACKETRECIEVEERR)
  {
    Serial.println("Communication error");
    ws.text(_clientId, json("error", "sensor", "delete", "\"Communication error\""));
  }
  else if (p == FINGERPRINT_BADLOCATION)
  {
    Serial.println("Could not delete in that location");
    ws.text(_clientId, json("error", "sensor", "delete", "\"Could not delete in that location\""));
  }
  else if (p == FINGERPRINT_FLASHERR)
  {
    Serial.println("Error writing to flash");
    ws.text(_clientId, json("error", "sensor", "delete", "\"Error writing to flash\""));
  }
  else
  {
    Serial.print("Unknown error: 0x");
    Serial.println(p, HEX);
    ws.text(_clientId, json("error", "sensor", "delete", "\"Unknown error: 0x" + String(p, HEX) + "\""));
  }
  _session = SESSION.ready;
}

uint8_t downloadFingerprintTemplate()
{

  Serial.println("Downloading fingerprint templates...");
  finger.getParameters();
  finger.getTemplateCount();
  uint8_t count = 0;

  for (uint8_t id = 1; id <= finger.capacity; id++)
  {
    Serial.print("Looading ID: ");
    Serial.println(id);
    uint8_t p = finger.loadModel(id);

    if (p == FINGERPRINT_OK)
    {
      Serial.print("Getting ID: ");
      Serial.println(id);
      p = finger.getModel();

      if (p != FINGERPRINT_OK)
      {
        Serial.print("Fingerprint error ID: ");
        Serial.println(id);

        String data = "{"
                      "\"id\": \"" +
                      String(id) + "\", "
                                   "\"error\": \"" +
                      String(p) + "\""
                                  "}";

        ws.text(_clientId, json("success", "sensor", "template", data));
      }
      else
      {
        uint8_t bytesReceived[534];
        memset(bytesReceived, 0xff, 534);
        uint32_t starttime = millis();
        int i = 0;
        while (i < 534 && (millis() - starttime) < 20000)
        {
          if (serial.available())
          {
            bytesReceived[i++] = serial.read();
          }
        }

        Serial.print(i);
        Serial.println(" bytes read.");

        uint8_t fingerTemplate[512];
        memset(fingerTemplate, 0xff, 512);

        int uindx = 9, index = 0;
        while (index < 534)
        {
          while (index < uindx)
            ++index;
          uindx += 256;
          while (index < uindx)
          {
            fingerTemplate[index++] = bytesReceived[index];
          }
          uindx += 2;
          while (index < uindx)
            ++index;
          uindx = index + 9;
        }
        String packet = "";
        for (int i = 0; i < 512; ++i)
        {
          packet += *printHex(fingerTemplate[i], 2);
        }
        Serial.println("Finger print packet:");
        Serial.println(packet);

        String data = "{"
                      "\"id\": \"" +
                      String(id) + "\", "
                                   "\"packet\": \"" +
                      packet + "\""
                               "}";

        ws.text(_clientId, json("success", "sensor", "template", data));
      }

      count++;
    }
    else
    {

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
      Serial.print("Error reading: ");
      Serial.println(p);
    }

    if (count >= finger.templateCount)
    {
      break;
    }
  }

  ws.text(_clientId, json("success", "sensor", "download", "\"done\""));

  Serial.println("\nFingerprint template downloaded.");
  delay(1000);
  _session = SESSION.ready;
}

char *printHex(int num, int precision)
{
  char tmp[16];
  char format[128];

  sprintf(format, "%%.%dX", precision);

  sprintf(tmp, format, num);
  return tmp;
}
