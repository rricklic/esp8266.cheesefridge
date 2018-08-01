#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>

#include "DHT.h"

////////////////////////////////////////////////////////////////////////////////
//                                    Notes
//
// Board: NodeMCU 1.0 ESP8266-12E
// LED: D2/GPIO2 (Onboard LED)
// Relay (cold): D5/GPIO14
// Relay (hot): D6/GPIO12
// Relay (humidity): D7/GPIO13
// DHT11 (temperature and humidity sensor): D1/GPIO5
//
// "DHT.h" Used: /home/rricklic/git/arduino/libraries/DHT_sensor_library
// Not used: /home/rricklic/git/arduino/libraries/arduino_576464
//
// Access Point:
//   Default IP address: 192.168.4.1
//   SSID: ESP8266
//   Password: fake1234
//   Channel: 6
//
// EEPROM:
//   0-31: SSID
//   32-95: Password
//   96-101: Target temperature
//   102-107: Target humidity
//   108-112: Minimum heat time off in ms
//   113-117: Minimum cool time off in ms
//   118-122: Minimum humidifier time off in ms
////////////////////////////////////////////////////////////////////////////////

//WIFI
#define DEFAULT_SSID "ESP8266"
#define DEFAULT_PASSWORD "fake1234" //must be at least 8 characters
#define DEFAULT_CHANNEL 6

//EEPROM
#define EEPROM_SIZE 512
#define EEPROM_MIN 0
#define EEPROM_SSID_MIN 0
#define EEPROM_SSID_MAX 31
#define EEPROM_PASSWORD_MIN 32
#define EEPROM_PASSWORD_MAX 95
#define EEPROM_TARGET_TEMP_MIN 96
#define EEPROM_TARGET_TEMP_MAX 101
#define EEPROM_TARGET_HUMID_MIN 102
#define EEPROM_TARGET_HUMID_MAX 107
#define EEPROM_MIN_HEAT_OFF_MIN 108
#define EEPROM_MIN_HEAT_OFF_MAX 112
#define EEPROM_MIN_COOL_OFF_MIN 113
#define EEPROM_MIN_COOL_OFF_MAX 117
#define EEPROM_MIN_HUMID_OFF_MIN 118
#define EEPROM_MIN_HUMID_OFF_MAX 122
#define EEPROM_MAX 122

//Pins
#define LED_PIN 2 //D4, GPIO2
#define COOL_RELAY_PIN D5 //GPIO14
#define HEAT_RELAY_PIN D6 //GPIO12
#define HUMID_RELAY_PIN D7 //GPIO13
#define DHTPIN 5 //D1, GPIO5
#define DHTTYPE DHT11

ESP8266WebServer server(80);
String content;
String availableNetworks;
int statusCode;

DHT dht = DHT(DHTPIN, DHTTYPE);
float temperature = -1.0;
float humidity = -1.0;
float targetTemperature = -1.0;
float targetHumidity = -1.0;
long minMillisHeatOff = 60000;
long minMillisCoolOff = 60000;
long minMillisHumidOff = 60000;
long maxMillisHeatOn = 60000; //TODO: support
long maxMillisCoolOn = 60000; //TODO: support
long maxMillisHumidOn = 60000; //TODO: support
long now = millis();
long coolLastTimeOff = millis();
long heatLastTimeOff = millis();
long humidLastTimeOff = millis();
long coolLastTimeOn = -1;
long heatLastTimeOn = -1;
long humidLastTimeOn = -1;
int humidRelayPin = LOW;
int heatRelayPin = LOW;
int coolRelayPin = LOW;

