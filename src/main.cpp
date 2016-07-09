#include <ESP8266mDNS.h>
#include <Arduino.h>
#include <SoftwareSerial.h>
#include <ESP8266HTTPClient.h>
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <ESP8266mDNS.h>
#include <WiFiManager.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#define BUFSIZE 1024

const byte P1_RTS = 5; // in the final product, we want to use GPIO4
const byte P1_RX  = 4; // in the final product, we want to use GPIO5

ESP8266WebServer server ( 80 );
SoftwareSerial swSer(P1_RX, -1, true, 256);

byte buffer[BUFSIZE]; //Buffer for serial data to find \n .
int bufpos = 0;
byte telegram[BUFSIZE];
int telegrampos = 0;
int interval = 10000;
unsigned long previousMillis = 0;
boolean gotstart = false;
boolean newTelegram = false;

boolean decodeTelegram();
void postTelegram();

void handleRoot() {
  char temp[400];
  int sec = millis() / 1000;
  int min = sec / 60;
  int hr = min / 60;

  snprintf( temp, 400,
             "<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>ESP8266 Demo</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Hello from ESP8266!</h1>\
    <p>Uptime: %02d:%02d:%02d</p>\
  </body>\
</html>",
             hr, min % 60, sec % 60
           );
  server.send ( 200, "text/html", temp );
}

void handleP1() {
  if ( telegrampos == 0 ) {
    server.send(404, "text/plain", "No telegram");
  } else {
    server.send(200, "text/plain", String((char *)telegram));
  }
}

void handleStatus() {
  char temp[1024];

  snprintf ( temp, 400,
             "<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <title>ESP8266 Demo</title>\
    <style>\
      body { background-color: #cccccc; font-family: Arial, Helvetica, Sans-Serif; Color: #000088; }\
    </style>\
  </head>\
  <body>\
    <h1>Hello from ESP8266!</h1>\
    <p>bufpos: %d</p>\
    <p>telegrampos: %d</p>\
    <p>gotstart: %s</p>\
    <p>newTelegram: %s</p>\
    </body>\
</html>",
             bufpos, telegrampos, gotstart ? "true" : "false", newTelegram ? "true" : "false");
  server.send ( 200, "text/html", temp );
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += ( server.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";

  for ( uint8_t i = 0; i < server.args(); i++ ) {
    message += " " + server.argName ( i ) + ": " + server.arg ( i ) + "\n";
  }

  server.send ( 404, "text/plain", message );
}

void debug(const char* msg) {
  Serial.print(millis());
  Serial.print(" ");
  Serial.println(msg);
}

void setup() {
  pinMode(P1_RX, INPUT);
  swSer.begin(115200);
  pinMode(P1_RTS, OUTPUT);
  //digitalWrite(P1_RX,LOW);
  Serial.begin(115200);
  //Serial.setDebugOutput(true);
  debug("\nP1 Recorder booted or woke up");

  WiFiManager wifiManager;
  wifiManager.setRemoveDuplicateAPs(false);
  //  wifiManager.resetSettings();
  while (!wifiManager.autoConnect()) {
    delay(0);
  }

  Serial.println("WiFi connected");

  if ( MDNS.begin ( "p1meter" ) ) {
    MDNS.addService("http", "tcp", 80);
    Serial.println ( "MDNS responder started" );
  }
  server.on ( "/", handleRoot );
  server.on("/p1", handleP1);
  server.on("/status", handleStatus);
  server.onNotFound ( handleNotFound );
  server.begin();
  Serial.println ( "HTTP server started" );

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
}


void loop() {
  decodeTelegram();
  // Get snapshot of time
  unsigned long currentMillis = millis();
  // How much time has passed, accounting for rollover with subtraction!
  if (previousMillis == 0 || ((unsigned long)(currentMillis - previousMillis) >= interval)) {
    digitalWrite(P1_RTS, HIGH);
    if ( newTelegram ) {
      postTelegram();
      previousMillis = currentMillis;
    }
  }
  server.handleClient();
  ArduinoOTA.handle();
  //goToSleep();
}

void postTelegram() {
    HTTPClient http;
    http.begin("http://192.168.0.184:8000/post"); //HTTP
    http.addHeader("Content-Type", "text/plain");
    // start connection and send HTTP header
    int httpCode = http.POST(telegram, telegrampos);
    http.writeToStream(&Serial);
    // httpCode will be negative on error
    if (httpCode > 0) {
      // HTTP header has been send and Server response header has been handled
      if ( httpCode != 200 ) {
        Serial.printf("%d [HTTP] POST... code: %d\n", millis(), httpCode);
      }
      newTelegram = false;
    } else {
      Serial.printf("%d ", millis());
      Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
    http.end();
}

void postFailure() {
  HTTPClient http;
  // configure traged server and url
  http.begin("http://192.168.0.98:8080/post"); //HTTP
  http.addHeader("Content-Type", "text/plain");
  debug("[HTTP] POST...\n");
  // start connection and send HTTP header
  int httpCode = http.POST("Failed to read telegram");
  http.writeToStream(&Serial);
  // httpCode will be negative on error
  if (httpCode > 0) {
    // HTTP header has been send and Server response header has been handled
    if ( httpCode != 200 ) {
      Serial.printf("%d [HTTP] POST... code: %d\n", millis(), httpCode);
    }
  } else {
    Serial.printf("%d ", millis());
    Serial.printf("[HTTP] POST... failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  http.end();
}

void resetBuffer() {
    for (int i = 0; i < BUFSIZE; i++)
    {
      buffer[i] = 0;
    }
    bufpos = 0;
}

boolean decodeTelegram() {
  int tl = 0;
  int tld = 0;
  boolean gotend = false;
  while (swSer.available() > 0) {
    char input = swSer.read();
    char inChar = (char)input;
    buffer[bufpos] = input & 127;
    bufpos++;
    if (input == '/') {
      telegrampos = 0;
      gotstart = true;
      gotend = false;
      resetBuffer();
      buffer[bufpos++] = input & 127;
    } else if (input == '\n') { // We received a new line (data up to \n)
      // ignore lines until we have see a starting line
      if ( gotstart ) {
        // add line to telegram
        for (int i = 0; i < bufpos; i++) {
          // todo fix buffer overflow...
          telegram[telegrampos++] = buffer[i];
        }
        if ( buffer[0] == '!' ) {
          // add terminating null
          telegram[telegrampos++] = 0;
          gotend = true;
          // stop receiving
          digitalWrite(P1_RTS, LOW);
          while(swSer.available()>0) {
            swSer.read();
          }
        }
      }
      // Empty buffer again (whole array)
      resetBuffer();
      if ( gotend ) {
        gotstart = false;
        newTelegram = true;
        return true;
      }
    }
  }
  return false;
}
