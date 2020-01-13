#include <FS.h>                                               // This needs to be first, or it all crashes and burns
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>                                      // Useful to access to ESP by hostname.local
#include <WiFiManager.h>                                      // https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <ArduinoJson.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>                                       // depends on WifiUdp
#include <PubSubClient.h>                                     // https://github.com/knolleary/pubsubclient
#include <Ticker.h>                                           // For LED status

// User settings are below here

const bool enableMQTTServices = true;                         // Use MQTT services, must be enabled for ArduinoOTA
const bool enableMDNSServices = true;                         // Use mDNS services, must be enabled for ArduinoOTA
const bool enableOTAServices =  true;                         // Use OTA services, requires mDNS services

const unsigned int captureBufSize = 150;                      // Size of the IR capture buffer.

// WEMOS users may need to adjust pins for compatibility
const int pinr1 = 14;                                         // Receiving pin
const int pins1 = 4;                                          // Transmitting preset 1
const int pins2 = 5;                                          // Transmitting preset 2
const int pins3 = 12;                                         // Transmitting preset 3
const int pins4 = 13;                                         // Transmitting preset 4
const int configpin = 10;                                     // Reset Pin

// User settings are above here
const int ledpin = BUILTIN_LED;                               // Built in LED defined for WEMOS people

char mdnsName[128]      = "ir.local";

char mqttServer[128]    = "mqtt.local";
int  mqttPort           = 1883;
char mqttPortString[6]  = "1883";
char mqttUser[32]       = "";
char mqttPasswd[32]     = "";

char static_ip[16] = "";
char static_gw[16] = "";
char static_sn[16] = "";

WiFiClient wifi;

const char* MQTT_TOPIC_SUB_SIMPLE = "ir/simple";
const char* MQTT_TOPIC_SUB_JSON = "ir/json";
const char* MQTT_TOPIC_PUB = "ir/receive";
PubSubClient * mqtt;

bool shouldSaveConfig = false;                                // Flag for saving data
bool holdReceive = false;                                     // Flag to prevent IR receiving while transmitting

IRrecv irrecv(pinr1, captureBufSize);
IRsend irsend1(pins1);
IRsend irsend2(pins2);
IRsend irsend3(pins3);
IRsend irsend4(pins4);

DynamicJsonDocument deviceState(1024);
Ticker ticker;

class Code {
  public:
    char encoding[14] = "";
    char address[20] = "";
    char command[40] = "";
    char data[40] = "";
    String raw = "";
    int bits = 0;
    time_t timestamp = 0;
    bool valid = false;
};

// Declare prototypes
void sendCodePage(Code selCode);
void sendCodePage(Code selCode, int httpcode);
void cvrtCode(Code& codeData, decode_results *results);
void copyCode (Code& c1, Code& c2);

Code last_recv;
Code last_send;

//+=============================================================================
// Callback notifying us of the need to save config
//
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}

//+=============================================================================
// Reenable IR receiving
//
void resetReceive() {
  if (holdReceive) {
    Serial.println("Enabling incoming IR signals");
    irrecv.resume();
    holdReceive = false;
  }
}

//+=============================================================================
// Toggle state
//
void tick()
{
  int state = digitalRead(ledpin);  // get the current state of GPIO1 pin
  digitalWrite(ledpin, !state);     // set pin to the opposite state
}

//+=============================================================================
// Turn off the Led after timeout
//
void disableLed()
{
  // Serial.println("Turning off the LED to save power.");
  digitalWrite(ledpin, HIGH);                           // Shut down the LED
  ticker.detach();                                      // Stopping the ticker
}

//+=============================================================================
// Gets called when WiFiManager enters configuration mode
//
void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println("Config mode activated");
  Serial.println(WiFi.softAPIP());
  // if you used auto generated SSID, print it
  Serial.println(myWiFiManager->getConfigPortalSSID());
  // entered config mode, make led toggle faster
  ticker.attach(0.2, tick);
}

//+=============================================================================
// Gets called when device loses connection to the accesspoint
//
void lostWifiCallback (const WiFiEventStationModeDisconnected& evt) {
  Serial.println("Lost Wifi");
  // reset and try again, or maybe put it to deep sleep
  ESP.reset();
  delay(1000);
}