////////////////////////////////////////////////////////////////////////////////
void setup() 
{
   EEPROM.begin(EEPROM_SIZE);
   WiFi.disconnect();

   Serial.begin(115200);
   Serial.println();

   pinMode(LED_PIN, OUTPUT);
   pinMode(COOL_RELAY_PIN, OUTPUT);
   pinMode(HEAT_RELAY_PIN, OUTPUT);
   pinMode(HUMID_RELAY_PIN, OUTPUT);

   digitalWrite(LED_PIN, LOW);
   digitalWrite(COOL_RELAY_PIN, LOW);
   digitalWrite(HEAT_RELAY_PIN, LOW);
   digitalWrite(HUMID_RELAY_PIN, LOW);

   dht.begin();
    
   String eepromSSID = readEEPROM(EEPROM_SSID_MIN, EEPROM_SSID_MAX);
   String eepromPassword = readEEPROM(EEPROM_PASSWORD_MIN, EEPROM_PASSWORD_MAX);
   String eepromTargetTemp = readEEPROM(EEPROM_TARGET_TEMP_MIN, EEPROM_TARGET_TEMP_MAX);
   String eepromTargetHumidity = readEEPROM(EEPROM_TARGET_HUMID_MIN, EEPROM_TARGET_HUMID_MAX);
   String eepromMinHeatOff = readEEPROM(EEPROM_MIN_HEAT_OFF_MIN, EEPROM_MIN_HEAT_OFF_MAX);
   String eepromMinCoolOff = readEEPROM(EEPROM_MIN_COOL_OFF_MIN, EEPROM_MIN_COOL_OFF_MAX);
   String eepromMinHumidityOff = readEEPROM(EEPROM_MIN_HUMID_OFF_MIN, EEPROM_MIN_HUMID_OFF_MAX);

   targetTemperature = eepromTargetTemp.length() ? eepromTargetTemp.toFloat() : -1.0;
   targetHumidity = eepromTargetHumidity.length() ? eepromTargetHumidity.toFloat() : -1.0;
   minMillisHeatOff = eepromMinHeatOff.length() ? eepromMinHeatOff.toInt() : 60000;
   minMillisCoolOff = eepromMinCoolOff.length() ? eepromMinCoolOff.toInt() : 60000;
   minMillisHumidOff = eepromMinHumidityOff.length() ? eepromMinHumidityOff.toInt() : 60000;

   if(eepromSSID.length()) {
      setupStation(eepromSSID, eepromPassword);
   }
   else {
      setupAccessPoint();
   }
}

////////////////////////////////////////////////////////////////////////////////
void loop()
{
   temperature = fetchDHT11Temperature();
   humidity = fetchDHT11Humidity();

   now = millis();

   humidRelayPin = digitalRead(HUMID_RELAY_PIN);
   heatRelayPin = digitalRead(HEAT_RELAY_PIN);
   coolRelayPin = digitalRead(COOL_RELAY_PIN);

   if(humidRelayPin == LOW && humidity < targetHumidity && isTimeDurationExceeded(now, humidLastTimeOff, minMillisHumidOff)) {
      Serial.println("Turn HUMID ON");
      humidLastTimeOn = millis();
      digitalWrite(HUMID_RELAY_PIN, HIGH);
   }
   else if(humidRelayPin == HIGH && humidity > targetHumidity) {
      Serial.println("Turn HUMID OFF");
      humidLastTimeOff = millis();
      digitalWrite(HUMID_RELAY_PIN, LOW);
   }

   if((heatRelayPin == LOW || coolRelayPin == HIGH) && temperature < targetTemperature) {
      if(isTimeDurationExceeded(now, heatLastTimeOff, minMillisHeatOff)) {
         Serial.println("Turn HEAT ON");
         heatLastTimeOn = millis();
         digitalWrite(HEAT_RELAY_PIN, HIGH);
      }

      if(coolRelayPin == HIGH) {
         Serial.println("Turn COOL OFF");
         coolLastTimeOff = millis();
         digitalWrite(COOL_RELAY_PIN, LOW);
      }
   }
   else if((heatRelayPin == HIGH || coolRelayPin == LOW) && temperature > targetTemperature) {
      if(heatRelayPin == HIGH) {
         Serial.println("Turn HEAT OFF");
         heatLastTimeOff = millis();      
         digitalWrite(HEAT_RELAY_PIN, LOW);
      }

      if(isTimeDurationExceeded(now, coolLastTimeOff, minMillisCoolOff)) {
         Serial.println("Turn COOL ON");
         digitalWrite(COOL_RELAY_PIN, HIGH);
         coolLastTimeOn = millis();
      }
   }

   server.handleClient();
}

