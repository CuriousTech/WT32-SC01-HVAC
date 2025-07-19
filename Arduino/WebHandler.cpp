// Do all the web stuff here

#define OTA_ENABLE  //uncomment to enable Arduino IDE Over The Air update code

#include <ESPmDNS.h>

#ifdef OTA_ENABLE
#include <ArduinoOTA.h>
#endif
#include <Time.h>
#include "WebHandler.h"
#include "HVAC.h"
#include "jsonString.h"
#include "display.h"
#include "eeMem.h"
#include "Media.h"
#include "forecast.h"
#include "pages.h"

//-----------------
int serverPort = 80;

#ifdef REMOTE
#include "WebSocketClient.h"
const char *hostName = RMTNAMEFULL;
WebSocketClient WsClient;
bool bWscConnected;
void WscSend(String s);
void startListener(void);
#else
const char *hostName = HOSTNAME;
IPAddress ipFcServer(192,168,31,100);    // local forecast server and port
int nFcPort = 80;
#endif

AsyncWebServer server( serverPort );
AsyncWebSocket ws("/ws"); // access at ws://[esp ip]/ws

Forecast FC;

//-----------------
extern HVAC hvac;
extern Display display;

void remoteCallback(int8_t iEvent, uint8_t iName, int32_t iValue, char *psValue);

int nWrongPass;
bool bKeyGood;
int WsClientID;
int WsRemoteID;
IPAddress lastIP;
IPAddress WsClientIP;

bool bConfigDone = false; // EspTouch done or creds set
bool bStarted = false;
uint32_t connectTimer;

#ifdef REMOTE
const char *jsonListSettings[] = { "cmd",
  "m", "am", "hm", "fm", "ot", "ht", "c0", "c1", "h0", "h1", "im", "cn", "cx", "ct", "tu", "ov", "rhm", "rh0", "rh1", NULL
};
const char *jsonListState[] = { "cmd",
  "r", "fr", "s", "it", "rh", "tt", "fm", "ot", "ol", "oh", "ct", "ft", "rt", "h", "lt", "lh", "rmt", NULL // state
};

#else
const char *jsonListSettings[] = { "cmd", "m", "am", "hm", "fm", "ot", "ht", "c0", "c1", "h0", "h1", "im", "cn", "cx", "ct", "tu", "ov", "rhm", "rh0", "rh1", "rmttemp", "rmtrh", NULL };
#endif
extern const char *cmdList[];

#ifndef REMOTE
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
      media.setPath("/"); // trigger file list send
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
    case WS_EVT_ERROR:    //error was received from the other end
      if(hvac.m_bRemoteStream && client->id() == WsRemoteID) // stop remote
      {
        hvac.m_bRemoteStream = false;
        hvac.m_notif = Note_RemoteOff;
      }
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
          FC.JsonParse((char *)data, 0, cmdList, remoteCallback);
        }
      }
      break;
  }
}
#else // Remote ws
void onWsEvent(AsyncWebSocket * server, AsyncWebSocketClient * client, AwsEventType type, void * arg, uint8_t *data, size_t len)
{  //Handle WebSocket event
  static bool rebooted = true;

  switch(type)
  {
    case WS_EVT_CONNECT:      //client connected
      if(rebooted)
      {
        rebooted = false;
        client->text( "{\"cmd\":\"alert\",\"text\":\"Restarted\"}" );
      }
      client->text(hvac.settingsJson()); // update free space
      media.setPath("/"); // trigger file list send
      break;
    case WS_EVT_DISCONNECT:    //client disconnected
    case WS_EVT_ERROR:    //error was received from the other end
      break;
    case WS_EVT_PONG:    //pong message was received (in response to a ping request maybe)
      break;
    case WS_EVT_DATA:  //data packet
      break;
  }
}
#endif

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
  connectTimer = time(nullptr);

  // attach AsyncWebSocket
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
#ifndef REMOTE

  server.on ( "/", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    // no 404, just an empty response
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

  // Main page  Hidden instead of / due to being externally accessible. Use your own here.
  server.on ( "/iot", HTTP_GET | HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(INTERNAL_FS, "/index.html");
  });

