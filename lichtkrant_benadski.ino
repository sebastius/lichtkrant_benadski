#include <FS.h>                   //this needs to be first, or it all crashes and burns...

#include <EEPROM.h>
#include <SPI.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Time.h>
#include <TimeLib.h>
#include <SoftwareSerial.h>
#include <Timezone.h>     //https://github.com/LelandSindt/Timezone

TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, +120};    //Daylight time = UTC +2 hours
TimeChangeRule CET = {"CET", Last, Sun, Oct, 3, +60};     //Standard time = UTC +1 hours
Timezone myTZ(CEST, CET);
TimeChangeRule *tcr;        //pointer to the time change rule, use to get TZ abbrev
time_t utc, local;

#define SoftSerialTX 13
#define SoftSerialRX 4
#define Button 0

WiFiClient espClient;
PubSubClient client(espClient);

long lastMsg = 0;
char msg[50];
int value = 0;

bool spacestate;

//define your default values here, if there are different values in config.json, they are overwritten.
char mqtt_server[40];
char mqtt_port[6] = "1883";
char location[3];
char id[3];
char debugtopic[40] = "debug";
char pubtopic[40] = "lichtkrant";
String swversion = "Lichtkrant v0.1";

String chipid;
char chipidchar[6];
uint8_t MAC_array[6];
char MAC_char[18];

unsigned long starttijd;

//flag for saving data
bool shouldSaveConfig = false;

// The extra parameters to be configured (can be either global or just in the setup)
// After connecting, parameter.getValue() will get you the configured value
// id/name placeholder/prompt default length
WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 5);
WiFiManagerParameter custom_location("location", "controller location", location, 3);
WiFiManagerParameter custom_id("id", "controller id", id, 3);
WiFiManagerParameter custom_debug("debugtopic", "Debug Topic", debugtopic, 40);
WiFiManagerParameter custom_pub("publictopic", "Publication Topic", pubtopic, 40);


//Color definitions
// Might want to try other values like 0x10 etc
#define blackblack 0x00
#define blackred 0x01
#define blackgreen 0x02
#define blackorange 0x03
#define redblack 0x04
#define redred 0x05
#define redgreen 0x06
#define redorange 0x07
#define greenblack 0x08
#define greenred 0x09
#define greengreen 0x0A
#define greenorange 0x0B
#define orangeblack 0x0C
#define orangered 0x0D
#define orangegreen 0x0E
#define orangeorange 0x0F
#define blackrainbow 0x20


SoftwareSerial mySerial(SoftSerialRX, SoftSerialTX, false);

const char sync[] = {
  0xAA, 0xAA, 0xAA, 0xAA, 0xAA,
};

const char sendprogram[] = {
  0xBB,
};

const char endprogram[] = {
  0xBF, 0xB1,
};


void snelheid(byte snelheid) {
  verstuurbyte(0xA0);
  if (snelheid = 0) {
    snelheid = 1;
  }
  if (snelheid > 9) {
    snelheid = 9;
  }
  verstuurbyte(snelheid + 30);
}

void programmakeuze(char programma) {
  verstuurbyte(0xAF);
  verstuurbyte(programma);
}

void Gosub(char programma) {
  verstuurbyte(0xA2);
  verstuurbyte(programma);
}

void Wachten(byte tijd) {
  verstuurbyte(0xA1);
  if (tijd = 0) {
    tijd = 1;
  }
  if (tijd > 9) {
    tijd = 9;
  }
  verstuurbyte(tijd + 30);
}

void verstuurbyte(byte data) {
  mySerial.write(data);
}

void klokinstellen(time_t t) {
  verstuurbericht(sync);
  verstuurbyte(0xBE);
  verstuurbyte(0x33);
  verstuurbyte((year(t) / 10) % 10);
  verstuurbyte((year(t) % 10));
  verstuurbyte((month(t) / 10) % 10);
  verstuurbyte((month(t) % 10));
  verstuurbyte((day(t) / 10) % 10);
  verstuurbyte((day(t) % 10));
  verstuurbyte((hour(t) / 10) % 10);
  verstuurbyte((hour(t) % 10));
  verstuurbyte((minute(t) / 10) % 10);
  verstuurbyte((minute(t) % 10));
  verstuurbyte((second(t) / 10) % 10);
  verstuurbyte((second(t) % 10));
  verstuurbyte(0xBF);
  verstuurbyte(0xB1);
}