////////////////////////////////////////////////////////////
boolean isTimeDurationExceeded(long now, long lastTime, long duration)
{
   return lastTime < 0 || (now - lastTime >= duration);
}

////////////////////////////////////////////////////////////
float fetchDHT11Humidity()
{
   return dht.readHumidity();
}

////////////////////////////////////////////////////////////
float fetchDHT11Temperature()
{
   return dht.readTemperature(true);
}

////////////////////////////////////////////////////////////////////////////////
String scanAvailableNetworks()
{
   WiFi.mode(WIFI_STA);
   WiFi.disconnect();

   String result = "";
   Serial.print("Scanning for available networks...");
   int numNetworks = WiFi.scanNetworks();
   Serial.println(" done");
   if(numNetworks == 0) {
      result = "No networks found";
   }
   else {
      Serial.print(numNetworks);
      Serial.println(" networks found");

      result += numNetworks;
      result += " networks found<br/>";
      for(int index = 0; index < numNetworks; ++index) {
         Serial.println(WiFi.SSID(index));
         result += (index + 1); 
         result += ": ";
         result += WiFi.SSID(index);
         result += " (";
         result += WiFi.RSSI(index);
         result += ")";
         result += (WiFi.encryptionType(index) == ENC_TYPE_NONE) ? " <br/>" : "<br/>";
         //TODO: use select html element
         delay(10);
      }
   }

   Serial.println("RESULTS= " + result);
   return result;
}

////////////////////////////////////////////////////////////////////////////////
void setupAccessPoint()
{
   availableNetworks = scanAvailableNetworks();
   
   boolean result = WiFi.softAP(DEFAULT_SSID, DEFAULT_PASSWORD, DEFAULT_CHANNEL);
   if(result) {
      setupAccessPointServer();
   }
   else {
      Serial.println("Failed to setup as access point server");
   }

   flashLed(10, 100, 100);
}

////////////////////////////////////////////////////////////////////////////////
void setupAccessPointServer()
{
   Serial.print("Setting ESP8266 as access point server...");
   server.on("/", []() {
      IPAddress ip = WiFi.softAPIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 (access point) at ";
      content += ipStr;
      content += "<p>";
      content += "</p><form method='get' action='setting'><label>SSID: </label><input type='text' name='ssid' length=32><input type='password' name='pass' length=64><input type='submit'></form>";
      content += availableNetworks;
      content += "</html>";
      server.send(200, "text/html", content);  
   });
   server.on("/setting", []() {
      String ssid = server.arg("ssid");
      String password = server.arg("pass");
      if(ssid.length() > 0 && password.length() > 0) {
         Serial.println("Clearing EEPROM...");
         for(int index = EEPROM_MIN; index <= EEPROM_MAX; ++index) { EEPROM.write(index, 0); }

         Serial.println("Writing ssid to EEPROM...");
         writeEEPROM(ssid, EEPROM_SSID_MIN, EEPROM_SSID_MAX);

         Serial.println("Writing password to EEPROM..."); 
         writeEEPROM(password, EEPROM_PASSWORD_MIN, EEPROM_PASSWORD_MAX);

         EEPROM.commit();
         EEPROM.end();
         
         content = "{\"Success\": \"saved network credentials to EEPROM... setting up as station server\"}";
         statusCode = 200;
         server.send(200, "text/html", content);
         delay(5000);

         server.stop();
         server.close();
         WiFi.disconnect();
         ESP.restart();
      } 
      else {
         content = "{\"Error\": \"404 not found\"}";
         statusCode = 404;
         Serial.println("Sending 404");
      }
      server.send(statusCode, "application/json", content);
   });  

   server.begin();
   Serial.println(" done");
}