bool processJsonMessage(char* topic, byte* payload, unsigned int length) {
  Serial.println("MQTT message received - JSON");

  DynamicJsonDocument root(1024);
  DeserializationError error = deserializeJson(root, payload, length);

  int simple = 0; // TODO voir ce que ça signifie
  int out = 1; // output pin number

  if (error) {
    Serial.println("JSON parsing failed");
    root.clear();
    ticker.attach(0.05, tick);
    delay(2000);
    ticker.detach();
    disableLed();
  } else {
    digitalWrite(ledpin, LOW);
    ticker.attach(0.5, disableLed);

    // Handle device state limitations for the global JSON command request
    // server->hasArg("device") // TODO voir ce que ça signifie
    // server->hasArg("state") // TODO voir ce que ça signifie

    String message = "Code sent";

    for (int x = 0; x < root.size(); x++) {
      String type = root[x]["type"];
      String ip = root[x]["ip"];
      int rdelay = root[x]["rdelay"];
      int pulse = root[x]["pulse"];
      int pdelay = root[x]["pdelay"];
      int repeat = root[x]["repeat"];
      int xout = root[x]["out"];
      if (xout == 0) {
        xout = out;
      }
      int duty = root[x]["duty"];

      if (pulse <= 0) pulse = 1; // Make sure pulse isn't 0
      if (repeat <= 0) repeat = 1; // Make sure repeat isn't 0
      if (pdelay <= 0) pdelay = 100; // Default pdelay
      if (rdelay <= 0) rdelay = 1000; // Default rdelay
      if (duty <= 0) duty = 50; // Default duty

      // Handle device state limitations on a per JSON object basis
      String device = root[x]["device"];
      if (device != "null") {
        int state = root[x]["state"];
        if (deviceState.containsKey(device)) {
          int currentState = deviceState[device];
          if (state == currentState) {
            Serial.println("Not sending command to " + device + ", already in state " + state);
            message = "Code sent. Some components of the code were held because device was already in appropriate state";
            continue;
          } else {
            Serial.println("Setting device " + device + " to state " + state);
            deviceState[device] = state;
          }
        } else {
          Serial.println("Setting device " + device + " to state " + state);
          deviceState[device] = state;
        }
      }

      if (type == "delay") {
        delay(rdelay);
      } else if (type == "raw") {
        JsonArray raw = root[x]["data"]; // Array of unsigned int values for the raw signal
        int khz = root[x]["khz"];
        if (khz <= 0) khz = 38; // Default to 38khz if not set
        rawblast(raw, khz, rdelay, pulse, pdelay, repeat, pickIRsend(xout),duty);
      } else if (type == "pronto") {
        JsonArray pdata = root[x]["data"]; // Array of values for pronto
        pronto(pdata, rdelay, pulse, pdelay, repeat, pickIRsend(xout));
      // } else if (type == "roku") {
      //   String data = root[x]["data"];
      //   rokuCommand(ip, data, repeat, rdelay);
      } else {
        String data = root[x]["data"];
        String addressString = root[x]["address"];
        long address = strtoul(addressString.c_str(), 0, 0);
        int len = root[x]["length"];
        irblast(type, data, len, rdelay, pulse, pdelay, repeat, address, pickIRsend(xout));
      }
    }

    root.clear();
  }
}

bool processReceived(decode_results &results) {
    char buffer[512] = "TODO";
    // StaticJsonDocument<256> root;
    // size_t n = serializeJson(root, buffer);
    mqtt->publish(MQTT_TOPIC_PUB, buffer, strlen(buffer));
}