void verstuurbericht(const char * bericht) {
  for (int x = 0; x < strlen((char *)bericht); x++) {
    mySerial.write(bericht[x]);
  }
}

void customchar(byte nummer) {
  char bericht[] = {
    0x10, nummer,
  };
  verstuurbericht(bericht);
}

void verstuurtext(char * bericht, byte color) {
  for (int x = 0; x < strlen((char *)bericht); x++) {
    char berichtje;
    if (bericht[x] == ' ')  {
      berichtje = ':';
    } else {
      if (bericht[x] == ':')  {
        berichtje = ' ';
      } else {
        berichtje = bericht[x];
      }
    }

    mySerial.write(color);
    mySerial.write(berichtje);
  }
}

void bamtext(char * bericht, char programma, byte color) {
  verstuurbericht(sync);
  verstuurbyte(0xBB);
  programmakeuze('A');
  verstuurbyte(0x8B);
  verstuurbyte(0x03);
  verstuurtext(bericht, color);
  verstuurbericht(endprogram);
}

void hoofdprogramma() {
  verstuurbericht(sync);
  verstuurbyte(0xBB);
  programmakeuze('A');
  Gosub('B');
  Gosub('C');
  Gosub('D');
  verstuurbericht(endprogram);
}

void setup() {
  // put your setup code here, to run once:
  bamtext("Booting",  'A', blackred);
  chipid = ESP.getChipId();
  chipid.toCharArray(chipidchar, sizeof chipidchar);

  WiFi.macAddress(MAC_array);
  sprintf(MAC_char, "%02x:%02x:%02x:%02x:%02x:%02x",
          MAC_array[0], MAC_array[1], MAC_array[2], MAC_array[3], MAC_array[4], MAC_array[5]);


  Serial.begin(115200);
  mySerial.begin(2400);
  Serial.println();
  Serial.println("mounting FS...");
  bamtext("mounting FS...", 'A', blackred);

  if (SPIFFS.begin()) {
    Serial.println("mounted file system");
    bamtext("mounted file system",  'A', blackred);
    if (SPIFFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      bamtext("reading config file",  'A', blackred);
      File configFile = SPIFFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        bamtext("opened config file",  'A', blackred);
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);
        DynamicJsonBuffer jsonBuffer;
        JsonObject& json = jsonBuffer.parseObject(buf.get());
        json.printTo(Serial);
        if (json.success()) {
          Serial.println("\nparsed json");
          bamtext("parsed json",  'A', blackred);
          strcpy(mqtt_server, json["mqtt_server"]);
          Serial.println(mqtt_server);

          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(location, json["location"]);
          strcpy(id, json["id"]);
          strcpy(debugtopic, json["debugtopic"]);
          strcpy(pubtopic, json["pubtopic"]);
        } else {
          Serial.println("failed to load json config");
          bamtext("failed to load json config",  'A', blackred);
        }
      }
    }
  } else {
    Serial.println("failed to mount FS");
    bamtext("failed to mount FS",  'A', blackred);
  }

  pinMode(Button, INPUT);


  WiFiManager wifiManager;
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_location);
  wifiManager.addParameter(&custom_id);
  wifiManager.addParameter(&custom_debug);
  wifiManager.addParameter(&custom_pub);

  //wifiManager.resetSettings();

  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.autoConnect("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    bamtext("failed to connect and hit timeout",  'A', blackred);
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");
  bamtext("connected...yeey :)",  'A', blackred);
  
  updateParameters();



  //Serial.println("local ip");  Serial.println(WiFi.localIP();
  
  Serial.println();
  Serial.print("MQTT Server: ");
  Serial.println(mqtt_server);
  
  Serial.println();
  client.setServer(mqtt_server, 1883);
  client.setCallback(onMqttMessage);
}



