// Do all the web stuff here

#define OTA_ENABLE  //uncomment to enable Arduino IDE Over The Air update code

#include <ESPmDNS.h>

#ifdef OTA_ENABLE
#include <ArduinoOTA.h>
#endif
#include <TimeLib.h> // http://www.pjrc.com/teensy/td_libs_Time.html
#include "WebHandler.h"
#include "HVAC.h"
#include <JsonParse.h> // https://github.com/CuriousTech/ESP-HVAC/tree/master/Libraries/JsonParse
#include "jsonString.h"
#include "display.h"
#include "eeMem.h"
#include <FS.h>
#include <SPIFFS.h>
#include "jsonstring.h"
#include "forecast.h"
//-----------------
int serverPort = 80;

#ifdef REMOTE
#include <esp_websocket_client.h>
const char *hostName = RMTNAMEFULL;
esp_websocket_client_handle_t hWSClient;
esp_websocket_client_config_t config = {0};
bool bWscConnected;
void WscSend(String s);
void startListener(void);
#else
const char *hostName = HOSTNAME;
IPAddress ipFcServer(192,168,31,100);    // local forecast server and port
int nFcPort = 80;
#endif

Forecast FC;

//-----------------
AsyncWebServer server( serverPort );
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws
extern HVAC hvac;
extern Display display;

void remoteCallback(int16_t iName, int iValue, char *psValue);
JsonParse remoteParse(remoteCallback);

int nWrongPass;
bool bKeyGood;
int WsClientID;
int WsRemoteID;
IPAddress lastIP;
IPAddress WsClientIP;

bool bConfigDone = false; // EspTouch done or creds set
bool bStarted = false;
uint32_t connectTimer;

const char pageR_T[] = R"rawliteral(
<!DOCTYPE html>
<html>
<meta name="viewport" content="width=device-width, initial-scale=1"/>
<head>
<title>ESP-HVAC Remote</title>
</head>
<body">
<strong><em>CuriousTech HVAC Remote</em></strong><br>
)rawliteral";

const char pageR_B[] = R"rawliteral(
<br><small>&copy 2016 CuriousTech.net</small>
</body>
</html>
)rawliteral";

#ifdef REMOTE

const char *jsonListState[] = { "cmd", "r", "fr", "s", "it", "rh", "tt", "fm", "ot", "ol", "oh", "ct", "ft", "rt", "h", "lt", "lh", "rmt", NULL };

// values sent at an interval of 30 seconds unless they change sooner
const char *cmdList[] = { "cmd",
  "key",
  "data",
  "sum",
  NULL};
#else
const char *jsonListState[] = { "cmd",  "rmttemp", "rmtrh", NULL };
extern const char *cmdList[];
#endif

const char *jsonListSettings[] = { "cmd", "m", "am", "hm", "fm", "ot", "ht", "c0", "c1", "h0", "h1", "im", "cn", "cx", "ct", "tu", "ov", "rhm", "rh0", "rh1", NULL };

void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{  //Handle WebSocket event
  static bool rebooted = true;
  String s;

  switch(type)
  {
    case WS_EVT_CONNECT:      //client connected
      if(rebooted)
      {
        rebooted = false;
        client->text( "{\"cmd\":\"alert\",\"text\":\"Restarted\"}" );
      }
      client->text( hvac.settingsJson() );
      client->text( dataJson() );
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
    case WS_EVT_ERROR:    //error was received from the other end
#ifndef REMOTE
      if(hvac.m_bRemoteStream && client->id() == WsRemoteID) // stop remote
      {
        hvac.m_bRemoteStream = false;
        hvac.m_notif = Note_RemoteOff;
      }
#endif
      break;
    case WS_EVT_PONG:    //pong message was received (in response to a ping request maybe)
      break;
    case WS_EVT_DATA:  //data packet
      AwsFrameInfo * info = (AwsFrameInfo*)arg;
      if(info->final && info->index == 0 && info->len == len){
        //the whole message is in a single frame and we got all of it's data
        if(info->opcode == WS_TEXT){
          data[len] = 0;
          bKeyGood = false; // for callback (all commands need a key)
          WsClientID = client->id();
          WsClientIP = client->remoteIP();

          remoteParse.process((char*)data);
        }
      }
      break;
  }
}

