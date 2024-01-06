#include <WiFiUdp.h>
#include <EEPROM.h>
#include <ESPAsyncWebSrv.h>
#include <NTP.h>
#include <ArduinoJson.h>

#define EEPROM_SIZE 512

#define EEPROM_WIFI_SSID_ADDRESS 0
#define EEPROM_WIFI_SSID_LEN 32
#define EEPROM_WIFI_PASSWORD_ADDRESS 40
#define EEPROM_WIFI_PASSWORD_LEN 64

#define EEPROM_TIME_ADDRESS 200
#define EEPROM_TIME_START 202

#define ENABLE_A 14
#define IN_1 12
#define IN_2 13

AsyncWebServer server(80);

WiFiUDP ntpUDP;
NTP ntp(ntpUDP);

unsigned long pause = 0;

void setup() {
  millis();
	// Set all the motor control pins to outputs
  pinMode(LED_BUILTIN, OUTPUT);
	pinMode(ENABLE_A, OUTPUT);
	pinMode(IN_1, OUTPUT);
	pinMode(IN_2, OUTPUT);
	turnOff();


  digitalWrite(LED_BUILTIN, LOW);

  //-------------------------------------------------------
  // Serial init
  Serial.begin(115200);
  while (!Serial) {}
  

  Serial.println("====================");
  Serial.println("====================");
  Serial.println("====================");
  Serial.println("Booting..");

  //-------------------------------------------------------
  // Inititalize EEPROM

  Serial.println("Initializing and reading EEPROM...");

  EEPROM.begin(EEPROM_SIZE);
  String wifiSsid     = readSsid();
  String wifiPassword = readPassword();
  Serial.printf("  WiFi SSID:     %s\n", wifiSsid.c_str());
  Serial.printf("  Dials address: %d\n", readDialsAddress());
  Serial.printf("  Dials:         %d\n", readDials());
  
  //-------------------------------------------------------
  // Connect to Wi-Fi

  Serial.print("Connecting to wifi");

  WiFi.begin(wifiSsid, wifiPassword);
  
  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - startTime >= 30000) {
      Serial.print(" connection timed out.");
      break;
    }
    delay(2000);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());

    Serial.println("Starting NTP...");

    ntp.updateInterval(600000);
    ntp.ruleDST("CEST", Last, Sun, Mar, 2, 120); // last sunday in march 2:00, timetone +120min (+1 GMT + 1h summertime offset)
    ntp.ruleSTD("CET", Last, Sun, Oct, 3, 60); // last sunday in october 3:00, timezone +60min (+1 GMT)
    ntp.begin();

    Serial.println("Blinking IP...");

    digitalWrite(LED_BUILTIN, HIGH);
    delay(1000);
    blinkByte(WiFi.localIP()[3]);

  } else {

    WiFi.disconnect();
    delay(1000);
    Serial.println("Failed to connect to WiFi starting in AP mode...");
    WiFi.setSleep(WIFI_PS_NONE);
    WiFi.softAP("CLOCK");
    IPAddress apIP(192, 168, 100, 1);

    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    IPAddress ip = WiFi.softAPIP();
    Serial.print("SoftAP IP address: ");
    Serial.println(ip);
    pause = millis();
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    if (request->hasParam("ssid")) {
      Serial.println("got ssid");
      writeStringToEEPROM(
        EEPROM_WIFI_SSID_ADDRESS,
        request->getParam("ssid")->value().c_str(),
        EEPROM_WIFI_SSID_LEN);
    }
    if (request->hasParam("password")) {
      Serial.println("got password");
      writeStringToEEPROM(
        EEPROM_WIFI_PASSWORD_ADDRESS,
        request->getParam("password")->value().c_str(),
        EEPROM_WIFI_PASSWORD_LEN);
    }

    if (request->hasParam("dialsaddress")) {
      Serial.print("got dials address: ");
      int address = atoi(request->getParam("dialsaddress")->value().c_str());
      Serial.println(address);
      writeDialsAddress(address);
    }

    if (request->hasParam("dials")) {
      Serial.println("got dials");
      int dials = atoi(request->getParam("dials")->value().c_str());
      if (dials >= 0 && dials < 720) {
        writeDials(dials);
      }
    }

    if (request->hasParam("pause")) {
      Serial.println("got pause");
      if (atoi(request->getParam("pause")->value().c_str()) != 0) {
        pause = millis();
      }
    }

    String reply = "Settings: \n";
    reply += " ssid:          " + readSsid() + "\n";
    reply += " password:      hidden\n";
    reply += " dialsaddress:  " + String(readDialsAddress()) + "\n";
    reply += " dials:         " + String(readDials()) + "\n";
    reply += " pause:         " + String(pause) + "\n";
    reply += "\n";
    reply += "NTP time (minutes): " + String(realClock()) + "\n";
    reply += "NTP time (format):  " + String(ntp.formattedTime("%F %T")) + "\n";

    request->send(200, "text/plain; charset=utf-8", reply);
  });

  server.begin();


}

