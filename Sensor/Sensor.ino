/**The MIT License (MIT)

Copyright (c) 2016 by Greg Cunningham, CuriousTech

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Simple remote sensor for HVAC, with OLED display, AM2320 and PIR sensor or button
// This uses HTTP GET to send temp/rh

// Build with Arduino IDE 1.8.19, esp8266 SDK 3.1.2, 1MB (FS:64KB) (SPIFFS)

// Uncomment if using a direct OLED display
//#define USE_OLED

//uncomment to enable Arduino IDE Over The Air update code
#define OTA_ENABLE

#include <Wire.h>
#ifdef USE_OLED
#include <ssd1306_i2c.h> // https://github.com/CuriousTech/WiFi_Doorbell/tree/master/Libraries/ssd1306_i2c
#endif

#include <EEPROM.h>
#include <ESP8266mDNS.h>
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include <TimeLib.h> // http://www.pjrc.com/teensy/td_libs_Time.html
#include "eeMem.h"
#include <JsonParse.h> // https://github.com/CuriousTech/ESP-HVAC/tree/master/Libraries/JsonParse
#include <JsonClient.h> // https://github.com/CuriousTech/ESP-HVAC/tree/master/Libraries/JsonClient
#include <FS.h>
#ifdef OTA_ENABLE
#include <ArduinoOTA.h>
#endif
#include "pages.h"
#include "jsonstring.h"
#include "uriString.h"
#include "TempArray.h"

// Uncomment only one
#include "tuya.h"  // Uncomment device in tuya.cpp
//#include "BasicSensor.h"

int serverPort = 80;

enum reportReason
{
  Reason_Setup,
  Reason_Status,
  Reason_Alert,
  Reason_Motion,
};

IPAddress lastIP;
IPAddress verifiedIP;
int nWrongPass;

uint32_t sleepTimer = 60; // seconds delay after startup to enter sleep (Note: even if no AP found)
int8_t nWsConnected;

AsyncWebServer server( serverPort );
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
int WsClientID;

void jsonCallback(int16_t iName, int iValue, char *psValue);
JsonParse jsonParse(jsonCallback);
void jsonPushCallback(int16_t iName, int iValue, char *psValue);
JsonClient jsonPush(jsonPushCallback);

eeMem ee;

bool bPIRTrigger;
uint16_t displayTimer;
int32_t tzOffset;

#ifdef TUYA_H
TuyaInterface sensor;
#else
BasicInterface sensor;
#endif

#ifdef USE_OLED
SSD1306 display(0x3c, 5, 4); // Initialize the oled display for address 0x3c, sda=5, sdc=4
#endif

bool bConfigDone = false;
bool bStarted = false;
uint32_t connectTimer;
int16_t outTemp;
int16_t outRh;

TempArray temps;

String settingsJson()
{
  jsonString js("settings");

  js.Var("tz",  ee.tz);
  js.Var("name", ee.szName);
  js.Var("to", ee.time_off );
  js.Var("srate", ee.sendRate);
  js.Var("lrate", ee.logRate);
  js.Var("sleep", ee.sleep);
  js.Var("pri", ee.e.PriEn);
  js.Var("o", ee.e.bEnableOLED);
  js.Var("l1", sensor.m_bLED[0]);
  js.Var("l2", sensor.m_bLED[1]);
  js.Var("pir", ee.e.bPIR);
  js.Var("pirpin", ee.pirPin);
  js.Var("prisec", ee.priSecs);
  js.Var("ch", ee.e.bCall);
  js.Var("cf", sensor.m_bCF);
  js.Var("df", sensor.m_dataFlags);
  js.Var("si", temps.m_bSilence);
  js.Var("wt", ee.weight);
  return js.Close();
}

String dataJson()
{
  jsonString js("state");

  js.Var("t", (uint32_t)now() - tzOffset);
  js.Var("df", sensor.m_dataFlags);
  js.Var("temp", String( (float)sensor.m_values[DE_TEMP] / 10, 1 ) );
  js.Var("rh", String((float)sensor.m_values[DE_RH] / 10, 1) );
  js.Var("st", sleepTimer);
  int sig = WiFi.RSSI();
  sensor.setSignal(sig);
  js.Var("rssi", sig);

  if(sensor.m_dataFlags & DF_CO2 )
    js.Var("co2", sensor.m_values[DE_CO2] );
  if(sensor.m_dataFlags & DF_CH2O )
    js.Var("ch2o", sensor.m_values[ DE_CH2O ] );
  if(sensor.m_dataFlags & DF_VOC )
    js.Var("voc", sensor.m_values[ DE_VOC ] );
  
  return js.Close();
}

void displayStart()
{
  if(ee.e.bEnableOLED == false && displayTimer == 0)
  {
#ifdef USE_OLED
    display.init();
#endif
  }
  displayTimer = 30;
}

const char *jsonList1[] = {
  "key",
  "name",
  "reset",
  "tempOffset",
  "oled",
  "TZ",
  "TO",
  "srate",
  "lrate",
  "pir",
  "pri", // 10
  "prisec",
  "led1",
  "led2",
  "cf",
  "ch",
  "hostip",
  "hist",
  "ID",
  "sleep",
  "pirpin", // 20
  "alertidx",
  "alertlevel",
  "silence",
  "rhOffset",
  "wt",
  NULL
};

void parseParams(AsyncWebServerRequest *request)
{
  if(nWrongPass && request->client()->remoteIP() != lastIP)  // if different IP drop it down
    nWrongPass = 10;
  lastIP = request->client()->remoteIP();

  for ( uint8_t i = 0; i < request->params(); i++ )
  {
    const AsyncWebParameter* p = request->getParam(i);
    String s = request->urlDecode(p->value());

    uint8_t idx;
    for(idx = 0; jsonList1[idx]; idx++)
      if( p->name().equals(jsonList1[idx]) )
        break;
    if(jsonList1[idx])
    {
      int iValue = s.toInt();
      if(s == "true") iValue = 1;
      jsonCallback(idx, iValue, (char *)s.c_str());
    }
  }
}

bool bKeyGood;
bool bDataMode;

void jsonCallback(int16_t iName, int iValue, char *psValue)
{
  if(bKeyGood == false && iName != 0 && iName != 17 ) // allow hist
    return;  // only allow key set
  static int alertIdx;

  switch(iName)
  {
    case 0: // key
      if(!strcmp(psValue, ee.szControlPassword)) // first item must be key
      {
        bKeyGood = true;
        verifiedIP = lastIP;
      }
      break;
    case 1: // device name
      if(!strlen(psValue))
        break;
      strncpy(ee.szName, psValue, sizeof(ee.szName));
      ee.update();
      delay(1000);
      ESP.reset();
      break;
    case 2: // reset
      ee.update();
      delay(1000);
      ESP.reset();
      break;
    case 3: // tempOffset
      ee.tempCal = constrain(iValue, -80, 80);
      break;
    case 4: // OLED
      ee.e.bEnableOLED = iValue;
      break;
    case 5: // TZ
      ee.tz = iValue;
      break;
    case 6: // TO
      ee.time_off = iValue;
      break;
    case 7: // srate
      ee.sendRate = iValue;
      break;
    case 8: // lrate
      ee.logRate = iValue;
      break;
    case 9: // pir
      ee.e.bPIR = iValue;
      break;
    case 10: // pri
      ee.e.PriEn = iValue;
      break;
    case 11: // prisec
      ee.priSecs = iValue;
      break;
    case 12: // led1
      sensor.setLED(0, iValue ? true:false);
      break;
    case 13: // led2
      sensor.setLED(1, iValue ? true:false);
      break;
    case 14: // cf
      sensor.setCF(iValue ? true:false);
      break;
    case 15: // ch
      ee.e.bCall = iValue;
      if(iValue) CallHost(Reason_Setup, ""); // test
      break;
    case 16: // hostip
      ee.hostPort = 80;
      ee.hostIP[0] = lastIP[0];
      ee.hostIP[1] = lastIP[1];
      ee.hostIP[2] = lastIP[2];
      ee.hostIP[3] = lastIP[3];
      ee.e.bCall = 1;
      CallHost(Reason_Setup, ""); // test
      break;
    case 17: // hist
      temps.historyDump(true, ws, WsClientID);
      break;
    case 18:
      break;
    case 19:
      ee.sleep = iValue;
      break;
    case 20:
      ee.pirPin = iValue;
      break;
    case 21:
      alertIdx = constrain(iValue, 0, 15);
      break;
    case 22: // set alertidx first
      ee.wAlertLevel[alertIdx] = iValue;
      break;
    case 23:
      temps.m_bSilence = iValue ? true:false;
      break;
    case 24:
      ee.rhCal = iValue;
      break;
    case 25: // wt
      ee.weight = constrain(iValue, 1, 7);
      break;
  }
}

// Time in hh:mm[:ss][AM/PM]
String timeFmt(bool do_sec, bool do_M)
{
  String r = "";
  if(hourFormat12() < 10) r = " ";
  r += hourFormat12();
  r += ":";
  if(minute() < 10) r += "0";
  r += minute();
  if(do_sec)
  {
    r += ":";
    if(second() < 10) r += "0";
    r += second();
    r += " ";
  }
  if(do_M)
  {
      r += isPM() ? "PM":"AM";
  }
  return r;
}

const char *jsonListPush[] = {
  "tzoffset", // 0
  "time",
  "ppkw",
  "outtemp",
  "outrh",
  NULL
};

void jsonPushCallback(int16_t iName, int iValue, char *psValue)
{
  switch(iName)
  {
    case 0: // tzoffset
      tzOffset = iValue;
      break;
    case 1: // time
      setTime(iValue + tzOffset + (DST() ? 3600:0) );
      temps.m_bValidDate = true;
      break;
    case 3: // outtemp
      outTemp = iValue;
      break;
    case 4: // outrh
      outRh = iValue;
      break;
  }
}

struct cQ
{
  IPAddress ip;
  String sUri;
  uint16_t port;
};
#define CQ_CNT 8
cQ queue[CQ_CNT];
uint8_t qI;

void checkQueue()
{
  if(WiFi.status() != WL_CONNECTED)
    return;

  int idx;
  for(idx = 0; idx < CQ_CNT; idx++)
  {
    if(queue[idx].port)
      break;
  }
  if(idx == CQ_CNT || queue[idx].port == 0) // nothing to do
    return;

  if( jsonPush.begin(queue[idx].ip, queue[idx].sUri.c_str(), queue[idx].port, false, false, NULL, NULL, 1) )
  {
    jsonPush.setList(jsonListPush);
    queue[idx].port = 0;
  }
}

bool callQueue(IPAddress ip, String sUri, uint16_t port)
{
  int idx;
  for(idx = 0; idx < CQ_CNT; idx++)
  {
    if(queue[idx].port == 0)
      break;
  }
  if(idx == CQ_CNT) // nothing to do
  {
    jsonString js("print");
    js.Var("text", "Q full");
    WsSend(js.Close());
    return false;
  }

  queue[idx].ip = ip;
  queue[idx].sUri = sUri;
  queue[idx].port = port;

  return true;
}

void CallHost(reportReason r, String sStr)
{
  if(WiFi.status() != WL_CONNECTED || ee.hostIP[0] == 0 || ee.e.bCall == false)
    return;

  uriString uri("/wifi");
  uri.Param("name", ee.szName);

  switch(r)
  {
    case Reason_Setup:
      uri.Param("reason", "setup");
      uri.Param("port", serverPort);
      break;
    case Reason_Status:
      if(sensor.m_values[DE_TEMP] == 0)
        return;

      uri.Param("reason", "status");
      uri.Param("temp", String( (float)sensor.m_values[DE_TEMP]/10 , 1) );
      uri.Param("rh", String( (float)sensor.m_values[DE_RH]/10 , 1) );
      break;
    case Reason_Alert:
      uri.Param("reason", "alert");
      uri.Param("value", sStr);
      break;
    case Reason_Motion:
      uri.Param("reason", "motion");
      break;
  }

  IPAddress ip(ee.hostIP);
  callQueue(ip, uri.string().c_str(), ee.hostPort);
}

void sendTemp()
{
  if(WiFi.status() != WL_CONNECTED || ee.hvacIP[0] == 0) // not set
    return;

  uint8_t sentWt;
  uriString uri("/s");
  uri.Param("key", ee.szControlPassword);
  uri.Param("rmtname", ee.szName);
  String s = String(sensor.m_values[DE_TEMP]);
  s += (ee.e.bCF) ? "F" : "C";
  uri.Param("rmttemp", s);
  uri.Param("rmtrh", sensor.m_values[DE_RH]);
  if(sentWt != ee.weight)
  {
    sentWt = ee.weight;
    uri.Param("rmtwt", ee.weight);
  }

  if(ee.e.bPIR && bPIRTrigger )
  {
    if(ee.priSecs)
      uri.Param("rmtto", ee.priSecs);
    bPIRTrigger = false;
  }

  IPAddress ip(ee.hvacIP);
  callQueue(ip, uri.string(), 80);
}

uint16_t stateTimer = 10;

void sendState()
{
  if(nWsConnected)
    ws.textAll(dataJson());
  stateTimer = ee.sendRate;
}

void findHVAC() // This seems to show only 6 at a time
{
  int n = MDNS.queryService("iot", "tcp");
  int d;
  for(int i = 0; i < n; ++i)
  {
    char szName[38];
    MDNS.hostname(i).toCharArray(szName, sizeof(szName));
    strtok(szName, "."); // remove .local

    if(!strcmp(szName, "HVAC"))
    {
      ee.hvacIP[0] = MDNS.IP(i)[0]; // update IP
      ee.hvacIP[1] = MDNS.IP(i)[1];
      ee.hvacIP[2] = MDNS.IP(i)[2];
      ee.hvacIP[3] = MDNS.IP(i)[3];
      break;
    }
  }
}

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{  //Handle WebSocket event
  static bool bRestarted = true;
  String s;

  switch(type)
  {
    case WS_EVT_CONNECT:      //client connected
      if(bRestarted)
      {
        bRestarted = false;
//        client->text("alert;Restarted");
      }
      client->keepAlivePeriod(50);
      client->text( settingsJson() );
      client->text( dataJson() );
      client->ping();
      nWsConnected++;
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
      bDataMode = false; // turn off numeric display and frequent updates
      if(nWsConnected)
        nWsConnected--;
      break;
    case WS_EVT_ERROR:    //error was received from the other end
      break;
    case WS_EVT_PONG:    //pong message was received (in response to a ping request maybe)
      break;
    case WS_EVT_DATA:  //data packet
      AwsFrameInfo * info = (AwsFrameInfo*)arg;
      if(info->final && info->index == 0 && info->len == len){
        //the whole message is in a single frame and we got all of it's data
        if(info->opcode == WS_TEXT){
          data[len] = 0;

          uint32_t ip = client->remoteIP();
          WsClientID = client->id();

          bKeyGood = (ip && verifiedIP == ip) ? true:false; // if this IP sent a good key, no need for more
          jsonParse.process((char *)data);
          if(bKeyGood)
            verifiedIP = ip;
        }
      }
      break;
  }
}

void WsSend(String s)
{
  ws.textAll(s);
}

void alert(String txt)
{
  jsonString js("alert");
  js.Var("text", txt);
  ws.textAll(js.Close());
}

void setup()
{
  ee.init();
  sensor.init(ee.e.bCF);

  // initialize dispaly
#ifdef USE_OLED
  display.init();
//  display.flipScreenVertically();
  display.clear();
  display.display();
#endif

  WiFi.hostname(ee.szName);
  WiFi.mode(WIFI_STA);

  if ( ee.szSSID[0] )
  {
    WiFi.begin(ee.szSSID, ee.szSSIDPassword);
    WiFi.setHostname(ee.szName);
    bConfigDone = true;
  }
  else
  {
    Serial.println("No SSID. Waiting for EspTouch.");
    WiFi.beginSmartConfig();
  }
  connectTimer = now();

  SPIFFS.begin();

  // attach AsyncWebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on( "/", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    parseParams(request);
    bDataMode = true;
//  request->send(SPIFFS, "/index.html");
    request->send_P(200, "text/html", page_index);
  });
  server.on( "/s", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    parseParams(request);

    String page = "{\"ip\": \"";
    page += WiFi.localIP().toString();
    page += ":";
    page += serverPort;
    page += "\"}";
    request->send( 200, "text/json", page );
  });
  server.on( "/json", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    parseParams(request);
    request->send( 200, "text/json", settingsJson() );
  });
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    AsyncWebServerResponse *response = request->beginResponse_P(200, "image/x-icon", favicon, sizeof(favicon));
    response->addHeader("Content-Encoding", "gzip");
    request->send(response);
  });

  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });

  server.onFileUpload([](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
  });

  server.begin();

#ifdef OTA_ENABLE
  ArduinoOTA.setHostname(ee.szName);
  ArduinoOTA.begin();
  ArduinoOTA.onStart([]() {
    ee.update();
    sensor.setLED(0, false); // set it all to off
    temps.saveData();
    SPIFFS.end();
    alert("OTA Update Started");
    ws.closeAll();
  });
#endif

  jsonParse.setList(jsonList1);
  sensor.setLED(0, false);
  if(ee.sendRate == 0) ee.sendRate = 60;
  sleepTimer = ee.sleep;
  temps.init(sensor.m_dataFlags);
  if(ee.weight == 0)
    ee.weight = 1;
  if(ee.pirPin)
    pinMode(ee.pirPin, INPUT);
}

void loop()
{
  static uint8_t hour_save, sec_save;
  static uint8_t cnt = 0;
  bool bNew;

  MDNS.update();
#ifdef OTA_ENABLE
  ArduinoOTA.handle();
#endif
  if(ee.pirPin)
  {
    static bool last_pir;
    if(digitalRead(ee.pirPin) != last_pir)
    {
      last_pir = digitalRead(ee.pirPin);
      if(last_pir)
      {
        CallHost(Reason_Motion, "");
        if(ee.e.bPIR && bPIRTrigger)
          sendTemp();
        jsonString js("print");
        js.Var("text", "Motion");
        WsSend(js.Close());
      }
    }
  }

  static int htimer = 10;
  if(--htimer == 0)
  {
    temps.historyDump(false, ws, WsClientID);
    htimer = 30;
  }

  if(int err = sensor.service(ee.tempCal, ee.rhCal))
  {
    String s = "Sensor error ";
    s += err;
    alert(s);
  }

  checkQueue();

  if(sec_save != second()) // only do stuff once per second (loop is maybe 20-30 Hz)
  {
    sec_save = second();

    if(!bConfigDone)
    {
      if( WiFi.smartConfigDone())
      {
        Serial.println("SmartConfig set");
        bConfigDone = true;
        connectTimer = now();
      }
    }
    if(bConfigDone)
    {
      if(WiFi.status() == WL_CONNECTED)
      {
        if(!bStarted)
        {
          Serial.println("WiFi Connected");
          WiFi.mode(WIFI_STA);
          MDNS.begin( ee.szName );
          bStarted = true;
          MDNS.addService("iot", "tcp", serverPort);
          WiFi.SSID().toCharArray(ee.szSSID, sizeof(ee.szSSID)); // Get the SSID from SmartConfig or last used
          WiFi.psk().toCharArray(ee.szSSIDPassword, sizeof(ee.szSSIDPassword) );
          ee.update();

          findHVAC();
          CallHost(Reason_Setup, "");
        }
      }
      else if(now() - connectTimer > 10) // failed to connect for some reason
      {
        Serial.println("Connect failed. Starting SmartConfig");
        connectTimer = now();
//        ee.szSSID[0] = 0;
        WiFi.mode(WIFI_AP_STA);
        WiFi.beginSmartConfig();
        bConfigDone = false;
        bStarted = false;
      }
    }

    if(hour_save != hour())
    {
      hour_save = hour();
      if((hour_save&1) == 0)
        CallHost(Reason_Setup, "");
      ee.update(); // update EEPROM if needed while we're at it (give user time to make many adjustments)
    }

    if(nWrongPass)
      nWrongPass--;

    if(displayTimer) // temp display on thing
      displayTimer--;

    if(sleepTimer && nWsConnected == 0 && WiFi.status() == WL_CONNECTED) // don't sleep until all ws connections are closed
    {
      if(--sleepTimer == 0)
      {
        if(ee.time_off)
        {
          uint32_t us = ee.time_off * 60022000;  // minutes to us calibrated
#ifdef USE_OLED
          display.displayOff();
#endif
          ESP.deepSleep(us, WAKE_RF_DEFAULT);
        }
      }
    }

    if(--stateTimer == 0 || sensor.m_bUpdated) // a 60 second keepAlive
    {
      if(sensor.m_bUpdated)
        temps.update(sensor.m_values);
      sensor.m_bUpdated = false;
      sendState();
    }

    static uint8_t timer = 5;
    if(--timer == 0)
    {
      timer = 30;
      if(sensor.m_values[DE_RH])
        CallHost(Reason_Status, "");
    }

    static uint8_t sendTimer = 20;
    if(--sendTimer == 0)
    {
      sendTimer = ee.sendRate;
      sendTemp();
    }
    static uint8_t addTimer = 10;
    if(--addTimer == 0)
    {
      addTimer = ee.logRate;
      temps.add( (uint32_t)now() - tzOffset, ws, WsClientID);
    }
  }

  if(WiFi.status() != WL_CONNECTED)
  {
    delay(40);
    sensor.setLED(0, !sensor.m_bLED[0] );
    return;
  }/*
  else if(wifi.state() == ws_connecting)
  {
    sensor.setLED(0, true);
    delay(8);
    sensor.setLED(0, false);
    return;
  }*/

