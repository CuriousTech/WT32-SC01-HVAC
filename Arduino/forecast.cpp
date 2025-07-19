#include "Forecast.h"
#include "Display.h"
#include <Time.h>
#include "eeMem.h"
#include "Media.h"
#include "ScreenSavers.h"

#include <TFT_eSPI.h> // TFT_espi library
extern TFT_eSPI tft;
extern ScreenSavers ss;

// OWM Format: https://openweathermap.org/forecast5

// forecast retrieval

Forecast::Forecast()
{
  m_ac.onConnect([](void* obj, AsyncClient* c) { (static_cast<Forecast*>(obj))->_onConnect(c); }, this);
  m_ac.onDisconnect([](void* obj, AsyncClient* c) { (static_cast<Forecast*>(obj))->_onDisconnect(c); }, this);
  m_ac.onData([](void* obj, AsyncClient* c, void* data, size_t len) { (static_cast<Forecast*>(obj))->_onData(c, static_cast<char*>(data), len); }, this);

  for(int i = 0; i < FC_CNT + 2; i++)
   m_fc.Data[i].temp = -1000;
  m_fc.Date = 0;
}

void Forecast::init(int16_t tzOff)
{
  m_tzOffset = tzOff;
}

// File retrieval start
void Forecast::start(IPAddress serverIP, uint16_t port, bool bCelcius, int8_t type)
{
  if(m_ac.connected() || m_ac.connecting())
    return;

  m_status = FCS_Busy;
  m_bCelcius = bCelcius;
  m_serverIP = serverIP;
  if(!m_ac.connect(serverIP, port))
    m_status = FCS_ConnectError;
  m_bLocal = true;
  m_type = type;
}

// OpenWeaterMap start
void Forecast::start(char *pCityID, bool bCelcius)
{
  if(m_ac.connected()  || m_ac.connecting())
    return;
  m_bCelcius = bCelcius;
  strcpy(m_cityID, pCityID);
  m_status = FCS_Busy;
  if(!m_ac.connect("api.openweathermap.org", 80))
    m_status = FCS_ConnectError;
  m_bLocal = false;
}

int Forecast::checkStatus()
{
  if(m_status == FCS_Done)
  {
    m_status = FCS_Idle;
    m_fc.loadDate = time(nullptr);
    m_bUpdateFcstIdle = true;
    m_bFcstUpdated = true;
    return FCS_Done;
  }
  return m_status;
}

void Forecast::_onConnect(AsyncClient* client)
{
  String path = "GET /";

  if(m_bLocal) // file
  {
    switch(m_type)
    {
      case 0:
        path += "Forecast.log";
        break;
      case 1:
        path += "Forecast.json";
        break;
    }
    path += " HTTP/1.1\n"
      "Host: ";
    path += client->remoteIP().toString();
    path += "\n"
      "Connection: close\n"
      "Accept: */*\n\n";
  }
  else // OWM server
  {
    path = "GET /data/2.5/forecast?id=";
    path += m_cityID;
    path += "&appid=";
    path += APPID;   // Account
    path += "&units=";
    if(m_bCelcius)
      path += "celcius";
    else
      path += "imperial";
    path += " HTTP/1.1\n"
      "Host: ";
    path += client->remoteIP().toString();
    path += "\n"
      "Connection: close\n"
      "Accept: */*\n\n";
  }

  m_ac.add(path.c_str(), path.length());
  m_pBuffer = new char[OWBUF_SIZE + 2];
  if(m_pBuffer) m_pBuffer[0] = 0;
  else m_status = FCS_MemoryError;
  m_bufIdx = 0;
}

// build file in chunks
void Forecast::_onData(AsyncClient* client, char* data, size_t len)
{
  if(m_pBuffer == NULL || m_bufIdx + len >= OWBUF_SIZE)
    return;
  memcpy(m_pBuffer + m_bufIdx, data, len);
  m_bufIdx += len;
  m_pBuffer[m_bufIdx] = 0;
}