void loop() {
  // put your main code here, to run repeatedly:
  if (!client.connected()) {
    Serial.println("Attempting MQTT Reconnect.");
    reconnect();
  }
  client.loop();

  if (digitalRead(Button) == LOW ) {
    ondemandPortal();
  }

  if (spacestate == true) {

  } else {

  }

}


//callback notifying us of the need to save config
void saveConfigCallback () {
  Serial.println("Should save config");
  shouldSaveConfig = true;
}


void updateParameters() {
  if (shouldSaveConfig) {
    //read updated parameters
    strcpy(mqtt_server, custom_mqtt_server.getValue());
    strcpy(mqtt_port, custom_mqtt_port.getValue());
    strcpy(location, custom_location.getValue());
    strcpy(id, custom_id.getValue());
    strcpy(debugtopic, custom_debug.getValue());
    strcpy(pubtopic, custom_pub.getValue());

    //save the custom parameters to FS

    Serial.println("saving config");
    DynamicJsonBuffer jsonBuffer;
    JsonObject& json = jsonBuffer.createObject();
    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["location"] = location;
    json["id"] = id;
    json["debugtopic"] = debugtopic;
    json["pubtopic"] = pubtopic;

    File configFile = SPIFFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    json.printTo(Serial);
    json.printTo(configFile);
    configFile.close();
  }
}

void ondemandPortal() {
  WiFiManager wifiManager;
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_location);
  wifiManager.addParameter(&custom_id);
  wifiManager.addParameter(&custom_debug);
  wifiManager.addParameter(&custom_pub);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  if (!wifiManager.startConfigPortal("AutoConnectAP", "password")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    ESP.reset();
    delay(5000);
  }
  Serial.println("connected...yeey :)");
  updateParameters();
}

boolean reconnect() {
  if (client.connect(chipidchar)) {
    Serial.println("Reconnected to MQTT");
    // Once connected, publish an announcement...
    mqtt_publish(String(debugtopic) + "/" + location + "/" + id + "/" + chipid, swversion + String(" ") + WiFi.localIP().toString() + String(" ") + String(MAC_char));

    client.subscribe("revspace/button/#");
    client.subscribe("revspace/state");
    client.loop();
  }
  return client.connected();
}

void onMqttMessage(char* topic, byte * payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
  char bericht[50] = "";
  if (strcmp(topic, "revspace/state") == 0) {
    for (uint8_t pos = 0; pos < length; pos++) {
      bericht[pos] = payload[pos];
    }

    if (strcmp(bericht, "open") == 0) {
      Serial.println("Revspace is open");
      if (spacestate == LOW) {
        spacestate = HIGH;
      }
    } else {
      // If it ain't open, it's closed! (saves a lot of time doing strcmp).
      Serial.println("Revspace is dicht");
      if (spacestate == HIGH) {
        spacestate = LOW;
      }
    }
  }

  if (strcmp(topic, "revspace/button/nomz") == 0) {
    for (uint8_t t = 0; t < 10; t++) {
      //digitalWrite(Buzzer, HIGH);

      delay(350);

      //digitalWrite(Buzzer, LOW);
      delay(350);
    }

  }

  if (strcmp(topic, "revspace/button/doorbell") == 0) {
    for (uint8_t t = 0; t < 10; t++) {

      //digitalWrite(Buzzer, HIGH);
      delay(20);

      //digitalWrite(Buzzer, LOW);
      delay(50);

    }
  }

  if (strcmp(topic, "revspace/button/rollcall") == 0) {
    Serial.println("Rollcall!");
    mqtt_publish(String(debugtopic) + "/" + location + "/" + chipid + "/" + location, swversion + String(" ") + WiFi.localIP().toString() + String(" ") + String(MAC_char));
  }
}

void mqtt_publish (String topic, String message) {
  Serial.println();
  Serial.print("Publishing ");
  Serial.print(message);
  Serial.print(" to ");
  Serial.println(topic);

  char t[100], m[100];
  topic.toCharArray(t, sizeof t);
  message.toCharArray(m, sizeof m);

  client.publish(t, m, /*retain*/ 1);
}