void startServer()
{
  WiFi.hostname(hostName);
  WiFi.mode(WIFI_STA);

  if ( ee.szSSID[0] )
  {
    WiFi.begin(ee.szSSID, ee.szSSIDPassword);
    WiFi.setHostname(hostName);
    bConfigDone = true;
    hvac.m_notif = Note_Connecting;
  }
  else
  {
    hvac.m_notif = Note_EspTouch;
    WiFi.beginSmartConfig();
  }
  connectTimer = now();

  SPIFFS.begin();

  // attach AsyncWebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  server.on ( "/", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
#ifdef REMOTE // This can change any time. Just used for debug to view vars
    String s = pageR_T;
    s += "RemoteStream "; s += hvac.m_bRemoteStream; s += "<br>";
    s += "WsConnected "; s += bWscConnected; s += "<br>";
    IPAddress ip(ee.hostIp);
    s += "HVAC IP "; s += ip.toString(); s += "<br>";
    s += "Now: "; s += now(); s += "<br>";
    s += "FcstIdle "; s += FC.m_bUpdateFcstIdle; s += "<br>";
    s += "UpdateFcst "; s += FC.m_bUpdateFcst; s += "<br>";
    s += "FcDate: "; s += FC.m_fc.loadDate; s += "<br>";
    s += pageR_B;
    request->send( 200, "text/html", s );
#endif
  });

  // This can change. Just used to view vars
  server.on ( "/info", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    String s = pageR_T;
    s += "RemoteStream "; s += hvac.m_bRemoteStream; s += "<br>";
    IPAddress ip(ee.hostIp);
    s += "Now: "; s += now(); s += "<br>";
    s += "FcDate: "; s += FC.m_fc.loadDate; s += "<br>";
    s += pageR_B;
    request->send( 200, "text/html", s );
  });

  // For quick commands. Remotes have a seperate command list
  server.on ( "/s", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    parseParams(request);
    String s = "OK\r\n\r\n";
    jsonString js;
    js.Var("outtemp", hvac.m_outTemp);
    js.Var("outrh", hvac.m_outRh);
    s += js.Close();
    s += "\r\n";
    request->send ( 200, "text/html", s );
  });

  server.on ( "/json", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    request->send ( 200, "text/json",  hvac.settingsJson());
  });

#ifndef REMOTE
  // Main page
  server.on ( "/iot", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){ // Hidden instead of / due to being externally accessible. Use your own here.
    request->send(SPIFFS, "/index.html");
  });
  server.on ( "/settings", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/settings.html");
  });
  server.on ( "/chart.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/chart.html");
  });

  server.on("/styles.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/styles.css");
  });

  server.on( "/wifi", HTTP_GET|HTTP_POST, [](AsyncWebServerRequest *request) // used by other iot devices
  {
    parseParams(request);
    jsonString js;
    js.Var("time", (long)(now() - ((ee.tz + hvac.m_DST) * 3600)) );
    js.Var("ppkw", ee.ppkwh );
    js.Var("temp", hvac.m_outTemp ); // for other WiFi weather devices
    js.Var("rh", hvac.m_rh );
    request->send(200, "text/plain", js.Close());
  });
#endif // !REMOTE
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/favicon.ico");
  });

  /*  silent on non-existing pages
  server.onNotFound([](AsyncWebServerRequest *request){
    request->send(404);
  });
  server.onFileUpload([](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
  });
  server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total){
  });
*/
  // respond to GET requests on URL /heap
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.begin();

#ifdef REMOTE
//  ee.hostIp[0] = 192; // force IP of HVAC if needed
//  ee.hostIp[1] = 168;
//  ee.hostIp[2] = 31;
//  ee.hostIp[3] = 46;
#endif
  remoteParse.setList(cmdList);

#ifdef OTA_ENABLE
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.begin();
  ArduinoOTA.onStart([]() {
    hvac.disable();
    hvac.dayTotals(day() - 1); // save for reload
    ee.update();
    hvac.saveStats();
    jsonString js("alert");
    js.Var("text", "OTA Update Started");
    ws.textAll(js.Close());
    ws.closeAll();
    SPIFFS.end();
  });
#endif
}

#ifdef REMOTE
void findHVAC() // find the HVAC on iot domain
{
  // Find HVAC
  int cnt = MDNS.queryService("iot", "tcp");
  for(int i = 0; i < cnt; ++i)
  {
    char szName[38];
    MDNS.hostname(i).toCharArray(szName, sizeof(szName));
    strtok(szName, "."); // remove .local

    if(!strcmp(szName, HOSTNAME))
    {
      ee.hostIp[0] = MDNS.IP(i)[0]; // update IP
      ee.hostIp[1] = MDNS.IP(i)[1];
      ee.hostIp[2] = MDNS.IP(i)[2];
      ee.hostIp[3] = MDNS.IP(i)[3];
      hvac.m_notif = Note_HVACFound;
      break;
    }
  }
  if(hvac.m_notif != Note_HVACFound)
    hvac.m_notif = Note_HVACNotFound;
}
#endif