//+=============================================================================
// First setup of the Wifi.
// If return true, the Wifi is well connected.
// Should not return false if Wifi cannot be connected, it will loop
//
bool setupWifi(bool resetConf) {
  // start ticker with 0.5 because we start in AP mode and try to connect
  ticker.attach(0.5, tick);

  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  // reset settings - for testing
  if (resetConf) {
    wifiManager.resetSettings();
  }

  // set callback that gets called when connecting to previous WiFi fails, and enters Access Point mode
  wifiManager.setAPCallback(configModeCallback);
  // set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);
  // reset device if on config portal for greater than 3 minutes
  wifiManager.setConfigPortalTimeout(180);

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);
        configFile.readBytes(buf.get(), size);
        DynamicJsonDocument json(1024);
        DeserializationError error = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if (!error) {
          Serial.println("\nparsed json");
          if (json.containsKey("mdnsName")) strncpy(mdnsName, json["mdnsName"], 128);
          if (json.containsKey("mqttServer")) strncpy(mqttServer, json["mqttServer"], 128);
          if (json.containsKey("mqttUser")) strncpy(mqttUser, json["mqttUser"], 32);
          if (json.containsKey("mqttPasswd")) strncpy(mqttPasswd, json["mqttPasswd"], 32);
          if (json.containsKey("mqttPort")) {
            strncpy(mqttPortString, json["mqttPort"], 6);
            mqttPort = atoi(json["mqttPort"]);
          }
          if (json.containsKey("ip")) strncpy(static_ip, json["ip"], 16);
          if (json.containsKey("gw")) strncpy(static_gw, json["gw"], 16);
          if (json.containsKey("sn")) strncpy(static_sn, json["sn"], 16);
        } else {
          Serial.println("failed to load json config");
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_mdnsName("mdnsName", "mDNS name of this IR Controller", mdnsName, 32);
  wifiManager.addParameter(&custom_mdnsName);

  WiFiManagerParameter custom_mqttServer("mqttServer", "MQTT server", mqttServer, 128);
  wifiManager.addParameter(&custom_mqttServer);

  WiFiManagerParameter custom_mqttPortString("mqttPortString", "MQTT Port", mqttPortString, 6);
  wifiManager.addParameter(&custom_mqttPortString);

  WiFiManagerParameter custom_mqttUser("mqttUser", "MQTT User", mqttUser, 32);
  wifiManager.addParameter(&custom_mqttUser);

  WiFiManagerParameter custom_mqttPasswd("mqttPasswd", "MQTT Password", mqttPasswd, 32);
  wifiManager.addParameter(&custom_mqttPasswd);

  if (strlen(static_ip) && strlen(static_gw) && strlen(static_sn)) {
    IPAddress sip, sgw, ssn;
    sip.fromString(static_ip);
    sgw.fromString(static_gw);
    ssn.fromString(static_sn);
    Serial.println("Using Static IP");
    wifiManager.setSTAStaticIPConfig(sip, sgw, ssn);
  } else {
    Serial.println("Using DHCP");
  }

  // fetches ssid and pass and tries to connect
  // if it does not connect it starts an access point with the specified name
  // and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect(mdnsName)) {
    Serial.println("Failed to connect and hit timeout");
    // reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  }

  // if you get here you have been connected to WiFi
  strncpy(mdnsName, custom_mdnsName.getValue(), 128);
  strncpy(mqttServer, custom_mqttServer.getValue(), 128);
  strncpy(mqttPortString, custom_mqttPortString.getValue(), 6);
  strncpy(mqttUser, custom_mqttUser.getValue(), 32);
  strncpy(mqttPasswd, custom_mqttPasswd.getValue(), 32);
  mqttPort = atoi(mqttPortString);

  // Reset device if lost wifi Connection
  WiFi.onStationModeDisconnected(&lostWifiCallback);
  Serial.println("WiFi connected! User choose hostname '" + String(mdnsName) + "'");

  // save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("Saving config...");
    DynamicJsonDocument json(1024);
    json["mdnsName"] = mdnsName;
    json["mqttServer"] = mqttServer;
    json["mqttPortString"] = mqttPortString;
    json["mqttUser"] = mqttUser;
    json["mqttPasswd"] = mqttPasswd;
    json["ip"] = WiFi.localIP().toString();
    json["gw"] = WiFi.gatewayIP().toString();
    json["sn"] = WiFi.subnetMask().toString();

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    Serial.println("");
    Serial.println("Writing config file");
    serializeJson(json, configFile);
    configFile.close();
    json.clear();
    Serial.println("Config written successfully");
  }

  ticker.detach();

  // keep LED on
  digitalWrite(ledpin, LOW);
  return true;
}