// Remove most outdated entries to fill in new
int Forecast::makeroom(uint32_t newTm)
{
  if(m_fc.Date == 0) // not filled in yet
    return 0;
  uint32_t tm2 = m_fc.Date;
  int fcIdx;
  for(fcIdx = 0; fcIdx < FC_CNT-4 && m_fc.Data[fcIdx].temp != -1000; fcIdx++) // find current active entry
  {
    if(tm2 >= newTm)
      break;
    tm2 += m_fc.Freq;
  }

  if(fcIdx > (FC_CNT - m_fcCnt - 1)) // not enough room left
  {
    int n = 2; // shift 6 hours back
    uint8_t *p = (uint8_t*)m_fc.Data;

    memcpy(p, p + (n * sizeof(forecastItem)), (FC_CNT - n) * sizeof(forecastItem) ); // make room
    m_fc.Date += m_fc.Freq * n; // increase the base date by the shift
    fcIdx -= n; // decrement the first new entry
  }

  return fcIdx;
}

void Forecast::_onDisconnect(AsyncClient* client)
{
  (void)client;

  char *p = m_pBuffer;
  m_status = FCS_Done;
  if(m_pBuffer == NULL)
    return;

  if(m_bufIdx == 0)
  {
    delete m_pBuffer;
    m_pBuffer = NULL;
    return;
  }

  m_fcIdx = 0;
  m_bFirst = false;
  m_lastTm = 0;

  switch(m_type)
  {
    case 0:
      processCDT(); // text file type
      break;
    case 1:
      processOWM(); // json type
      break;
  }
  m_fc.Data[m_fcIdx].temp = -1000; // mark past last as invalid
  delete m_pBuffer;
  m_pBuffer = NULL;
}

void Forecast::processOWM()
{
  const char *jsonListOw[] = { // root keys
    "cod",     // 0 (200=good)
    "message", // 1 (0)
    "cnt",     // 2 list count (40)
    "list",    // 3 the list
    "city",    // 4 "id", "name", "coord", "country", "population", "timezone", "sunrise", "sunset"
    NULL
  };

  char *p = m_pBuffer;

  if(p[0] != '{') // local copy has no headers
    while(p[4]) // skip all the header lines
    {
      if(p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n')
      {
        p += 4;
        break;
      }
      p++;
    }

  processJson(p, 0, jsonListOw);
}

// read data as comma delimited 'time,temp,rh,code' per line
void Forecast::processCDT()
{
  const char *p = m_pBuffer;
  m_status = FCS_Done;

  while(m_fcIdx < FC_CNT-1 && *p)
  {
    uint32_t tm = atoi(p); // this should be local time
    if(tm > 1700516696) // skip the headers
    {
      if(!m_bFirst)
      {
        m_bFirst = true;
        m_fcCnt = 56; // 7d @ 3h. This is the older weather.gov data
        m_fcIdx = makeroom(tm);
        if(m_fc.Date == 0)
          m_fc.Date = tm;
      }
      else
      {
        m_fc.Freq = tm - m_lastTm;
      }
      m_lastTm = tm;
      while(*p && *p != ',') p ++;
      if(*p == ',') p ++;
      else break;
      m_fc.Data[m_fcIdx].temp = (atof(p)*10);
      while(*p && *p != ',') p ++;
      if(*p == ',') p ++;
      else break;
      m_fc.Data[m_fcIdx].humidity = (atof(p)*10);
      while(*p && *p != ',') p ++;
      if(*p == ',') p ++;
      {
        m_fc.Data[m_fcIdx].id = atoi(p);
      }
      m_fcIdx++;
    }
    while(*p && *p != '\r' && *p != '\n') p ++;
    while(*p == '\r' || *p == '\n') p ++;
  }
  m_fc.Data[m_fcIdx].temp = -1000;
  delete m_pBuffer;
}

bool Forecast::getCurrentIndex(int8_t& fcOff, int8_t& fcCnt, uint32_t& tm)
{
  fcOff = 0;
  fcCnt = 0;
  tm = m_fc.Date;

  if(m_fc.Date == 0) // not read yet or time not set
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return false;
  }

  for(fcCnt = 0; fcCnt < FC_CNT && m_fc.Data[fcCnt].temp != -1000; fcCnt++) // get current time in forecast and valid count
  {
    if( tm + m_fc.Freq < time(nullptr) - m_tzOffset)
    {
      fcOff++;
      tm += m_fc.Freq;
    }
  }

  if(fcCnt >= FC_CNT || m_fc.Data[fcOff].temp == -1000 ) // entire list outdated
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return false;
  }

  return true;
}