#endif
  server.on("/heap", HTTP_GET, [](AsyncWebServerRequest * request) { // just for checking
    request->send(200, "text/plain", String(ESP.getFreeHeap()));
  });

  server.on ( "/fm.html", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send ( 200, "text/html",  fileman);
  });

  server.on ( "/upload", HTTP_POST, [](AsyncWebServerRequest * request)
  {
    request->send( 200);
    ws.textAll(hvac.settingsJson()); // update free space
  },
  [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if(!index)
      request->_tempFile = media.createFile(filename);
    if(len)
     request->_tempFile.write((byte*)data, len);
    if(final)
      request->_tempFile.close();
   }
  );

  server.serveStatic("/", INTERNAL_FS, "/");
  server.begin();

#ifdef OTA_ENABLE
  ArduinoOTA.setHostname(hostName);
  ArduinoOTA.begin();
  ArduinoOTA.onStart([]() {
#ifndef REMOTE
    tm timeinfo;
    getLocalTime(&timeinfo);

    hvac.disable();
    hvac.dayTotals(timeinfo.tm_mday - 1); // save for reload
    ee.update();
    hvac.saveStats();
#endif
    jsonString js("alert");
    js.Var("text", "OTA Update Started");
    ws.textAll(js.Close());
    ws.closeAll();
    hvac.m_notif = Note_Updating; // Display a thing
    display.updateNotification(true);
    INTERNAL_FS.end();
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
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
      ee.hostIp[0] = MDNS.address(i)[0]; // update IP
      ee.hostIp[1] = MDNS.address(i)[1];
      ee.hostIp[2] = MDNS.address(i)[2];
      ee.hostIp[3] = MDNS.address(i)[3];
#else
      ee.hostIp[0] = MDNS.IP(i)[0]; // update IP
      ee.hostIp[1] = MDNS.IP(i)[1];
      ee.hostIp[2] = MDNS.IP(i)[2];
      ee.hostIp[3] = MDNS.IP(i)[3];
#endif
      hvac.m_notif = Note_HVACFound;
      break;
    }
  }
}
#endif

void handleServer()
{
#ifndef REMOTE
  static uint8_t n;
  if(++n >= 10)
  {
    historyDump(false);
    n = 0;
  }
#endif
#ifdef OTA_ENABLE
  ArduinoOTA.handle();
#endif
}

void WsSend(String s) // mostly for debug
{
  ws.textAll(s);
}

void wsPrint(String s)
{
  jsonString js("print");
  js.Var("text", s);
  ws.textAll(js.Close());
}