bool setupMQTT() {

  Serial.println("Using MQTT broker " + String(mqttServer));

  // MQTT Connect
  mqtt = new PubSubClient(mqttServer, mqttPort, processJsonMessage, wifi);

  int retries = 5;
  while (!mqtt->connected()) {
    Serial.print("Connecting to MQTT broker...");
    retries--;
    if (mqtt->connect(mdnsName, mqttUser, mqttPasswd)) {
      Serial.println("connected");
      break;
    } else {
      Serial.print("failed with state ");
      Serial.println(mqtt->state());
      if (retries) {
        Serial.print("Will retry in 1s...");
        delay(2000);
        Serial.println("");
      } else {
          Serial.println("Failed to connect and hit timeout");
          // reset and try again, or maybe put it to deep sleep
          ESP.reset();
          delay(1000);
      }
    }
  }

  if (mqtt->subscribe(MQTT_TOPIC_SUB_JSON)) {
    Serial.println("Subscribed to " + String(MQTT_TOPIC_SUB_JSON));
    Serial.println("Will publish to " + String(MQTT_TOPIC_PUB));
  } else {
      Serial.print("Failed to subscribe to " + String(MQTT_TOPIC_SUB_JSON));
      // reset and try again, or maybe put it to deep sleep
      ESP.reset();
      delay(1000);
  }

  return true;
}

bool setupOTA() {
  ArduinoOTA.setPort(8266);
  ArduinoOTA.setHostname(mdnsName);
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("ArduinoOTA started");
  return true;
}

//+=============================================================================
// Setup web server and IR receiver/blaster
//
void setup() {
  // Initialize serial
  Serial.begin(115200);

  // set led pin as output
  pinMode(ledpin, OUTPUT);

  Serial.println("");
  Serial.println("ESP8266 IR Controller");
  pinMode(configpin, INPUT_PULLUP);
  Serial.print("Config pin GPIO");
  Serial.print(configpin);
  Serial.print(" set to: ");
  Serial.println(digitalRead(configpin));
  if (!setupWifi(digitalRead(configpin) == LOW)) {
    return;
  }

  Serial.println("WiFi configuration complete");

  if (strlen(mdnsName) > 0) {
    WiFi.hostname(mdnsName);
  } else {
    WiFi.hostname().toCharArray(mdnsName, 128);
  }

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  wifi_set_sleep_type(LIGHT_SLEEP_T);
  digitalWrite(ledpin, LOW);
  // Turn off the led in 2s
  ticker.attach(2, disableLed);

  Serial.print("Local IP: ");
  Serial.println(WiFi.localIP().toString());

  if (enableMDNSServices) {

    // Configure OTA Update
    if (enableOTAServices) {
      setupOTA();
    }

    // Configure mDNS
    MDNS.addService("mqtt", "tcp", mqttPort); // Announce the ESP as an MQTT service
    Serial.println("mDNS MQTT service added. Hostname is set to " + String(mdnsName));
  }

  setupMQTT();

  irsend1.begin();
  irsend2.begin();
  irsend3.begin();
  irsend4.begin();
  irrecv.enableIRIn();
  Serial.println("Ready to send and receive IR signals");
}

//+=============================================================================
// Split string by character
//
String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

//+=============================================================================
// Return which IRsend object to act on
//
IRsend pickIRsend (int out) {
  switch (out) {
    case 1: return irsend1;
    case 2: return irsend2;
    case 3: return irsend3;
    case 4: return irsend4;
    default: return irsend1;
  }
}