void Forecast::callback(int8_t iEvent, uint8_t iName, int32_t iValue, char *psValue)
{
  switch(iEvent)
  {
    case 0: // root
      switch(iName)
      {
        case 0: // cod
          if(iValue != 200)
            m_status = FCS_Fail;
          break;
        case 1: // message
          break;
        case 2: // cnt
          m_fcCnt = iValue;
          break;
        case 3: // list
          {
            static const char *jsonList[] = {
              "dt",      // 0
              "main",    // 1
              "weather", // 2
//              "clouds",  // 3
//              "wind",
//              "visibility",
//              "pop",
//              "sys",
//              "dt_txt",
              NULL
            };

            processJson(psValue, 1, jsonList);
          }
          break;
        case 4: // city
          {
            static const char *jsonCity[] = {
              "id", // 0  3163858,
              "timezone", // 1 7200,
              "sunrise", // 2 1661834187,
              "sunset", // 3 1661882248
//              "name", //  "Zocca",
//              "coord", //  {"lat": 44.34, "lon": 10.99}
//              "country", // "IT",
//              "population", // 4593,
              NULL
            };
            processJson(psValue, 4, jsonCity);
          }
          break;
      }
      break;
    case 1: // list
      switch(iName)
      {
        case 0: // dt
          if(!m_bFirst)
          {
            m_bFirst = true;
            m_fcIdx = makeroom(iValue);
            if(m_fc.Date == 0) // first time uses external date, subsequent will shift
              m_fc.Date = iValue;
          }
          else
          {
            m_fc.Freq = iValue - m_lastTm; // figure out frequency of periods
          }
          m_lastTm = iValue;
          break;
        case 1: // main
          {
            const char *jsonList[] = {
              "temp",      // 0
              "feels_like", // 1
              "temp_min",   // 2
              "temp_max",  // 3
              "pressure",
              "humidity", // 5
              "temp_kf",
              NULL
            };
            processJson(psValue, 2, jsonList);
          }
          break;
        case 2: // weather
          {
            static const char *jsonList[] = {
              "id", // 802
              "main", // Clouds
              "description", // scattered clouds
              "icon", // 03d
              NULL
            };
            processJson(psValue, 3, jsonList);
          }
          if(m_fcIdx < FC_CNT - 1)
            m_fcIdx++;
          break;
      }
      break;
    case 2: // main
      switch(iName)
      {
        case 0: // temp
          m_fc.Data[m_fcIdx].temp = (atof(psValue)*10);
          break;
        case 5: // humidity
          m_fc.Data[m_fcIdx].humidity = (atoi(psValue)*10);
          break;
      }
      break;
    case 3: // weather
      switch(iName)
      {
        case 0: // id 804
          m_fc.Data[m_fcIdx].id = atoi(psValue);
          break;
        case 1: // main "Clouds"
          break;
        case 2: // description "Overcast clouds"
          break;
        case 3: // icon 04d (id has more values)
          break;
      }
      break;
    case 4: // city
      switch(iName)
      {
        case 0: // id
          break;
        case 1: // timezone
          break;
        case 2: // sunrise
          break;
        case 3: // sunset
          break;
      }
      break;
  }
}