////////////////////////////////////////////////////////////////////////////////
String readEEPROM(int startIndex, int endIndex)
{
   char c;
   String string = "";
   for(int index = startIndex; index <= endIndex; ++index) {
      c = char(EEPROM.read(index));
      if(c == 0) { break; }
      string += c;
   }

   string[string.length()] = '\0';
   return string;
}

////////////////////////////////////////////////////////////////////////////////
void writeEEPROM(String string, int startIndex, int endIndex)
{
   int index;
   for(index = startIndex; index-startIndex < string.length() && index <= endIndex; ++index) {
      EEPROM.write(index, string[index-startIndex]);
   }

   if(index <= endIndex) {
      EEPROM.write(index, '\0');
   }
}

////////////////////////////////////////////////////////////////////////////////
void setupStation(String ssid, String password)
{
   WiFi.begin(ssid.c_str(), password.c_str());
   if(testWifi()) {
      setupStationServer();
      flashLed(3, 500, 100);
   }
   else {
      setupAccessPoint();
   }
}

////////////////////////////////////////////////////////////////////////////////
bool testWifi()
{
   int tries = 0;
   Serial.println("Testing Wifi connection...");  
   while(tries < 15) {
      if(WiFi.status() == WL_CONNECTED) { 
         Serial.println(" - connected"); 
         return true; 
      }
      delay(1000);
      Serial.print(WiFi.status());    
      tries++;
   }
   Serial.println("");
   Serial.println("Connection timed out");
   return false;
}