void handleServer()
{
  static uint8_t n;
  if(++n >= 10)
  {
    historyDump(false);
    n = 0;
  }

#ifdef OTA_ENABLE
// Handle OTA server.
  ArduinoOTA.handle();
#endif
}

void WsSend(String s) // mostly for debug
{
  ws.textAll(s);
}

bool secondsServer() // called once per second
{
  bool bConn = false;

  if(!bConfigDone)
  {
    if( WiFi.smartConfigDone())
    {
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
//        WiFi.mode(WIFI_STA); // Stop broadcasting SSID
        MDNS.begin( hostName );
        bStarted = true;
        MDNS.addService("iot", "tcp", serverPort);
        WiFi.SSID().toCharArray(ee.szSSID, sizeof(ee.szSSID)); // Get the SSID from SmartConfig or last used
        WiFi.psk().toCharArray(ee.szSSIDPassword, sizeof(ee.szSSIDPassword) );
        hvac.m_notif = Note_Connected;
        bConn = true;
#ifdef REMOTE
        findHVAC();
#endif
      }
    }
    else if(now() - connectTimer > 10) // failed to connect for some reason
    {
      connectTimer = now();
//      ee.szSSID[0] = 0;
      WiFi.mode(WIFI_AP_STA);
      WiFi.beginSmartConfig();
      bConfigDone = false;
      bStarted = false;
    }
  }

  if(WiFi.status() != WL_CONNECTED)
    return bConn;

  ws.cleanupClients();

  static uint8_t nUpdateDelay = 5; // startup delay of 5s, then used for reattempts (60s)
  if(nUpdateDelay)
    nUpdateDelay--;

  if(now() >= FC.m_fc.loadDate + (3600*3) && (nUpdateDelay == 0) ) // > 3 hours old
  {
    FC.m_bUpdateFcst = true;
  }

#ifdef REMOTE
  if(hvac.tempChange())
  {
    ws.textAll(dataJson()); // not used?
  }

  static uint8_t start = 4; // give it time to settle before initial connect
  if(start && WiFi.status() == WL_CONNECTED)
    if(--start == 0)
        startListener();

  if(FC.m_bUpdateFcst && bWscConnected && (nUpdateDelay == 0))
  {
    FC.m_bUpdateFcst = false;
    FC.m_bUpdateFcstIdle = false;
    nUpdateDelay = 60;
    WscSend("{\"bin\":1}"); // request forcast data
  }
#else  // !Remote
  String s = hvac.settingsJsonMod(); // returns "{}" if nothing has changed
  if(s.length() > 2)
    ws.textAll(s); // update anything changed

  if(hvac.stateChange() || hvac.tempChange())
    ws.textAll( dataJson() );

  if(nWrongPass)
    nWrongPass--;

  if(FC.m_bUpdateFcst && FC.m_bUpdateFcstIdle && nUpdateDelay == 0 && year() > 2020)
  {
    FC.m_bUpdateFcst = false;
    FC.m_bUpdateFcstIdle = false;
    nUpdateDelay = 60; // delay retries by 1 minute
    switch(ee.b.nFcstSource)
    {
      case 1:
        FC.start(ipFcServer, nFcPort, ee.b.bCelcius, 0);    // get preformatted data from local server
      case 0:
        FC.start(ipFcServer, nFcPort, ee.b.bCelcius, 1);    // get OpenWeatherMap file from local server
        break;
      case 2:
        FC.start(ee.cityID, ee.b.bCelcius);    // get data from OpenWeatherMap 5 day
        break;
    }
  }

  int stat = FC.checkStatus();
  if(stat == FCS_Fail)
  {
    jsonString js("alert");
    js.Var("text", "Forecast failed");
    hvac.m_notif = Note_Forecast;
    WsSend(js.Close());
  }

#endif // !REMOTE
  return bConn;
}