// Use this for space saving
void Forecast::JsonParse(char *p, int8_t event, const char **jsonList, void (*pcb)(int8_t iEvent, uint8_t iName, int32_t iValue, char *psValue))
{
  m_pCallback = pcb;
  processJson(p, event, jsonList);
  m_pCallback = NULL;
  m_pJsonList = NULL;
}

// Modify the list while parsing
void Forecast::setList(const char **jsonList)
{
  m_pJsonList = jsonList;
}

void Forecast::processJson(char *p, int8_t event, const char **jsonList)
{
  char *pPair[2]; // param:data pair
  int8_t brace = 0;
  int8_t bracket = 0;
  int8_t inBracket = 0;
  int8_t inBrace = 0;

  while(*p)
  {
    p = skipwhite(p);
    if(*p == '{'){p++; brace++;}
    if(*p == '['){p++; bracket++;}
    if(*p == ',') p++;
    p = skipwhite(p);

    bool bInQ = false;
    if(*p == '"'){p++; bInQ = true;}
    pPair[0] = p;
    if(bInQ)
    {
       while(*p && *p!= '"') p++;
       if(*p == '"') *p++ = 0;
    }else
    {
      while(*p && *p != ':') p++;
    }
    if(*p != ':')
      return;

    *p++ = 0;
    p = skipwhite(p);
    bInQ = false;
    if(*p == '{') inBrace = brace+1; // data: {
    else if(*p == '['){p++; inBracket = bracket+1;} // data: [
    else if(*p == '"'){p++; bInQ = true;}
    pPair[1] = p;
    if(bInQ)
    {
       while(*p && *p!= '"') p++;
       if(*p == '"') *p++ = 0;
    }else if(inBrace)
    {
      while(*p && inBrace != brace){
        p++;
        if(*p == '{') inBrace++;
        if(*p == '}') inBrace--;
      }
      if(*p=='}') p++;
    }else if(inBracket)
    {
      while(*p && inBracket != bracket){
        p++;
        if(*p == '[') inBracket++;
        if(*p == ']') inBracket--;
      }
      if(*p == ']') *p++ = 0;
    }else while(*p && *p != ',' && *p != '\r' && *p != '\n') p++;
    if(*p) *p++ = 0;
    p = skipwhite(p);
    if(*p == ',') *p++ = 0;

    inBracket = 0;
    inBrace = 0;
    p = skipwhite(p);

    if(pPair[0][0])
    {
      if(m_pJsonList) // externally modified (remote mode)
        jsonList = m_pJsonList;

      for(int i = 0; jsonList[i]; i++)
      {
        if(!strcmp(pPair[0], jsonList[i]))
        {
          int32_t n = atol(pPair[1]);
          if(!strcmp(pPair[1], "true")) n = 1; // bool case
          if(m_pCallback)
            m_pCallback(event, i, n, pPair[1]);
          else
            callback(event, i, n, pPair[1]);
          break;
        }
      }
    }

  }
}

char *Forecast::skipwhite(char *p)
{
  while(*p == ' ' || *p == '\t' || *p =='\r' || *p == '\n')
    p++;
  return p;
}

// All the rest is for display
struct iconLookup
{
  uint16_t id;
  uint8_t icon;
  uint16_t color;
};