//+=============================================================================
// Display encoding type
//
String encoding(decode_results *results) {
  String output;
  switch (results->decode_type) {
    default:
    case UNKNOWN:      output = "UNKNOWN";            break;
    case NEC:          output = "NEC";                break;
    case SONY:         output = "SONY";               break;
    case RC5:          output = "RC5";                break;
    case RC6:          output = "RC6";                break;
    case DISH:         output = "DISH";               break;
    case SHARP:        output = "SHARP";              break;
    case JVC:          output = "JVC";                break;
    case SANYO:        output = "SANYO";              break;
    case SANYO_LC7461: output = "SANYO_LC7461";       break;
    case MITSUBISHI:   output = "MITSUBISHI";         break;
    case SAMSUNG:      output = "SAMSUNG";            break;
    case LG:           output = "LG";                 break;
    case WHYNTER:      output = "WHYNTER";            break;
    case AIWA_RC_T501: output = "AIWA_RC_T501";       break;
    case PANASONIC:    output = "PANASONIC";          break;
    case DENON:        output = "DENON";              break;
    case COOLIX:       output = "COOLIX";             break;
    case GREE:         output = "GREE";               break;
    case LUTRON:       output = "LUTRON";             break;
  }
  return output;
}

//+=============================================================================
// Code to string
//
void fullCode (decode_results *results) {
  Serial.print("One line: ");
  serialPrintUint64(results->value, 16);
  Serial.print(":");
  Serial.print(encoding(results));
  Serial.print(":");
  Serial.print(results->bits, DEC);
  if (results->repeat) Serial.print(" (Repeat)");
  Serial.println("");
  if (results->overflow)
    Serial.println("WARNING: IR code too long. "
                   "Edit IRController.ino and increase captureBufSize");
}

//+=============================================================================
// Code to JsonObject
//
void cvrtCode(Code& codeData, decode_results *results) {
  strncpy(codeData.data, uint64ToString(results->value, 16).c_str(), 40);
  strncpy(codeData.encoding, encoding(results).c_str(), 14);
  codeData.bits = results->bits;
  String r = "";
      for (uint16_t i = 1; i < results->rawlen; i++) {
      r += results->rawbuf[i] * kRawTick;
      if (i < results->rawlen - 1)
        r += ",";                           // ',' not needed on last one
      //if (!(i & 1)) r += " ";
    }
  codeData.raw = r;
  if (results->decode_type != UNKNOWN) {
    strncpy(codeData.address, ("0x" + String(results->address, HEX)).c_str(), 20);
    strncpy(codeData.command, ("0x" + String(results->command, HEX)).c_str(), 40);
  } else {
    strncpy(codeData.address, "0x0", 20);
    strncpy(codeData.command, "0x0", 40);
  }
}

//+=============================================================================
// Dump out the decode_results structure.
//
void dumpInfo(decode_results *results) {
  if (results->overflow)
    Serial.println("WARNING: IR code too long. "
                   "Edit IRrecv.h and increase RAWBUF");

  // Show Encoding standard
  Serial.print("Encoding  : ");
  Serial.print(encoding(results));
  Serial.println("");

  // Show Code & length
  Serial.print("Code      : ");
  serialPrintUint64(results->value, 16);
  Serial.print(" (");
  Serial.print(results->bits, DEC);
  Serial.println(" bits)");
}

//+=============================================================================
// Dump out the decode_results structure.
//
void dumpRaw(decode_results *results) {
  // Print Raw data
  Serial.print("Timing[");
  Serial.print(results->rawlen - 1, DEC);
  Serial.println("]: ");

  for (uint16_t i = 1;  i < results->rawlen;  i++) {
    if (i % 100 == 0)
      yield();  // Preemptive yield every 100th entry to feed the WDT.
    uint32_t x = results->rawbuf[i] * kRawTick;
    if (!(i & 1)) {  // even
      Serial.print("-");
      if (x < 1000) Serial.print(" ");
      if (x < 100) Serial.print(" ");
      Serial.print(x, DEC);
    } else {  // odd
      Serial.print("     ");
      Serial.print("+");
      if (x < 1000) Serial.print(" ");
      if (x < 100) Serial.print(" ");
      Serial.print(x, DEC);
      if (i < results->rawlen - 1)
        Serial.print(", ");  // ',' not needed for last one
    }
    if (!(i % 8)) Serial.println("");
  }
  Serial.println("");  // Newline
}