void parseParams(AsyncWebServerRequest *request)
{
  if(request->params() == 0)
    return;

#ifdef REMOTE

  for ( uint8_t i = 0; i < request->params(); i++ )
  {
    AsyncWebParameter* p = request->getParam(i);
    String s = request->urlDecode(p->value());
    int val = s.toInt();
 
    switch( p->name().charAt(0) )
    {
      case 'f': // get forecast
          FC.m_bUpdateFcst = true;
          break;
      case 'H': // host  (from browser type: hTtp://thisip/s?H=hostip)
          {
            IPAddress ip;
            ip.fromString(s);
            ee.hostIp[0] = ip[0];
            ee.hostIp[1] = ip[1];
            ee.hostIp[2] = ip[2];
            ee.hostIp[3] = ip[3];
            startListener(); // reset the URI
            ee.update();
          }
          break;
      case 'R': // remote
          if(ee.b.bLock) break;
          if(val)
          {
            if(hvac.m_bRemoteStream == false)
              hvac.enableRemote(); // enable
          }
          else
          {
            if(hvac.m_bRemoteStream == true)
              hvac.enableRemote(); // disable
          }
          break;
      case 'Z': // Timezone
          ee.tz = val;
          break;
    }
  }
#else
  bool bPassGood;

  for( uint8_t i = 0; i < request->params(); i++ ) // password may be at end
  {
    AsyncWebParameter* p = request->getParam(i);
    String s = request->urlDecode(p->value());

    if(p->name() == "key")
      bPassGood = s.equals(String(ee.password));
  }

  IPAddress ip = request->client()->remoteIP();

  if( (ip[0] != 192 && ip[1] != 168 && !bPassGood) || nWrongPass )
  {
    if(nWrongPass == 0)
    {
      nWrongPass = 10;
      jsonString js("hack");
      js.Var("ip", ip.toString() );
      ws.textAll(js.Close());
    }
    else if((nWrongPass & 0xFFFFF000) == 0 ) // time doubles for every high speed wrong password attempt.  Max 1 hour
      nWrongPass <<= 1;
    if(ip != lastIP)  // if different IP drop it down
       nWrongPass = 10;

    lastIP = ip;
    return;
  }

  lastIP = ip;

  for ( uint8_t i = 0; i < request->params(); i++ )
  {
    AsyncWebParameter* p = request->getParam(i);
    String s = request->urlDecode(p->value());

    if(p->name() == "key");
    else if(p->name() == "restart")
    {
      ESP.restart();
    }
    else if(p->name() == "led")
    {
      display.m_maxBrightness = constrain(s.toInt(), 10, 255);
    }
    else
    {
      if(p->name() == "fc")
        FC.m_bUpdateFcst = true;
      hvac.setVar(p->name(), s.toInt(), (char *)s.c_str(), ip );
    }
  }
#endif // !REMOTE
}

// Pushed data
String dataJson()
{
  return hvac.getPushData();
}

void historyDump(bool bStart)
{
#ifndef REMOTE
  static bool bSending = false;
  static int entryIdx = 0;
  static int tempMin = 0;
  static int lMin;
  static int hMin;
  static int rhMin;
  static int otMin;

  if(bStart)
    bSending = true;
  if(bSending == false)
    return;

  gPoint gpt;

  if(bStart)
  {
    entryIdx = 0;
    if( display.getGrapthPoints(&gpt, 0) == false)
    {
      bSending = false;
      return;
    }

    jsonString js("ref");
    int maxv;
    tempMin = display.minPointVal(0, maxv);
    lMin = display.minPointVal(1, maxv);
    hMin = display.minPointVal(2, maxv);
    rhMin = display.minPointVal(3, maxv);
    otMin = display.minPointVal(4, maxv);
  
    js.Var("tb", display.m_lastPDate);
    js.Var("th", ee.cycleThresh[ (hvac.m_modeShadow == Mode_Heat) ? 1:0] ); // threshold
    js.Var("tm", tempMin); // temp min
    js.Var("lm", lMin); // threshold low min
    js.Var("rm", rhMin); // rh min
    js.Var("om", otMin); // ot min
    ws.text(WsClientID, js.Close());
  }

  String out;
#define CHUNK_SIZE 800
  out.reserve(CHUNK_SIZE + 100);

  out = "{\"cmd\":\"data\",\"d\":[";

  bool bC = false;

  for(; entryIdx < GPTS - 1 && out.length() < CHUNK_SIZE && display.getGrapthPoints(&gpt, entryIdx); entryIdx++)
  {
    int len = out.length();
    if(bC) out += ",";
    bC = true;
    out += "[";         // [seconds, temp, rh, lowThresh, state, outTemp],
    out += gpt.bits.tmdiff;
    out += ",";
    out += gpt.t.inTemp - tempMin;
    out += ",";
    out += gpt.bits.rh - rhMin;
    out += ",";
    out += gpt.t.target - lMin;
    out += ",";
    out += gpt.bits.u & 7; // state + fan
    out += ",";
    out += gpt.t.outTemp - otMin;
    if(hvac.m_Sensor[0].IP)
    {
      out += ",";
      out += gpt.sens0 + gpt.t.inTemp - tempMin;
      if(hvac.m_Sensor[1].IP)
      {
        out += ",";
        out += gpt.sens1 + gpt.t.inTemp - tempMin;
        if(hvac.m_Sensor[2].IP)
        {
          out += ",";
          out += gpt.sens2 + gpt.t.inTemp - tempMin;
          if(hvac.m_Sensor[3].IP)
          {
            out += ",";
            out += gpt.sens3 + gpt.t.inTemp - tempMin;
          }
          if(hvac.m_Sensor[4].IP)
          {
            out += ",";
            out += gpt.bits.sens4 + gpt.t.inTemp - tempMin;
          }
        }
      }
    }
    out += "]";
    if( out.length() == len) // memory full
      break;
  }
  if(bC) // don't send blank
  {
    out += "]}";
    ws.text(WsClientID, out);
  }
  else
    bSending = false;
  if(bSending == false)
    ws.text(WsClientID, "{\"cmd\":\"draw\"}"); // tell page to draw after all is sent
#endif
}