void loop() {

  if (pause != 0) {
    // soft AP mode or in pause mode, do not do anything
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(100);

    if ((millis() - pause) / 60000 > 5) {
      // but have a reboot after 5 minutes so that we do not end up in this state
      ESP.restart();
    }
    return;
  }

  ntp.update();

  int real = realClock();
  int dials = readDials();

  // check if we are before the
  int diff = real - dials;
  if (diff < 0) {
    diff += 720;
  }

  Serial.printf("(real) %d - (dials) %d = (diff) %d - ", real, dials, diff);

  // normal case, go forward
  //   real =  39, dials =  38, diff =  1
  //   real =   0, dials = 719, diff =  1
  //   real =  10, dials = 713, diff = 17

  // future, just wait for the time to catch up
  //   real =  39, dials =  40, diff = 719
  //   real = 719, dials =   0, diff = 719

  if (diff == 0) {
    // no change, don't do anything
    Serial.println("no need to update..");
  } else if (diff > (720 - 70)) {
    // just wait for the time to catch up
    Serial.println("wait for the time to catch up..");
  } else {
    // one step in ahead folks
    Serial.println("tick tack!");

    tick(dials % 2 == 0);

    if (readDials() == dials) {
      // only update the dials if the dials hasn't changed by "someone" else
      dials = (dials + 1) % 720;
      writeDials(dials);
    }
  } 
  
  if (real == 240 || WiFi.status() != WL_CONNECTED) {
    Serial.println("Restart controller..");
    delay(10000);
    ESP.restart();
  }

  delay(1000);
}


int realClock() {
  return (60 * (ntp.hours() % 12)) + ntp.minutes();
}

//==============================================================
// Motor handling

void tick(bool positive) {
  pulse(positive);
  delay(1000);
}

void pulse(bool fwd) {
  digitalWrite(IN_1, fwd ? HIGH : LOW);
	digitalWrite(IN_2, fwd ? LOW : HIGH);
  analogWrite(ENABLE_A, 255);
	delay(1000);

  turnOff();
}

void turnOff() {
  analogWrite(ENABLE_A, 0);
  digitalWrite(IN_1, LOW);
	digitalWrite(IN_2, LOW);
}

//==============================================================
// EEPROM handling

int readDialsAddress() {
  return ((int) EEPROM.read(EEPROM_TIME_ADDRESS + 1)) << 8 | EEPROM.read(EEPROM_TIME_ADDRESS);
}

void nextDialsAddress() {
  writeDialsAddress(readDialsAddress() + 1);
}

void writeDialsAddress(int address) {
  if (address >= EEPROM_SIZE) {
    Serial.println("ERROR: Address is > EEPROM_SIZE, resetting to 0");
    address = 0; // just restart.. what to do
  }
  EEPROM.write(EEPROM_TIME_ADDRESS + 1, (address >> 8) & 0xFF);
  EEPROM.write(EEPROM_TIME_ADDRESS, address & 0xFF);
  EEPROM.commit();
}

int readDials() {
  int address = EEPROM_TIME_START + readDialsAddress();
  return ((int) EEPROM.read(address + 1)) << 8 | EEPROM.read(address);
}

void writeDials(int time) {
  while (true) {
    int address = EEPROM_TIME_START + readDialsAddress();
    if (address > EEPROM_SIZE) {
      Serial.println("ERROR: Cannot write to this address. Resetting to 0.");
      writeDialsAddress(0);
      address = EEPROM_TIME_ADDRESS + readDialsAddress();
    }
    
    EEPROM.write(address + 1, (time >> 8) & 0xFF);
    EEPROM.write(address, time & 0xFF);
    EEPROM.commit();

    if (readDials() == time) {
      break;
    }

    nextDialsAddress();
  }
}


String readSsid() {
  return readStringFromEEPROM(EEPROM_WIFI_SSID_ADDRESS, EEPROM_WIFI_SSID_LEN);
}


String readPassword() {
  return readStringFromEEPROM(EEPROM_WIFI_PASSWORD_ADDRESS, EEPROM_WIFI_PASSWORD_LEN);
}


String readStringFromEEPROM(int address, int maxLength) {
  String result = "";
  for (int i = 0; i < maxLength; i++) {
    char c = EEPROM.read(address + i);
    if (c == '\0') {
      break; // Null terminator found, end of string
    }
    result += c;
  }
  return result;
}

void writeStringToEEPROM(int address, const char* str, size_t maxLength) {
  int length = min(strlen(str), maxLength);
  for (int i = 0; i < length; i++) {
    EEPROM.write(address + i, str[i]);
  }
  EEPROM.write(address + length, '\0'); // Null-terminate the string
  EEPROM.commit();
}


//------------------------------------------------
// other


void blinkByte(byte value)
{
  byte part;
  byte i;

  part = value / 100 % 10;
  //Serial.print(part); Serial.print(" : ");
  for (i = 0; i < part; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
  }
  delay (1000);

  part = value / 10 % 10;
  //Serial.print(part); Serial.print(" : ");
  for (i = 0; i < part; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
  }
  delay(1000);

  part = value % 10;
  //Serial.println(part);
  for (i = 0; i < part; i++) {
    digitalWrite(LED_BUILTIN, LOW);
    delay(100);
    digitalWrite(LED_BUILTIN, HIGH);
    delay(500);
  }
}