static const iconLookup ic[] = {
  //id,icon,color
  {200,11,TFT_RED}, // Thunderstorm  thunderstorm with light rain   11d
  {201,11,TFT_RED}, // Thunderstorm  thunderstorm with rain   11d
  {202,11,TFT_RED}, // Thunderstorm  thunderstorm with heavy rain   11d
  {210,11,TFT_RED}, // Thunderstorm  light thunderstorm   11d
  {211,11,TFT_RED}, // Thunderstorm  thunderstorm   11d
  {212,11,TFT_RED}, // Thunderstorm  heavy thunderstorm   11d
  {221,11,TFT_RED}, // Thunderstorm  ragged thunderstorm  11d
  {230,11,TFT_RED}, // Thunderstorm  thunderstorm with light drizzle  11d
  {231,11,TFT_RED}, // Thunderstorm  thunderstorm with drizzle  11d
  {232,11,TFT_RED}, // Thunderstorm  thunderstorm with heavy drizzle  11d
  {300,9,TFT_NAVY}, // Drizzle light intensity drizzle  09d
  {301,9,TFT_NAVY}, // Drizzle drizzle  09d
  {302,9,TFT_NAVY}, // Drizzle heavy intensity drizzle  09d
  {310,9,TFT_NAVY}, // Drizzle light intensity drizzle rain   09d
  {311,9,TFT_NAVY}, // Drizzle drizzle rain   09d
  {312,9,TFT_NAVY}, // Drizzle heavy intensity drizzle rain   09d
  {313,9,TFT_NAVY}, // Drizzle shower rain and drizzle  09d
  {314,9,TFT_NAVY}, // Drizzle heavy shower rain and drizzle  09d
  {321,9,TFT_NAVY}, // Drizzle shower drizzle   09d
  {500,10,TFT_BLUE}, // Rain  light rain   10d
  {501,10,TFT_BLUE}, // Rain  moderate rain  10d
  {502,10,TFT_BLUE}, // Rain  heavy intensity rain   10d
  {503,10,TFT_BLUE}, // Rain  very heavy rain  10d
  {504,10,TFT_BLUE}, // Rain  extreme rain   10d
  {511,13,TFT_BLUE}, // Rain  freezing rain  13d
  {520,9,TFT_BLUE}, // Rain  light intensity shower rain  09d
  {521,9,TFT_BLUE}, // Rain  shower rain  09d
  {522,9,TFT_BLUE}, // Rain  heavy intensity shower rain  09d
  {531,9,TFT_BLUE}, // Rain  ragged shower rain   09d
  {600,12,TFT_WHITE}, // Snow  light snow   13d
  {601,12,TFT_WHITE}, // Snow  snow   13d
  {602,13,TFT_WHITE}, // Snow  heavy snow   13d
  {611,12,TFT_WHITE}, // Snow  sleet  13d
  {612,12,TFT_WHITE}, // Snow  light shower sleet   13d
  {613,12,TFT_WHITE}, // Snow  shower sleet   13d
  {615,12,TFT_WHITE}, // Snow  light rain and snow  13d
  {616,12,TFT_WHITE}, // Snow  rain and snow  13d
  {620,12,TFT_WHITE}, // Snow  light shower snow  13d
  {621,12,TFT_WHITE}, // Snow  shower snow  13d
  {622,12,TFT_WHITE}, // Snow  heavy shower snow  13d
  {701,50,TFT_LIGHTGREY}, // Mist  mist   50d
  {711,50,TFT_DARKGREY}, // Smoke smoke  50d
  {721,50,TFT_LIGHTGREY}, // Haze  haze   50d
  {731,50,TFT_LIGHTGREY}, // Dust  sand/dust whirls   50d
  {741,50,TFT_SILVER}, // Fog fog  50d
  {751,50,TFT_SILVER}, // Sand  sand   50d
  {761,50,TFT_LIGHTGREY}, // Dust  dust   50d
  {762,50,TFT_LIGHTGREY}, // Ash volcanic ash   50d
  {771,50,TFT_LIGHTGREY}, // Squall  squalls  50d
  {781,50,TFT_YELLOW}, // Tornado tornado  50d
  {800,1,0}, // Clear clear sky  01d
  {801,2,0}, // Clouds  few clouds: 11-25%   02d
  {802,3,0}, // Clouds  scattered clouds: 25-50%   03d
  {803,2,0}, // Clouds  broken clouds: 51-84%  04d
  {804,5,0}, // Clouds  overcast clouds: 85-100%   04d
  {  0,1,0},
};