//+=============================================================================
// Dump out the decode_results structure.
//
void dumpCode(decode_results *results) {
  // Start declaration
  Serial.print("uint16_t  ");              // variable type
  Serial.print("rawData[");                // array name
  Serial.print(results->rawlen - 1, DEC);  // array size
  Serial.print("] = {");                   // Start declaration

  // Dump data
  for (uint16_t i = 1; i < results->rawlen; i++) {
    Serial.print(results->rawbuf[i] * kRawTick, DEC);
    if (i < results->rawlen - 1)
      Serial.print(",");  // ',' not needed on last one
    if (!(i & 1)) Serial.print(" ");
  }

  // End declaration
  Serial.print("};");  //

  // Comment
  Serial.print("  // ");
  Serial.print(encoding(results));
  Serial.print(" ");
  serialPrintUint64(results->value, 16);

  // Newline
  Serial.println("");

  // Now dump "known" codes
  if (results->decode_type != UNKNOWN) {
    // Some protocols have an address &/or command.
    // NOTE: It will ignore the atypical case when a message has been decoded
    // but the address & the command are both 0.
    if (results->address > 0 || results->command > 0) {
      Serial.print("uint32_t  address = 0x");
      Serial.print(results->address, HEX);
      Serial.println(";");
      Serial.print("uint32_t  command = 0x");
      Serial.print(results->command, HEX);
      Serial.println(";");
    }

    // All protocols have data
    Serial.print("uint64_t  data = 0x");
    serialPrintUint64(results->value, 16);
    Serial.println(";");
  }
}

//+=============================================================================
// Binary value to hex
//
String bin2hex(const uint8_t* bin, const int length) {
  String hex = "";

  for (int i = 0; i < length; i++) {
    if (bin[i] < 16) {
      hex += "0";
    }
    hex += String(bin[i], HEX);
  }

  return hex;
}

//+=============================================================================
// Send IR codes to variety of sources
//
void irblast(String type, String dataStr, unsigned int len, int rdelay, int pulse, int pdelay, int repeat, long address, IRsend irsend) {
  Serial.println("Blasting off");
  type.toLowerCase();
  uint64_t data = strtoull(("0x" + dataStr).c_str(), 0, 0);
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");
  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      serialPrintUint64(data, HEX);
      Serial.print(":");
      Serial.print(type);
      Serial.print(":");
      Serial.println(len);
      if (type == "nec") {
        irsend.sendNEC(data, len);
      } else if (type == "sony") {
        irsend.sendSony(data, len);
      } else if (type == "coolix") {
        irsend.sendCOOLIX(data, len);
      } else if (type == "whynter") {
        irsend.sendWhynter(data, len);
      } else if (type == "panasonic") {
        Serial.print("Address: ");
        Serial.println(address);
        irsend.sendPanasonic(address, data);
      } else if (type == "jvc") {
        irsend.sendJVC(data, len, 0);
      } else if (type == "samsung") {
        irsend.sendSAMSUNG(data, len);
      } else if (type == "sharpraw") {
        irsend.sendSharpRaw(data, len);
      } else if (type == "dish") {
        irsend.sendDISH(data, len);
      } else if (type == "rc5") {
        irsend.sendRC5(data, len);
      } else if (type == "rc6") {
        irsend.sendRC6(data, len);
      } else if (type == "denon") {
        irsend.sendDenon(data, len);
      } else if (type == "lg") {
        irsend.sendLG(data, len);
      } else if (type == "sharp") {
        irsend.sendSharpRaw(data, len);
      } else if (type == "rcmm") {
        irsend.sendRCMM(data, len);
      } else if (type == "gree") {
        irsend.sendGree(data, len);
      } else if (type == "lutron") {
        irsend.sendLutron(data, len);
      } else if (type == "roomba") {
        roomba_send(atoi(dataStr.c_str()), pulse, pdelay, irsend);
      }
      if (p + 1 < pulse) delay(pdelay);
    }
    if (r + 1 < repeat) delay(rdelay);
  }

  Serial.println("Transmission complete");

  strncpy(last_send.data, dataStr.c_str(), 40);
  last_send.bits = len;
  strncpy(last_send.encoding, type.c_str(), 14);
  strncpy(last_send.address, ("0x" + String(address, HEX)).c_str(), 20);
  last_send.valid = true;

  resetReceive();
}