void appendDump(uint32_t startTime)
{
#ifndef REMOTE
  String out = "{\"cmd\":\"data2\",\"d\":[";

  uint32_t tb = display.m_lastPDate;
  bool bC = false;
  gPoint gpt;

  for(int entryIdx = 0; entryIdx < GPTS - 1 && out.length() < CHUNK_SIZE && display.getGrapthPoints(&gpt, entryIdx) && tb > startTime; entryIdx++)
  {
    if(bC) out += ",";
    bC = true;
    out += "[";         // [seconds, temp, rh, lowThresh, state, outTemp],
    out += tb;
    tb -= gpt.bits.tmdiff;
    out += ",";
    out += gpt.t.inTemp;
    out += ",";
    out += gpt.bits.rh;
    out += ",";
    out += gpt.t.target;
    out += ",";
    out += gpt.bits.u & 7;
    out += ",";
    out += gpt.t.outTemp;
    if(hvac.m_Sensor[0].IP)
    {
      out += ",";
      out += gpt.sens0 + gpt.t.inTemp;
      if(hvac.m_Sensor[1].IP)
      {
        out += ",";
        out += gpt.sens1 + gpt.t.inTemp;
        if(hvac.m_Sensor[2].IP)
        {
          out += ",";
          out += gpt.sens2 + gpt.t.inTemp;
          if(hvac.m_Sensor[3].IP)
          {
            out += ",";
            out += gpt.sens3 + gpt.t.inTemp;
            if(hvac.m_Sensor[4].IP)
            {
              out += ",";
              out += gpt.bits.sens4 + gpt.t.inTemp;
            }
          }
        }
      }
    }
    out += "]";
  }
  if(bC) // don't send blank
  {
    out += "]}";
    ws.text(WsClientID, out);
  }
#endif
}