#define FC_Left     37
#define FC_Top      34
#define FC_Width   430
#define FC_Height  143

String Forecast::makeName(uint8_t icon, uint8_t h)
{
  static String sName;

  if(icon < 10) sName = "0";
  sName += icon;
  if(icon == 3 || icon == 4 ||icon == 5 || icon == 13) // don't have 'n' for these
    sName += 'd';
  else
    sName += (h > 5 && h < 20) ? "d" : "n";
  sName += ".png";
  return sName;
}

bool Forecast::forecastPage()
{
  if(m_fc.Date == 0) // no data yet
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return false;
  }

  int8_t fcOff;
  int8_t fcDispOff = 0;
  int8_t fcCnt;
  uint32_t unused;

  if(!getCurrentIndex(fcOff, fcCnt, unused))
    return false;

  time_t tt = (m_fc.Date + m_tzOffset + (fcOff * m_fc.Freq));
  tm *pTimeinfo = gmtime(&tt ); // get current hour

  if(fcOff >= (pTimeinfo->tm_hour / 3) )
    fcDispOff = fcOff - (pTimeinfo->tm_hour / 3); // shift back to start of day
  else
    fcDispOff = fcOff; // else just shift the first day

  tt = (time_t)(m_fc.Date + m_tzOffset + (fcDispOff * m_fc.Freq));
  tm *ptmE = gmtime(&tt );  // get current hour after adjusting for display offset

  int8_t hrng = fcCnt - fcDispOff;
  if(hrng > ee.fcDisplay)
    hrng = ee.fcDisplay; // shorten to user display range

  // Update min/max
  int16_t tmin;
  int16_t tmax;
  getMinMax(tmin, tmax, fcDispOff, hrng);

  int16_t y = FC_Top + 5;
  int16_t incy = (FC_Height-20) / 3;
  int16_t dec = (tmax - tmin) / 3;
  int16_t t = tmax;

  int hrs = hrng * m_fc.Freq / 3600; // normally ~180 hours

  if(hrs <= 0) // divide by 0
    return false;

  display.goDark();
  media.loadImage("bgForecast.png", 0, 0); // load the background image

  // temp scale
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(rgb16(0, 63, 31)); // cyan text
  for(int i = 0; i < 4; i++)
  {
    tft.drawNumber(t/10, 8, y);
    y += incy;
    t -= dec;
  }

  y = FC_Height / 3 + FC_Top;
  tft.drawLine(FC_Left, y, FC_Left+FC_Width, y, rgb16(7, 14, 7) ); // dark gray
  y += FC_Height / 3;
  tft.drawLine(FC_Left, y, FC_Left+FC_Width, y, rgb16(7, 14, 7) ); // dark gray

  tft.setTextDatum(TC_DATUM); // center day on noon

  uint8_t wkday = ptmE->tm_wday;              // current DOW

  int16_t low = 1500, high = -1500;
  uint16_t i2 = fcDispOff; // index into forecast
  uint16_t x;
  uint8_t h = ptmE->tm_hour; // starting hour
  uint8_t iDay = 0; // icon position day
  uint16_t lastDayX = 0;

  memset(m_fcIcon, 0, sizeof(m_fcIcon));

  for(int i = 0; i < hrs; i++)
  {
    uint8_t iconIdx;

    if((i % (m_fc.Freq / 3600)) == 0) // 3 hours
    {
      uint16_t id = m_fc.Data[i2].id;

      if(id)
      {
        for(iconIdx = 0; ic[iconIdx].id; iconIdx++)
          if(id == ic[iconIdx].id)
            break;

        if(ic[iconIdx].color && x >= FC_Left && x < DISPLAY_WIDTH - 20)
          tft.fillRect(x, FC_Top+FC_Height, 10, 5, ic[iconIdx].color); // show color bar (10 wide for 3 hour)

        m_fcIcon[iDay].icon[h / 3] = ic[iconIdx].icon; // fix, assuming 3 hour
      }

      uint16_t t = m_fc.Data[i2].temp;
      if(t != -1000)
      {
        if( high <= t) high = t;// highest
        if( low >= t)  low = t; // lowest
      }
      i2++;
    }

    x = i * FC_Width / hrs + FC_Left;

    if(h == 12) // noon line and weekday
    {
      tft.drawLine(x, FC_Top, x, FC_Top+FC_Height, rgb16(7, 14, 7) ); // dark gray
      if(x < FC_Left + FC_Width - 26) // skip last day if too far right
      {
        tft.drawString( ss.dayShortStr(wkday), x, 10);
      }
    }
    else if(h == 0) // new day (draw line, prev day peaks, and prev icon)
    {
      tft.drawLine(x, FC_Top+1, x, FC_Top+FC_Height-2, rgb16(14, 28, 14) ); // (light gray)

      if(low != 1500) // show peaks
      {
        if(x > FC_Left + 23)
          tft.drawFloat((float)high / 10, 1, x - 20, FC_Top+FC_Height + 10);
        if(x >FC_Left + 63) // first one shouldn't go too far left
          tft.drawFloat((float)low / 10, 1, x - 54, FC_Top+FC_Height + 10 + 21);
      }

      iconAni *pIcon = &m_fcIcon[iDay - 1];
      pIcon->x = x;
      uint8_t h1 = 23;
      while(h1 > 15 && pIcon->icon[h1/3]) // find first valid in prev day down to noon
        h1 -= 3;
      drawIcon(iDay - 1, h1, 0); // show noon icon first if valid

      lastDayX = x;

      low = 1500; // reset temp range
      high = -1500;
    }

    if(++h >= 24) // next day
    {
      h = 0;
      iDay++;
      if(++wkday > 6) wkday = 0;
    }
  }

  // get that last partial day
  x = lastDayX + (FC_Width * 24 / hrs);

  if(x < DISPLAY_WIDTH + 30 && low != 1500)
  {
    if(x < DISPLAY_WIDTH - 10 && high != -1000)
      tft.drawFloat((float)high / 10, 1, x - 20, FC_Top+FC_Height + 10);
    tft.drawFloat((float)low / 10, 1, x - 54, FC_Top+FC_Height + 10 + 21);

    m_fcIcon[iDay].x = x;
    int8_t h1 = 0;
    while(h1 <= 10 && m_fcIcon[iDay].icon[h1/3]) // find last valid in last day, up to noon
      h1 += 3;
    drawIcon(iDay, h1, 0);
  }

  tft.setTextDatum(TL_DATUM);

  uint16_t x2, y2, rh2;

  uint16_t i;

  for(i = 0; i < hrng; i++) // should be 41 data points
  {
    uint16_t t = m_fc.Data[i + fcDispOff].temp;
    uint16_t y1 = FC_Top+FC_Height - 1 - (t - tmin) * (FC_Height-2) / (tmax-tmin);
    uint16_t x1 = i * FC_Width / hrng + FC_Left;
    int rhY = (FC_Top + FC_Height) - (FC_Height * m_fc.Data[i + fcDispOff].humidity / 1000);

    if(i) // 1st run just saves pos
    {
      tft.drawLine(x2, rh2, x1, rhY, rgb16(0, 30, 0) ); // rh (green)
      tft.drawLine(x2, y2, x1, y1, (i== fcOff) ? rgb16(20, 0, 8) : rgb16(31, 0, 0) ); // red (current purple)
    }
    x2 = x1;
    y2 = y1;
    rh2 = rhY;
  }

  m_iconIdx = 0; // starting pos of icons
  return true;
}