////////////////////////////////////////////////////////////////////////////////
void setupStationServer()
{
   Serial.print("Setting ESP8266 as station server...");
   //Index
   server.on("/", []() {
      IPAddress ip = WiFi.localIP();
      String ipStr = String(ip[0]) + '.' + String(ip[1]) + '.' + String(ip[2]) + '.' + String(ip[3]);
      content = "<!DOCTYPE HTML>\r\n<html>Hello from ESP8266 (station) at ";
      content += ipStr;
      content += "<p>";
      content += "</p><form method='get' action='setting'> Current Temperature: " + 
            String(temperature) + "<br/><label>Target Temperature: </label><input name='target_temp' length=6 value='" + String(targetTemperature) + "'><br/>" +
            "Current Humidity: " + String(humidity) + "<br/><label>Target Humidity: </label><input name='target_humidity' length=6 value='" + String(targetHumidity) + "'><br/>" +
            "<label>Min Heat Off: </label><input name='min_heat_off' length=5 value='" + String(minMillisHeatOff) + "'><br/>" +
            "<label>Min Cool Off: </label><input name='min_cool_off' length=5 value='" + String(minMillisCoolOff) + "'><br/>" +
            "<label>Min Humidity Off: </label><input name='min_humid_off' length=5 value='" + String(minMillisHumidOff) + "'><br/>" +
            "<input type='submit' value='Set Values'></form>" +
            "<form method='get' action='cleareeprom'><input type='submit' value = 'Clear EEPROM'></form><br/>";
      content += "<table><tr><th>Heat</th><th>Cool</th><th>Humidity</th></tr>";
      content += "<tr>";
      content += "<th><svg height=\"50\" width=\"50\"><circle cx=\"25\" cy=\"25\" r=\"20\" stroke=\"black\" stroke-width=\"3\" fill=\"" + String(heatRelayPin == HIGH ? "red" : "grey") + "\"/></svg></th>";
      content += "<th><svg height=\"50\" width=\"50\"><circle cx=\"25\" cy=\"25\" r=\"20\" stroke=\"black\" stroke-width=\"3\" fill=\"" + String(coolRelayPin == HIGH ? "blue" : "grey") + "\"/></svg></th>";
      content += "<th><svg height=\"50\" width=\"50\"><circle cx=\"25\" cy=\"25\" r=\"20\" stroke=\"black\" stroke-width=\"3\" fill=\"" + String(humidRelayPin == HIGH ? "cyan" : "grey") + "\"/></svg></th>";
      content += "</tr>";
      content += "<tr><th>" + (heatRelayPin == HIGH ? String((now - heatLastTimeOn) / 1000.0) + " seconds on" : String((now - heatLastTimeOff) / 1000.0) + " seconds off");
      content += "</th><th>" + (coolRelayPin == HIGH ? String((now - coolLastTimeOn) / 1000.0) + " seconds on" : String((now - coolLastTimeOff) / 1000.0) + " seconds off");
      content += "</th><th>" + (humidRelayPin == HIGH ? String((now - humidLastTimeOn) / 1000.0) + " seconds on" : String((now - humidLastTimeOff) / 1000.0) + " seconds off");
      content += "</th></tr>";
      content += "</table></html>";
      server.send(200, "text/html", content);      
   });
   //Setting
   server.on("/setting", []() {
      String targetTemperatureArg = server.arg("target_temp");
      Serial.println("Writing targetTemperature to EEPROM...");
      writeEEPROM(targetTemperatureArg, EEPROM_TARGET_TEMP_MIN, EEPROM_TARGET_TEMP_MAX);
      targetTemperature = targetTemperatureArg.toFloat();

      String targetHumidityArg = server.arg("target_humidity");
      Serial.println("Writing targetHumidity to EEPROM...");
      writeEEPROM(targetHumidityArg, EEPROM_TARGET_HUMID_MIN, EEPROM_TARGET_HUMID_MAX);
      targetHumidity = targetHumidityArg.toFloat();

      String minHeatOffArg = server.arg("min_heat_off");
      Serial.println("Writing minHeatOff to EEPROM...");
      writeEEPROM(minHeatOffArg, EEPROM_MIN_HEAT_OFF_MIN, EEPROM_MIN_HEAT_OFF_MAX);
      minMillisHeatOff = minHeatOffArg.toInt();

      String minCoolOffArg = server.arg("min_cool_off");
      Serial.println("Writing minCoolOff to EEPROM...");
      writeEEPROM(minCoolOffArg, EEPROM_MIN_COOL_OFF_MIN, EEPROM_MIN_COOL_OFF_MAX);
      minMillisCoolOff = minCoolOffArg.toInt();

      String minHumidOffArg = server.arg("min_humid_off");
      Serial.println("Writing minHumidOff to EEPROM...");
      writeEEPROM(minHumidOffArg, EEPROM_MIN_HUMID_OFF_MIN, EEPROM_MIN_HUMID_OFF_MAX);
      minMillisHumidOff = minHumidOffArg.toInt();

      EEPROM.commit();
      EEPROM.end();

      //server.send(200, "text/html", content);
      server.sendHeader("Location", String("/"), true);
      server.send(302, "text/plain", "");
   });
   //Clear EEPROM
   server.on("/cleareeprom", []() {      
      Serial.println("clearing eeprom");
      for(int i = EEPROM_MIN; i <= EEPROM_MAX; ++i) { EEPROM.write(i, 0); }
      EEPROM.commit();
      EEPROM.end();

      content = "{\"Success\": \"Cleared EEPROM... restarting\"}";
      statusCode = 200;
      server.send(200, "text/html", content);
      delay(5000);

      server.stop();
      server.close();
      WiFi.disconnect();
      ESP.restart();
    });
    server.begin();
    Serial.println(" done");
}

////////////////////////////////////////////////////////////////////////////////
void flashLed(int times, int onDelay, int offDelay)
{
   for(int i = 0; i < times; i++) {
      digitalWrite(LED_PIN, HIGH);
      delay(onDelay);
      digitalWrite(LED_PIN, LOW);
      delay(offDelay);
   }
}