void pronto(JsonArray &pronto, int rdelay, int pulse, int pdelay, int repeat, IRsend irsend) {
  Serial.println("Pronto transmit");
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");
  int psize = pronto.size();
  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      Serial.println("Sending pronto code");
      uint16_t output[psize];
      for (int d = 0; d < psize; d++) {
        String phexp = pronto[d];
        output[d] = strtoul(phexp.c_str(), 0, 0);
      }
      irsend.sendPronto(output, psize);
      if (p + 1 < pulse) delay(pdelay);
    }
    if (r + 1 < repeat) delay(rdelay);
  }

  Serial.println("Transmission complete");

  strncpy(last_send.data, "", 40);
  last_send.bits = psize;
  strncpy(last_send.encoding, "PRONTO", 14);
  strncpy(last_send.address, "0x0", 20);
  last_send.valid = true;

  resetReceive();
}

void rawblast(JsonArray &raw, int khz, int rdelay, int pulse, int pdelay, int repeat, IRsend irsend,int duty) {
  Serial.println("Raw transmit");
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");
  // Repeat Loop
  for (int r = 0; r < repeat; r++) {
    // Pulse Loop
    for (int p = 0; p < pulse; p++) {
      Serial.println("Sending code");
      irsend.enableIROut(khz,duty);
      for (unsigned int i = 0; i < raw.size(); i++) {
        int val = raw[i];
        if (i & 1) irsend.space(std::max(0, val));
        else       irsend.mark(val);
      }
      irsend.space(0);
      if (p + 1 < pulse) delay(pdelay);
    }
    if (r + 1 < repeat) delay(rdelay);
  }

  Serial.println("Transmission complete");

  strncpy(last_send.data, "", 40);
  last_send.bits = raw.size();
  strncpy(last_send.encoding, "RAW", 14);
  strncpy(last_send.address, "0x0", 20);
  last_send.valid = true;

  resetReceive();
}

void roomba_send(int code, int pulse, int pdelay, IRsend irsend) {
  Serial.print("Sending Roomba code ");
  Serial.println(code);
  holdReceive = true;
  Serial.println("Blocking incoming IR signals");

  int length = 8;
  uint16_t raw[length * 2];
  unsigned int one_pulse = 3000;
  unsigned int one_break = 1000;
  unsigned int zero_pulse = one_break;
  unsigned int zero_break = one_pulse;
  uint16_t len = 15;
  uint16_t hz = 38;

  int arrayposition = 0;
  for (int counter = length - 1; counter >= 0; --counter) {
    if (code & (1 << counter)) {
      raw[arrayposition] = one_pulse;
      raw[arrayposition + 1] = one_break;
    }
    else {
      raw[arrayposition] = zero_pulse;
      raw[arrayposition + 1] = zero_break;
    }
    arrayposition = arrayposition + 2;
  }
  for (int i = 0; i < pulse; i++) {
    irsend.sendRaw(raw, len, hz);
    delay(pdelay);
  }

  resetReceive();
}

void copyCode (Code& c1, Code& c2) {
  strncpy(c2.data, c1.data, 40);
  strncpy(c2.encoding, c1.encoding, 14);
  strncpy(c2.address, c1.address, 20);
  strncpy(c2.command, c1.command, 40);
  c2.bits = c1.bits;
  c2.raw = c1.raw;
  c2.timestamp = c1.timestamp;
  c2.valid = c1.valid;
}

void loop() {
  ArduinoOTA.handle();
  mqtt->loop();

  decode_results results;                                        // Somewhere to store the results
  if (irrecv.decode(&results) && !holdReceive) {                  // Grab an IR code
    Serial.println("Signal received:");
    fullCode(&results);                                           // Print the singleline value
    dumpCode(&results);                                           // Output the results as source code
    cvrtCode(last_recv, &results);                                // Store the results
    Serial.println("");                                           // Blank line between entries
    irrecv.resume();                                              // Prepare for the next value
    digitalWrite(ledpin, LOW);                                    // Turn on the LED for 0.5 seconds
    ticker.attach(0.5, disableLed);
    processReceived(results);
  }

  delay(100);
}