void Forecast::getMinMax(int16_t& tmin, int16_t& tmax, int8_t offset, int8_t range)
{
  // Update min/max
  tmax = tmin = m_fc.Data[offset].temp;

  // Get min/max of current forecast
  for(int8_t i = offset + 1; i < offset + range && m_fc.Data[i].temp != -1000 && i < FC_CNT; i++)
  {
    int16_t t = m_fc.Data[i].temp;
    if(tmin > t) tmin = t;
    if(tmax < t) tmax = t;
  }

  if(tmin == tmax) tmax++;   // div by 0 check
}

int16_t Forecast::getCurrentTemp(int& shiftedTemp, uint8_t shiftMins)
{
  int8_t fcOff;
  int8_t fcCnt;
  uint32_t tmO;

  if(!getCurrentIndex(fcOff, fcCnt, tmO))
    return 0;

  tm timeinfo;
  getLocalTime(&timeinfo);

  int16_t m = timeinfo.tm_min;
  uint32_t tmNow = time(nullptr) - m_tzOffset;
  int16_t r = m_fc.Freq / 60; // usually 3 hour range (180 m)

  if( tmNow >= tmO )
    m = (tmNow - tmO) / 60;  // offset = minutes past forecast up to 179

  int16_t temp = tween(m_fc.Data[fcOff].temp, m_fc.Data[fcOff+1].temp, m, r);

  m += shiftMins; // get the adjust shift
  while(m >= r && fcOff < fcCnt - 2 && m_fc.Data[fcOff + 1].temp != -1000) // skip a window if 3h+ over range
  {
    fcOff++;
    m -= r;
  }

  while(m < 0 && fcOff) // skip a window if 3h+ prior to range
  {
    fcOff--;
    m += r;
  }
  if(m < 0) m = 0; // if just started up

  shiftedTemp = tween(m_fc.Data[fcOff].temp, m_fc.Data[fcOff+1].temp, m, r);

  return temp;
}