#ifdef USE_OLED
  static bool bClear;
  bool bDraw = (ee.e.bEnableOLED || displayTimer || bDataMode);
  if(bDraw == false && bClear)
    return;

  // draw the screen here
  display.clear();
  if(bDraw)
  {
    String s = timeFmt(true, true);
    s += "  ";
    s += dayShortStr(weekday());
    s += " ";
    s += String(day());
    s += " ";
    s += monthShortStr(month());
    s += "  ";

    Scroller(s);

    display.drawPropString(2, 47, String((float)temp/10, 1) + "]");
    display.drawPropString(64, 47, String((float)rh/10, 1) + "%");
    bClear = false;
  }
  else
    bClear = true;
  display.display();
#endif
}

bool DST() // 2016 starts 2AM Mar 13, ends Nov 6
{
  tmElements_t tm;
  breakTime(now(), tm);
  // save current time
  uint8_t m = tm.Month;
  int8_t d = tm.Day;
  int8_t dow = tm.Wday;

  tm.Month = 3; // set month = Mar
  tm.Day = 14; // day of month = 14
  breakTime(makeTime(tm), tm); // convert to get weekday

  uint8_t day_of_mar = (7 - tm.Wday) + 8; // DST = 2nd Sunday

  tm.Month = 11; // set month = Nov (0-11)
  tm.Day = 7; // day of month = 7 (1-30)
  breakTime(makeTime(tm), tm); // convert to get weekday

  uint8_t day_of_nov = (7 - tm.Wday) + 1;

  if ((m  >  3 && m < 11 ) ||
      (m ==  3 && d > day_of_mar) ||
      (m ==  3 && d == day_of_mar && hour() >= 2) ||  // DST starts 2nd Sunday of March;  2am
      (m == 11 && d <  day_of_nov) ||
      (m == 11 && d == day_of_nov && hour() < 2))   // DST ends 1st Sunday of November; 2am
    return true;
  return false;
}

// Text scroller optimized for very long lines
#ifdef USE_OLED
void Scroller(String s)
{
  static int16_t ind = 0;
  static char last = 0;
  static int16_t x = 0;

  if(last != s.charAt(0)) // reset if content changed
  {
    x = 0;
    ind = 0;
  }
  last = s.charAt(0);
  int len = s.length(); // get length before overlap added
  s += s.substring(0, 18); // add ~screen width overlap
  int w = display.propCharWidth(s.charAt(ind)); // first char for measure
  String sPart = s.substring(ind, ind + 18);
  display.drawPropString(x, 0, sPart );

  if( --x <= -(w))
  {
    x = 0;
    if(++ind >= len) // reset at last char
      ind = 0;
  }
}
#endif