void remoteCallback(int16_t iName, int iValue, char *psValue)
{
  static int8_t cmd = 0;

#ifdef REMOTE
  switch(iName)
  {
    case 0: // cmd
      if(!strcmp(psValue, "settings"))
      {
        remoteParse.setList(jsonListSettings);
        cmd = 1;
      }
      else if(!strcmp(psValue, "state"))
      {
        remoteParse.setList(jsonListState);
        cmd = 2;
      }
      else
      {
        remoteParse.setList(cmdList);
        cmd = 0;            
      }
      break;
  }
  if(iName) switch(cmd)
  {
    case 0: // cmd
      break;
    case 1: //settings
      hvac.setSettings(iName - 1, iValue);
      break;
    case 2: // state
      hvac.updateVar(iName - 1, iValue);
      break;
  }

#else
  switch(cmd) // Should always be 0
  {
    case 0: // key
      if(iName == 1) // 0 = key
      {
        if(!strcmp(psValue, ee.password)) // first item must be key
        {
          bKeyGood = true;
        }
      }
      else if(iName == 2) // 2 = data
      {
        if(iValue) appendDump(iValue);
        else historyDump(true);
      }
      else if(iName == 3) // 3 = summary
      {
        String out = String("{\"cmd\":\"sum\",\"mon\":[");

        for(int i = 0; i < 12; i++)
        {
          if(i) out += ",";
          out += "[";
          out += hvac.m_SecsMon[i][0];
          out += ",";
          out += hvac.m_SecsMon[i][1];
          out += ",";
          out += hvac.m_SecsMon[i][2];
          out += "]";
        }
        out += "],\"day\":[";
        for(int i = 0; i < 31; i++)
        {
          if(i) out += ",";
          out += "[";
          out += hvac.m_SecsDay[i][0];
          out += ",";
          out += hvac.m_SecsDay[i][1];
          out += ",";
          out += hvac.m_SecsDay[i][2];
          out += "]";
        }
        out += "],";
        out += "\"fcDate\":";
        out += FC.m_fc.Date;
        out += ",\"fcFreq\":";
        out += FC.m_fc.Freq;
        out += ",\"fc\":[";
        for(int i = 0; FC.m_fc.Data[i].temp != -1000 && i < FC_CNT; i++)
        {
          if(i) out += ",";
          out += FC.m_fc.Data[i].temp;
        }
        out += "]}";
        ws.text(WsClientID, out);
      }
      else if(iName == 4) // 4 = bin
      {
        WsRemoteID = WsClientID; // Only remote uses binary
        switch(iValue)
        {
          case 0: // backward compatible forecast data (can't be done easily)
            break;
          case 1: // new forecast data
            ws.binary(WsClientID, (uint8_t*)&FC.m_fc, sizeof(FC.m_fc));
            break;
        }
      }
      else if(bKeyGood && iName >= 5)
      {
        hvac.setVar(cmdList[iName], iValue, psValue, WsClientIP); // 5 is "fanmode"
      }
      break;
    case 2: // state
      switch(iName)
      {
        case 0: // temp
          hvac.setVar("rmttemp", iValue, psValue, WsClientIP);
          break;
        case 1: // rh
          hvac.setVar("rmtrh", iValue, psValue, WsClientIP);
          break;
      }
      break;
  }

#endif
}

#ifdef REMOTE
void WscSend(String s) // remote WebSocket
{
  if(esp_websocket_client_is_connected(hWSClient))
   esp_websocket_client_send_text(hWSClient, s.c_str(), s.length(), 1000);
}

static void clientEventHandler(void* handle, const char* na, int ID, void* ptr)
{
  esp_websocket_event_data_t *pEv = (esp_websocket_event_data_t *)ptr;

  switch(ID)
  {
    case WEBSOCKET_EVENT_ERROR:
    // not used yet?
      break;
    case WEBSOCKET_EVENT_CONNECTED:
      switch(pEv->op_code)
      {
        case 10:// ?
          break;
      }
      bWscConnected = true;
      FC.m_bUpdateFcst = true; // Get the forecast faster
      FC.m_bUpdateFcstIdle = true;
      if(hvac.m_notif == Note_Network || hvac.m_notif == Note_HVACNotFound) // remove net disconnect error
        hvac.m_notif = Note_None;
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      switch(pEv->op_code)
      {
        case 10:// ?
          break;
      }
      bWscConnected = false;
      hvac.m_notif = Note_Network;
      break;
    case WEBSOCKET_EVENT_DATA: // data
      switch(pEv->op_code)
      {
        case 1: // text
          remoteParse.process((char*)pEv->data_ptr);
          break;
        case 2: // binary
          if(pEv->data_len == sizeof(FC.m_fc) )
          {
            memcpy((void*)&FC.m_fc, pEv->data_ptr, sizeof(FC.m_fc));
            FC.m_fc.loadDate = now();
            FC.m_bUpdateFcstIdle = true;
            FC.m_bFcstUpdated = true;
          }
          break;
        case 10: // ping?
          break;
      }
      break;
  }
}

void startListener()
{
  IPAddress ip(ee.hostIp);
  static char szHost[32];
  
  strcpy(szHost, ip.toString().c_str());
  config.host = szHost;
  config.uri = "/ws";
  config.port = ee.hostPort;
  hWSClient = esp_websocket_client_init((const esp_websocket_client_config_t*)&config);
  if(!hWSClient) return;
  esp_websocket_client_start(hWSClient);
  esp_websocket_register_events(hWSClient, WEBSOCKET_EVENT_ANY, clientEventHandler, NULL);
}
#endif