// get value at current minute between hours
int Forecast::tween(int16_t t1, int16_t t2, int m, int r)
{
  if(r == 0) r = 1; // div by zero check
  float t = (float)(t2 - t1) * (m * 100 / r) / 100;
  return (int)(t + (float)t1);
}

// Animate the weather icons
void Forecast::forecastAnimate()
{
  static uint8_t dly = 2; // 2 seconds

  if(--dly)
    return;
  dly = 2;

  for(uint8_t d = 0; d < 7; d++) // go through the days
  {
    iconAni *pIcon = &m_fcIcon[d];
    if(pIcon->x == 0)
      continue;

    drawIcon(d, m_iconIdx * 3, 0);

    uint16_t x = pIcon->x;
    uint8_t w = 80;
  
    if(x < 90) x = 90, w = 90 - pIcon->x;
    else if(x > DISPLAY_WIDTH-90) w = min(x - (DISPLAY_WIDTH - 90), 80);

    tft.fillRect(x - 80, DISPLAY_HEIGHT - 14, min(w, (uint8_t)(m_iconIdx * 10)), 2, TFT_BLUE); // progress bar
  }

  if(++m_iconIdx >= 8)
      m_iconIdx = 0;
}

void Forecast::drawIcon(uint8_t d, uint8_t h, uint16_t x)
{
  iconAni *pIcon = &m_fcIcon[d];
  if(x == 0) // not forced
    x = pIcon->x;

  uint8_t icon = pIcon->icon[h/3];

  if(icon == 0)
    return;

  String sIcon = makeName(icon, h);

  if(x > DISPLAY_WIDTH-90) // right side partial image
  {
    x -= 80;
    int16_t w = min(DISPLAY_WIDTH - 10 - x, 80);
    media.loadImage(sIcon, x, DISPLAY_HEIGHT - 92, 0, 0, w, 80);
  }
  else if(x < 90) // left side partial image
    media.loadImage(sIcon, 10, DISPLAY_HEIGHT - 92, 90 - x, 0, x - 10, 80);
  else
    media.loadImage(sIcon, x - 80, DISPLAY_HEIGHT - 92);
}