bool secondsServer() // called once per second
{
  bool bConn = false;

  if(!bConfigDone)
  {
    if( WiFi.smartConfigDone())
    {
      bConfigDone = true;
      connectTimer = time(nullptr);
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
    else if(time(nullptr) - connectTimer > 10) // failed to connect for some reason
    {
      connectTimer = time(nullptr);
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

  if(time(nullptr) >= FC.m_fc.loadDate + (3600*3) && (nUpdateDelay == 0) ) // > 3 hours old
  {
    FC.m_bUpdateFcst = true;
  }

#ifdef REMOTE

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

  tm timeinfo;
  getLocalTime(&timeinfo);

  if(FC.m_bUpdateFcst && FC.m_bUpdateFcstIdle && nUpdateDelay == 0 && timeinfo.tm_year > 124)
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
  else if(stat == FCS_Done)
  {
    display.m_bShowFC = true;
  }
#endif // !REMOTE
  return bConn;
}

#ifndef REMOTE
void parseParams(AsyncWebServerRequest *request)
{
  if(request->params() == 0)
    return;

  bool bPassGood;

  for( uint8_t i = 0; i < request->params(); i++ ) // password may be at end
  {
    const AsyncWebParameter* p = request->getParam(i);
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
    const AsyncWebParameter* p = request->getParam(i);
    String s = request->urlDecode(p->value());

    if(p->name() == "key");
    else if(p->name() == "restart")
    {
      ESP.restart();
    }
    else
    {
      if(p->name() == "fc")
        FC.m_bUpdateFcst = true;
      hvac.setVar(p->name(), s.toInt(), (char *)s.c_str(), ip );
    }
  }
}
#endif

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

void remoteCallback(int8_t iEvent, uint8_t iName, int32_t iValue, char *psValue)
{
  static int8_t cmd = 0;

#ifdef REMOTE
  switch(iName)
  {
    case 0: // cmd
      if(!strcmp(psValue, "settings"))
      {
        FC.setList(jsonListSettings);
        cmd = 1;
      }
      else if(!strcmp(psValue, "state"))
      {
        FC.setList(jsonListState);
        cmd = 2;
      }
      break;
  }
  if(iName) switch(cmd)
  {
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
      else if(iName == 3) // 3 = sum
      {
        String out = String("{\"cmd\":\"sum\",\"mon\":[");

        uint32_t (*pSecsMon)[3] = hvac.m_SecsMon;
        uint16_t (*pSecsDay)[3] = hvac.m_SecsDay;

        uint16_t tempSecsDay[32][3];
        uint32_t tempSecsMon[12][3];

        uint8_t mon = iValue & 0xF;
        uint16_t yr = (iValue >> 4) & 0xFFF;

        tm timeinfo;
        getLocalTime(&timeinfo);

        if(mon && mon != timeinfo.tm_mon+1) // month is 1-12, so 0 can be used as current
        {
          String sName = "/statsday"; // decode requested file
          sName += yr;
          sName += ".";
          sName += mon;
          sName += ".dat";

          File F;
          if(F = INTERNAL_FS.open(sName) )
          {
            F.read((byte*)&tempSecsDay, sizeof(tempSecsDay)); // read the requested file
            F.close();
            pSecsDay = tempSecsDay;
          }
          tm timeinfo;
          getLocalTime(&timeinfo);

          if(yr != timeinfo.tm_year+1900) // not currrent year
          {
            sName = "/statsmonth";
            sName += yr;
            sName += ".dat";
            if(F = INTERNAL_FS.open(sName) )
            {
              F.read((byte*)&tempSecsMon, sizeof(tempSecsMon)); // read the requested file
              F.close();
              pSecsMon = tempSecsMon;
            }
          }
        }

        for(int i = 0; i < 12; i++)
        {
          if(i) out += ",";
          out += "[";
          out += pSecsMon[i][0];
          out += ",";
          out += pSecsMon[i][1];
          out += ",";
          out += pSecsMon[i][2];
          out += "]";
        }
        out += "],\"day\":[";
        for(int i = 0; i < 31; i++)
        {
          if(i) out += ",";
          out += "[";
          out += pSecsDay[i][0];
          out += ",";
          out += pSecsDay[i][1];
          out += ",";
          out += pSecsDay[i][2];
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

// Remote websocket client
void WscSend(String s) // remote WebSocket
{
  if(!bWscConnected)
    return;
  WsClient.send(s);
}

void webSocketEventHandler(uint8_t event, char *data, uint16_t length)
{
  static bool bFirst = true;

  switch(event)
  {
    case WEBSOCKET_EVENT_ERROR:
      break;
    case WEBSOCKET_EVENT_CONNECTED:
      bWscConnected = true;
      FC.m_bUpdateFcst = true; // Get the forecast faster
      FC.m_bUpdateFcstIdle = true;
      if(hvac.m_notif == Note_Network) // remove net disconnect error
        hvac.m_notif = Note_None;
      if(bFirst)
      {
        bFirst = false;
        hvac.m_notif = Note_HVAC_connected; // helps see things connecting
      }
      break;
    case WEBSOCKET_EVENT_DISCONNECTED:
      bWscConnected = false;
      hvac.m_notif = Note_Network;
      break;
    case WEBSOCKET_EVENT_DATA_TEXT:
      FC.JsonParse((char *)data, 0, jsonListSettings, remoteCallback);
      break;
    case WEBSOCKET_EVENT_DATA_BINARY:
      if(length != sizeof(FC.m_fc) )
        break;
      memcpy((void*)&FC.m_fc, data, sizeof(FC.m_fc));
      FC.m_fc.loadDate = time(nullptr);
      FC.m_bUpdateFcstIdle = true;
      FC.m_bFcstUpdated = true;
      display.m_bShowFC = true;
      break;
  }
}

void startListener()
{
  IPAddress ip(ee.hostIp);
  WsClient.connect((char *)ip.toString().c_str(), "/ws", 80);
  WsClient.setCallback(webSocketEventHandler);
}
#endif
