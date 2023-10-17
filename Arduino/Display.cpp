#include "HVAC.h"
#include "display.h"
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include "jsonstring.h"
#include <TimeLib.h>
#include <SPIFFS.h>
#define FileSys SPIFFS   // SPIFFS is needed for AsyncWebServer
#include "eeMem.h"
#include "music.h"
Music mus;
#include <TFT_eSPI.h>                                     // TFT_espi library
TFT_eSPI tft = TFT_eSPI();

// FT6206, FT6336U - touchscreen library                  // modified -> Wire.begin(18, 19)
#include "Adafruit_FT6206.h"
Adafruit_FT6206 ts = Adafruit_FT6206();

#include "digitalFont.h"

const char *_days_short[] PROGMEM = {"BAD", "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char *_months_short[] PROGMEM = {"Nul", "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

int16_t xpos = 0; // used for callback draw position
int16_t ypos = 0;

extern HVAC hvac;
extern void WsSend(String s);

PNG png;

void pngDraw(PNGDRAW *pDraw)
{
  static uint16_t lineBuffer[480+2];
  png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);
  tft.pushImage(xpos, ypos + pDraw->y, pDraw->iWidth, 1, lineBuffer);
}

File pngfile;

void * pngOpen(const char *filename, int32_t *size) {
  pngfile = FileSys.open(filename, "r");
  *size = pngfile.size();
  return &pngfile;
}

void pngClose(void *handle) {
  File pngfile = *((File*)handle);
  if (pngfile) pngfile.close();
}

int32_t pngRead(PNGFILE *page, uint8_t *buffer, int32_t length) {
  if (!pngfile) return 0;
  page = page; // Avoid warning
  return pngfile.read(buffer, length);
}

int32_t pngSeek(PNGFILE *page, int32_t position) {
  if (!pngfile) return 0;
  page = page; // Avoid warning
  return pngfile.seek(position);
}

void Display::init(void)
{
  memset(m_fc.Data, -127, sizeof(m_fc.Data));
  m_fc.Date = 0;

  ts.begin(40);                                           // adafruit touch
  tft.init();                                             // TFT_eSPI
  tft.setRotation(1);                                     // set desired rotation
  tft.setTextDatum(TL_DATUM);
  tft.setFreeFont(&FreeSans12pt7b);
  m_brightness = 255;

  FileSys.begin();
  mus.init();

  screen(true);
}

bool Display::screen(bool bOn)
{
  static bool bOldOn = false;

  if(bOldOn && m_currPage) // not in sync
    bOldOn = false;

  if(bOn == false && m_currPage == Page_Graph) // last sequence was graph
    bOn = true;

  m_backlightTimer = DISPLAY_TIMEOUT; // update the auto backlight timer

  if(bOn)
  {
    if( bOn == bOldOn )
      return false; // no change occurred
    m_currPage = Page_Thermostat;
    loadImage("/bg.png", 0, 0);
    refreshAll();
    m_brightness = 255;
  }
  else switch(m_currPage)
  {
    case Page_Clock: // already clock
      randomSeed(micros());
      m_currPage = Page_Blank; // lines
      m_brightness = 255;
      tft.fillScreen(TFT_BLACK);
      break;
    case Page_Blank: // lines
      m_currPage = Page_Graph; // chart thing
      loadImage("/bgBlank.png", 0, 0);
      fillGraph();
      break;
    default:  // probably thermostat
      m_currPage = Page_Clock; // clock
      loadImage("/bgClock.png", 0, 0);
      updateClock();
      m_brightness = 60;
      break;
  }
  bOldOn = bOn;
  return true;
}

void Display::service(void)
{
  static int8_t currBtn;
  static bool bPress;

  dimmer();

  Lines();

  if (ts.touched())
  {
    TS_Point p = ts.getPoint();
    uint16_t y = 320 - p.x;     // adjust touch point for rotation = 1
    uint16_t x = p.y;

    if(m_btnMode) // up/dn being held
    {
      if(--m_btnDelay <= 0)
      {
        buttonRepeat();
        m_btnDelay = 40; // repeat speed
      }
      return;
    }

    if(bPress == false) // only need 1 touch
    {
      if(m_currPage) // not on thermostat
      {
        screen(true); // switch back to thermostat screen
      }
      else
      {
        currBtn = -1;
        for(uint8_t i = 0; i < BTN_CNT; i++)
        {
          if (x >= m_btn[i].x && x <= m_btn[i].x + m_btn[i].w && y >= m_btn[i].y && y <= m_btn[i].y + m_btn[i].h)
          {
//            Serial.print("btn ");
            currBtn = i;
//            Serial.println(currBtn);
            buttonCmd(currBtn);
          }
        }
      }
    }
    
    bPress = true;
  }
  else if(bPress) // release
  {
    bPress = false;
    if(currBtn >= 0)
    {
//      Serial.print("release ");
//      Serial.println(currBtn);
      m_btnMode = 0; // Up/Dn release
    }
    currBtn = -1;
  }
  else // idle
  {
  }
}

void Display::buttonCmd(uint8_t btn)
{
  m_backlightTimer = DISPLAY_TIMEOUT;
  m_brightness = 255;

  if( m_backlightTimer == 0)
    return;

  mus.add(6000, 20);

  switch(btn)
  {
    case Btn_IndCH ... Btn_IndHL: // indicator + values cool hi, lo, heat hi, lo
      hvac.m_bLink = (m_adjustMode == btn-Btn_IndCH);
      m_adjustMode = btn-Btn_IndCH;
      break;

    case Btn_Up: // Up button
      m_btnMode = 1;
      buttonRepeat();
      m_btnDelay = 80; // first repeat
      break;
    case Btn_Dn: // Down button
      m_btnMode = 2;
      buttonRepeat();
      m_btnDelay = 80;
      break;

    case Btn_Fan:
      if(ee.b.bLock) break;
      hvac.setFan( (hvac.getFan() == FM_On) ? FM_Auto : FM_On ); // Todo: Add 3rd icon
      updateModes(false); // faster feedback
      break;
    case Btn_Mode:
      if(ee.b.bLock) break;
      hvac.setMode( (hvac.getSetMode() + 1) & 3 );
      updateModes(false); // faster feedback
      break;
    case Btn_HeatMode:
      if(ee.b.bLock) break;
      hvac.setHeatMode( (hvac.getHeatMode() + 1) % 3 );
      updateModes(false); // faster feedback
      break;
    case Btn_Note:
      if(ee.b.bLock) break;
      hvac.m_notif = Note_None;
      break;
    case Btn_Fc: // forecast
      m_currPage = Page_Graph;
      loadImage("/bgBlank.png", 0, 0);
      fillGraph();
      break;
    case Btn_Time: // time
      m_currPage = Page_Clock;
      loadImage("/bgClock.png", 0, 0);
      updateClock();
      break;
    case Btn_TargetTemp:
      if(ee.b.bLock) break;
      hvac.enableRemote();
      break;
    case Btn_InTemp: // in
    case Btn_Rh: // rh
      updateTemps();
      break;
//#define PWLOCK  // uncomment for password entry unlock
#ifdef PWLOCK
    case 25: // lock
      if(ee.b.bLock)
      {
        textIdx = 2;
//        nex.itemText(0, ""); // clear last text
//        nex.setPage("keyboard"); // go to keyboard
//        nex.itemText(1, "Enter Password");
      }
      else
        ee.b.bLock = 1;
#else
      ee.b.bLock = !ee.b.bLock;
#endif
//      nex.itemPic(9, ee.b.bLock ? 20:21);
      break;
  }
}

// called each second
void Display::oneSec()
{
  if(WiFi.status() != WL_CONNECTED)
    return;
  
  updateClock();
  updateRunIndicator(false); // running stuff
  displayTime();    // time update every second
  updateModes(false);    // mode, heat mode, fan mode
  updateTemps();    // 
  updateAdjMode(false); // update touched temp settings
  updateNotification(false);
  updateRSSI();     //
  if( m_backlightTimer ) // the dimmer thing
  {
    if(--m_backlightTimer == 0)
        screen(false);
  }
  static uint8_t lastState;
  static bool lastFan;
  if(--m_temp_counter <= 0 || hvac.getState() != lastState || hvac.getFanRunning() != lastFan)
  {
    displayOutTemp();
    addGraphPoints();
    lastState = hvac.getState();
    lastFan = hvac.getFanRunning();
  }

  if(m_currPage == Page_Thermostat && m_bFcstUpdated)
  {
    m_bFcstUpdated = false;
    drawForecast(true);
  }
}

void Display::displayOutTemp()
{
  if(m_fc.Date == 0) // not read yet or time not set
    return;

  int iH = 0;
  int m = minute();
  uint32_t tmNow = now() - ((ee.tz+hvac.m_DST)*3600);
  uint32_t fcTm = m_fc.Date;

  if( tmNow >= fcTm)
  {
    for(iH = 0; tmNow > fcTm && m_fc.Data[iH] != -127 && iH < FC_CNT - 1; iH++)
      fcTm += m_fc.Freq;
 
    if(iH)
    {
      iH--; // set iH to current 3 hour frame
      fcTm -= m_fc.Freq;
    }
    m = (tmNow - fcTm) / 60;  // offset = minutes past forecast
    if(m > m_fc.Freq/60) m = minute();
  }

  int r = m_fc.Freq / 60; // usually 3 hour range (180 m)
  int outTempReal = tween(m_fc.Data[iH], m_fc.Data[iH+1], m, r);
  int outTempShift = outTempReal;
  int fcOffset = ee.fcOffset[hvac.m_modeShadow == Mode_Heat];

  m += fcOffset % 60;
  if(m < 0) m += 60;
  if(m >= r)
  {
    iH++;
    m -= r;
  }
  if(fcOffset <= r) while(fcOffset <= r && iH)
  {
    iH--;
    fcOffset += r;
  }
  while(fcOffset >= r)
  {
    iH++;
    fcOffset -= r;
  }

  outTempShift = tween(m_fc.Data[iH], m_fc.Data[iH+1], m, r);

  tft.setTextColor( rgb16(0, 63, 31), rgb16(0, 4, 10) );
  tft.setFreeFont(&digitaldreamFatNarrow_14ptFont);
  if(m_currPage == Page_Thermostat)
    tft.drawFloat((float)outTempReal/10, 1, m_btn[Btn_OutTemp].x, m_btn[Btn_OutTemp].y );

  // Summer/winter curve.  Summer is delayed 3 hours
  hvac.updateOutdoorTemp( outTempShift );
}

void Display::updateTemps(void)
{
  static uint16_t last[7];  // only draw changes
  const uint16_t bgColor = rgb16(0, 5, 10);
  if(m_currPage)
  {
    memset(last, 0, sizeof(last));
    return;
  }

  tft.setFreeFont(&digitaldreamFatNarrow_14ptFont);
  tft.setTextColor( hvac.m_bRemoteStream ? rgb16(31, 0, 15) : rgb16(0, 63, 31), bgColor );

  if(last[0] != hvac.m_inTemp)
    tft.drawFloat((float)(last[0] = hvac.m_inTemp)/10, 1, m_btn[Btn_InTemp].x, m_btn[Btn_InTemp].y );
  if(last[1] != hvac.m_rh)
    tft.drawFloat((float)(last[1] = hvac.m_rh)/10, 1, m_btn[Btn_Rh].x, m_btn[Btn_Rh].y );

  tft.setTextColor( rgb16(0, 63, 31), bgColor );
  if(last[2] != hvac.m_targetTemp)
    tft.drawFloat((float)(last[2] = hvac.m_targetTemp)/10, 1, m_btn[Btn_TargetTemp].x, m_btn[Btn_TargetTemp].y );

  tft.setTextColor( rgb16(0, 8, 31), bgColor );
  if(last[3] != ee.coolTemp[1])
    tft.drawFloat((float)(last[3] = ee.coolTemp[1])/10, 1, m_btn[Btn_CoolTempH].x, m_btn[Btn_CoolTempH].y );
  if(last[4] != ee.coolTemp[0])
    tft.drawFloat((float)(last[4] = ee.coolTemp[0])/10, 1, m_btn[Btn_CoolTempL].x, m_btn[Btn_CoolTempL].y );
  tft.setTextColor( rgb16(31, 4, 0), bgColor );
  if(last[5] != ee.heatTemp[1])
    tft.drawFloat((float)(last[5] = ee.heatTemp[1])/10, 1, m_btn[Btn_HeatTempH].x, m_btn[Btn_HeatTempH].y );
  tft.setTextColor( rgb16(31, 4, 4), bgColor );
  if(last[6] != ee.heatTemp[0])
    tft.drawFloat((float)(last[6] = ee.heatTemp[0])/10, 1, m_btn[Btn_HeatTempL].x, m_btn[Btn_HeatTempL].y );
}

void Display::updateModes(bool bForce) // update any displayed settings
{
  static bool bFan = true; // set these to something other than default to trigger them all
  static int8_t FanMode = 4;
  static uint8_t nMode = 10;
  static uint8_t heatMode = 10;

  if(m_currPage || bForce)
  {
    FanMode = nMode = heatMode = 10;
  }

  if(m_currPage)
    return;

  if( (FanMode != hvac.getFan() || bFan != hvac.getFanRunning()) )
  {
    int idx = 0; // not running
    FanMode = hvac.getFan();
    if( bFan = hvac.getFanRunning() )
    {
      idx = 1; // running
      if(FanMode == FM_On)
        idx = 2; // on and running
    }

    const char *pFan[] = {"/fanOff.png", "/fanOn.png", "/fanAutoOn.png",};
    loadImage((char*)pFan[idx], m_btn[Btn_Fan].x, m_btn[Btn_Fan].y);
  }

  if(nMode != hvac.getSetMode())
  {
    nMode = hvac.getSetMode();

    const char *szModes[] = {"/off.png", "/cool.png", "/heat.png", "/auto.png", "/auto.png"};
    loadImage((char*)szModes[nMode], m_btn[Btn_Mode].x, m_btn[Btn_Mode].y);

    hvac.m_bLink = true;
    m_adjustMode = (nMode == Mode_Heat) ? 2:0; // set adjust to selected heat/cool
    updateAdjMode(false);
  }

  if(heatMode != hvac.getHeatMode())
  {
    heatMode = hvac.getHeatMode();
    const char *szHeatModes[] = {"/hp.png", "/ng.png", "/hauto.png"};
    loadImage((char*)szHeatModes[heatMode], m_btn[Btn_HeatMode].x, m_btn[Btn_HeatMode].y);
  }
}

void Display::buttonRepeat()
{
  int8_t m = (m_adjustMode < 2) ? Mode_Cool : Mode_Heat; // lower 2 are cool
  int8_t hilo = (m_adjustMode ^ 1) & 1; // hi or low of set
  int16_t t = hvac.getSetTemp(m, hilo );

  t += (m_btnMode==1) ? 1:-1; // inc by 0.1
  hvac.setTemp(m, t, hilo);

  if(hvac.m_bLink) // adjust both high and low
  {
    t = hvac.getSetTemp(m, hilo^1 ) + ((m_btnMode==1) ? 1:-1); // adjust opposite hi/lo the same
    hvac.setTemp(m, t, hilo^1);
  }
  updateTemps();
}

void Display::loadImage(char *pName, uint16_t x, uint16_t y)
{
  xpos = x;
  ypos = y;
  int16_t rc = png.open(pName, pngOpen, pngClose, pngRead, pngSeek, pngDraw);
  if(rc == PNG_SUCCESS)
  {
    tft.startWrite();
//    Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\r\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
//    uint32_t dt = millis();
    rc = png.decode(NULL, 0);
    png.close();
    tft.endWrite();
    // How long did rendering take...
//    Serial.print(millis()-dt); Serial.println("ms");
  }
  else
  {
    Serial.print(pName);
    Serial.print(" loadImage error: ");
    Serial.println(rc);
  }
}

// time and dow on main page
void Display::displayTime()
{
  if(m_currPage) // not main page
    return;

  String sTime = String(month());
  sTime += "/";
  sTime += String(day());
  sTime += " ";
  
  sTime += String( hourFormat12() );
  if(hourFormat12() < 10)
    sTime = " " + sTime;
  sTime += ":";
  if(minute() < 10) sTime += "0";
  sTime += minute();
  sTime += ":";
  if(second() < 10) sTime += "0";
  sTime += second();
  sTime += " ";
  sTime += isPM() ? "PM":"AM";
  sTime += " ";

  tft.fillRect(m_btn[Btn_Time].x, m_btn[Btn_Time].y, m_btn[Btn_Time].w, m_btn[Btn_Time].h, rgb16(8,16,8));
  tft.setTextColor(rgb16(0, 31, 31), rgb16(8,16,8) );
  tft.setFreeFont(&digitaldreamFatNarrow_14ptFont);
  tft.drawString(sTime, m_btn[Btn_Time].x, m_btn[Btn_Time].y);

  tft.setFreeFont(&FreeSans12pt7b);
  tft.fillRect(m_btn[Btn_Dow].x, m_btn[Btn_Dow].y, m_btn[Btn_Dow].w, m_btn[Btn_Dow].h, rgb16(8,16,8));
  tft.drawString(_days_short[weekday()], m_btn[Btn_Dow].x, m_btn[Btn_Dow].y);
}

#define Fc_Left     37
#define Fc_Top      34
#define Fc_Width   286
#define Fc_Height   78

bool Display::drawForecast(bool bRef)
{
  int fcOff = 0;
  int fcCnt = 0;
  uint32_t tm = m_fc.Date;

  if(m_fc.Date == 0) // no data yet
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return false;
  }

  for(fcCnt = 0; fcCnt < FC_CNT && m_fc.Data[fcCnt] != -127; fcCnt++) // get current time in forecast and valid count
  {
    if( tm < now() )
    {
      fcOff = fcCnt;
      tm += m_fc.Freq;
    }
  }

  if(fcCnt >= FC_CNT || m_fc.Data[fcOff] == -127 ) // outdated
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return false;
  }

  if(bRef)
  {
    int rng = fcCnt;
    if(rng > ee.fcRange) rng = ee.fcRange;

    // Update min/max
    int8_t tmin = m_fc.Data[fcOff];
    int8_t tmax = m_fc.Data[fcOff];

    // Get min/max of current forecast
    for(int i = fcOff + 1; i < fcOff+rng && i < FC_CNT; i++)
    {
      int8_t t = m_fc.Data[i];
      if(tmin > t) tmin = t;
      if(tmax < t) tmax = t;
    }

    if(tmin == tmax) tmax++;   // div by 0 check

    hvac.m_outMin = tmin;
    hvac.m_outMax = tmax;
  }
  if(m_currPage) // on different page
    return true;
  displayOutTemp(); // update temp for HVAC

  tft.fillRect(Fc_Left, Fc_Top, Fc_Width, Fc_Height - 1, 0x0000); // clear graph area

    // Update min/max
    int8_t tmin = m_fc.Data[0];
    int8_t tmax = m_fc.Data[0];

    int rng = fcCnt;
    if(rng > ee.fcDisplay) rng = ee.fcDisplay; // shorten to user display range

    // Get min/max of current forecast
    for(int i = fcOff; i < fcOff+rng && i < FC_CNT; i++)
    {
      int8_t t = m_fc.Data[i];
      if(tmin > t) tmin = t;
      if(tmax < t) tmax = t;
    }

    if(tmin == tmax) tmax++;   // div by 0 check

  int16_t y = Fc_Top+1;
  int16_t incy = (Fc_Height-10) / 3;
  int16_t dec = (tmax - tmin)/3;
  int16_t t = tmax;
  int16_t x;

  // temp scale
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor( rgb16(0, 31, 31), rgb16(8,16,8));
  for(int i = 0; i <= 3; i++)
  {
    tft.drawNumber(t, 6, y + 4);
    y += incy;
    t -= dec;
  }

  int hrs = rng * m_fc.Freq / 3600; // normally 180ish hours
  int day_x = 0;
  if((tmax-tmin) == 0 || hrs <= 0) // divide by 0
    return true;

  int y2 = Fc_Top+Fc_Height - 1 - (m_fc.Data[fcOff] - tmin) * (Fc_Height-2) / (tmax-tmin);
  int x2 = Fc_Left;
  int hOld = 0;
  int day = weekday()-1;              // current day

  tft.setTextColor(rgb16(0, 63, 31), 0);

  tft.setTextDatum(TC_DATUM); // center day on noon

  int h = 0;

  for(int i = fcOff; i < fcOff+rng && m_fc.Data[i] != -127; i++) // should be 41 data points (close to 300ms)
  {
    int y1 = Fc_Top+Fc_Height - 1 - (m_fc.Data[i] - tmin) * (Fc_Height-2) / (tmax-tmin);
    int x1 = Fc_Left + h * (Fc_Width-1) / hrs;

    tft.drawLine(x2, y2, x1, y1, rgb16(31, 0, 0) ); // red

    int h24 = h % 24;
    if(hOld > h24 ) // new day (draw line)
    {
      tft.drawLine(x1, Fc_Top+1, x1, Fc_Top+Fc_Height-2, rgb16(20, 41, 20) ); // (light gray)
      if(++day > 6) day = 0;
    }
    if( hOld < 12 && h24 >= 12) // noon (dark line)
    {
      tft.drawLine(x1, Fc_Top, x1, Fc_Top+Fc_Height, rgb16(12, 25, 12) ); // gray
      if(x1 - 16 > Fc_Left) // skip 1st day if too far left
      {
        tft.drawString( _days_short[day+1], day_x = x1, Fc_Top+Fc_Height); // cyan
      }
    }
    hOld = h24;
    x2 = x1;
    y2 = y1;
    h += m_fc.Freq / 3600;
  }
  day_x += 20;
  if(day_x < Fc_Left+Fc_Width - 20 )  // last partial day
    tft.drawString(_days_short[day+1], day_x, Fc_Top+Fc_Height ); // cyan
  tft.setFreeFont(&FreeSans12pt7b); // set font back to normal
  tft.setTextDatum(TL_DATUM);
  return true;
}

// get value at current minute between hours
int Display::tween(int8_t t1, int8_t t2, int m, int r)
{
  if(r == 0) r = 1; // div by zero check
  float t = (float)(t2 - t1) * (m * 100 / r) / 100;
  return (int)((t + (float)t1) * 10);
}

void Display::Note(char *cNote)
{
  screen(true);
  tft.fillRect(m_btn[Btn_Note].x, m_btn[Btn_Note].y, m_btn[Btn_Note].w, m_btn[Btn_Note].h, rgb16(0,3,4));
  tft.setTextColor(rgb16(31, 5, 10), rgb16(0,3,4));
  tft.setFreeFont(&FreeSans12pt7b);
  tft.drawString(cNote, m_btn[Btn_Note].x+2, m_btn[Btn_Note].y+6);
  jsonString js("alert");
  js.Var("text", cNote);
  WsSend(js.Close());
}

// update the notification text box
void Display::updateNotification(bool bRef)
{
  static uint8_t note_last = Note_None; // Needs a clear after startup
  if(!bRef && note_last == hvac.m_notif) // nothing changed
    return;
  note_last = hvac.m_notif;

  String s = "";
  switch(hvac.m_notif)
  {
    case Note_None:
      break;
    case Note_CycleLimit:
      s = "Cycle Limit";
      break;
    case Note_Filter:
      s = "Replace Filter";
      break;
    case Note_Forecast:
      s = "Forecast Error"; // max chars 14 with this font
      break;
    case Note_Network:
      s = "Network Error";
      break;
    case Note_Connecting:
      s = "Connecting";
      break;
    case Note_Connected:
      s = "Connected";
      break;
    case Note_RemoteOff:
      s = "Remote Off";
      break;
    case Note_RemoteOn:
      s = "Remote On";
      break;
    case Note_EspTouch:
      s = "Use EspTouch App";
      break;
  }
  tft.fillRect(m_btn[Btn_Note].x, m_btn[Btn_Note].y, m_btn[Btn_Note].w, m_btn[Btn_Note].h, rgb16(0,3,4));
  tft.setTextColor(rgb16(31, 5, 10), rgb16(0,3,4));
  tft.setFreeFont(&FreeSans12pt7b);
  tft.drawString(s, m_btn[Btn_Note].x+2, m_btn[Btn_Note].y+6);
  if(s != "")
  {
    if(bRef == false) // refresh shouldn't be resent
    {
      jsonString js("alert");
      js.Var("text", s);
      WsSend(js.Close());
    }
    if(bRef == false || hvac.m_notif >= Note_Network) // once / repeats if important
    {
      mus.add(3500, 180);
      mus.add(2500, 90); // notification sound
      mus.add(3500, 180);
    }
  }
}

// smooth adjust brigthness (0-255)
void Display::dimmer()
{
  static uint8_t bright;
  
  if(bright == m_brightness)
    return;
  if(m_brightness > bright)
    bright ++;
  else
    bright --;

  analogWrite(TFT_BL, bright);
}

// things to update on page change to thermostat
void Display::refreshAll()
{
  updateRunIndicator(true);
  drawForecast(true);
  updateNotification(true);
  updateAdjMode(true);
  updateModes(true);

  loadImage("/up.png", m_btn[Btn_Up].x, m_btn[Btn_Up].y);
  loadImage("/dn.png", m_btn[Btn_Dn].x, m_btn[Btn_Dn].y);
}

void Display::updateAdjMode(bool bRef)  // current adjust indicator of the 4 temp settings
{
  static uint8_t am = 0;
  static bool bl = false;

  if(m_currPage || (bRef == false && am == m_adjustMode && bl == hvac.m_bLink) )
    return;

  loadImage("/ledoff.png", m_btn[Btn_IndCH+am].x, m_btn[Btn_IndCH+am].y);
  loadImage("/ledoff.png", m_btn[Btn_IndCH + (am^1)].x, m_btn[Btn_IndCH + (am^1)].y);

  am = m_adjustMode;
  loadImage("/ledon.png", m_btn[Btn_IndCH+am].x, m_btn[Btn_IndCH+am].y);
  if(hvac.m_bLink)
    loadImage("/ledon.png", m_btn[Btn_IndCH + (am^1)].x, m_btn[Btn_IndCH + (am^1)].y);

  bl = hvac.m_bLink;
}

void Display::updateRSSI()
{
  static uint8_t seccnt = 2;
  static int16_t rssiT;
#define RSSI_CNT 8
  static int16_t rssi[RSSI_CNT];
  static uint8_t rssiIdx = 0;

  if(m_currPage) // must be page 0
  {
    rssiT = 0; // cause a refresh later
    seccnt = 1;
    return;
  }
  if(--seccnt)
    return;
  seccnt = 3;     // every 3 seconds

  rssi[rssiIdx] = WiFi.RSSI();
  if(++rssiIdx >= RSSI_CNT) rssiIdx = 0;

  int16_t rssiAvg = 0;
  for(int i = 0; i < RSSI_CNT; i++)
    rssiAvg += rssi[i];

  rssiAvg /= RSSI_CNT;
  if(rssiAvg == rssiT)
    return;

//  nex.itemText(22, String(rssiT = rssiAvg) + "dB");

  int sigStrength = 127 + rssiT;
  int wh = 24; // width and height
  int x = 220; // X/Y position
  int y = 236;
  int sect = 127 / 5; //
  int dist = wh  / 5; // distance between blocks

  y += wh;

  for (int i = 1; i < 6; i++)
  {
    tft.fillRect( x + i*dist, y - i*dist, dist-2, i*dist, (sigStrength > i * sect) ? rgb16(0, 63,31) : rgb16(5, 10, 5) );
  }
}

void Display::updateRunIndicator(bool bForce) // run and fan running
{
  static bool bOn = false; // blinker
  static bool bCurrent = false; // run indicator
  static bool bHeat = false; // red/blue
  static bool bHumid = false; // next to rH

  if(bForce)
  {
    bOn = false;
    bCurrent = false;
    bHeat = false;
    bHumid = false;
  }

  if(m_currPage)
    return;

  if(hvac.getState()) // running
  {
    if(bHeat != (hvac.getState() > State_Cool) ? true:false)
      bHeat = (hvac.getState() > State_Cool) ? true:false;
    if(hvac.m_bRemoteStream)
      bOn = !bOn; // blink indicator if remote temp
    else bOn = true; // just on
  }
  else bOn = false;

  if(bCurrent != bOn)
  {
    if(bOn)
       loadImage((char *)(bHeat ? "/ledred.png" : "/ledblue.png"), m_btn[Btn_IndR].x, m_btn[Btn_IndR].y);
    else
       loadImage("/ledoff.png", m_btn[Btn_IndR].x, m_btn[Btn_IndR].y);
    bCurrent = bOn;
  }

  if(bHumid != hvac.getHumidifierRunning())
     loadImage((char *)((bHumid = hvac.getHumidifierRunning()) ? "/ledon.png" : "/ledoff.png"), m_btn[Btn_IndH].x, m_btn[Btn_IndH].y);
}

// Analog clock
void Display::updateClock()
{
  if(m_currPage != Page_Clock)
    return;

  const float x = DISPLAY_WIDTH/2-1; // center
  const float y = DISPLAY_HEIGHT/2-1;
  const uint16_t bgColor = rgb16(9,18,9);
  float x1,y1, x2,y2, x3,y3;

  tft.fillCircle(x, y, 92, bgColor ); // no room for transparency

  float a = (hour() + (minute() * 0.00833) ) * 30;
  cspoint(x1, y1, x, y, a, 64);
  a = (hour() + (minute() * 0.00833)+2 ) * 30;
  cspoint(x2, y2, x, y, a, 10);
  a = (hour() + (minute() * 0.00833)-2 ) * 30;
  cspoint(x3, y3, x, y, a, 10);
  tft.fillTriangle(x1,y1, x2,y2, x3, y3, rgb16(2, 4, 2) ); // hour hand

  cspoint(x1, y1, x, y, minute() * 6, 88);
  cspoint(x2, y2, x, y, (minute()+5) * 6, 10);
  cspoint(x3, y3, x, y, (minute()-5) * 6, 10);
  tft.fillTriangle(x1,y1, x2,y2, x3, y3, rgb16(0, 0, 0) ); // minute hand

  tft.fillCircle(x, y, 12, 0 ); // center cap

  cspoint(x2, y2, x, y, second() * 6, 91);
  cspoint(x3, y3, x, y, (second()+30) * 6, 24);
  tft.drawWideLine(x3, y3, x2, y2, 2.5, rgb16(31, 0, 0), bgColor ); // second hand

  tft.setTextColor(rgb16(0, 31, 31) );
  tft.setFreeFont(&digitaldreamFatNarrow_14ptFont);

  tft.drawString((char *)_days_short[weekday()], 16, 30);

  String sTime = _months_short[month()];
  sTime += " ";
  sTime += String(day());
  tft.drawString(sTime, 16, 80);

  sTime = String(year());
  tft.drawString(sTime, 16, 115);

  sTime = (hourFormat12() < 10) ? " ":"";
  sTime += String( hourFormat12() );
  sTime += ":";
  if(minute() < 10) sTime += "0";
  sTime += minute();
  sTime += ":";
  if(second() < 10) sTime += "0";
  sTime += second();
  sTime += " ";
  sTime += isPM() ? "PM":"AM";

  tft.fillRect(18, DISPLAY_HEIGHT-40, 118, 25, rgb16(1,8,4));
  tft.drawString(sTime, 16, DISPLAY_HEIGHT-37);
}

void Display::cspoint(float &x2, float &y2, float x, float y, float angle, float size)
{
  float ang =  M_PI * (180-angle) / 180;
  x2 = x + size * sin(ang);
  y2 = y + size * cos(ang);  
}

// Lines demo
void Display::Lines()
{
  if(m_currPage != Page_Blank)
    return;

#define LINES 50
  static Line line[LINES], delta;
  uint16_t color;
  static uint8_t r=0, g=0, b=0;
  static int8_t cnt = 0;
  static bool bInit = false;

  if(!bInit)
  {
    memset(&line, 10, sizeof(line));
    memset(&delta, 1, sizeof(delta));
    bInit = true;
  }
  // Erase oldest line
  tft.drawLine(line[LINES - 1].x1, line[LINES - 1].y1, line[LINES - 1].x2, line[LINES - 1].y2, 0);

  // FIFO the lines
  for(int i = LINES - 2; i >= 0; i--)
    line[i+1] = line[i];

  if(--cnt <= 0)
  {
    cnt = 5; // every 5 runs
    delta.x1 = constrain(delta.x1 + random(-1,2), -4, 4); // random direction delta
    delta.x2 = constrain(delta.x2 + random(-1,2), -4, 4);
    delta.y1 = constrain(delta.y1 + random(-1,2), -4, 4);
    delta.y2 = constrain(delta.y2 + random(-1,2), -4, 4);
  }
  line[0].x1 += delta.x1; // add delta to positions
  line[0].y1 += delta.y1;
  line[0].x2 += delta.x2;
  line[0].y2 += delta.y2;

  line[0].x1 = constrain(line[0].x1, 0, DISPLAY_WIDTH); // keep it on the screen
  line[0].x2 = constrain(line[0].x2, 0, DISPLAY_WIDTH);
  line[0].y1 = constrain(line[0].y1, 0, DISPLAY_HEIGHT);
  line[0].y2 = constrain(line[0].y2, 0, DISPLAY_HEIGHT);

  b += random(-2, 3); // random RGB shift
  g += random(-3, 4); // green is 6 bits
  r += random(-2, 3);

  color = rgb(r,g,b);
  
  tft.drawLine(line[0].x1, line[0].y1, line[0].x2, line[0].y2, color); // draw the new line

  if(line[0].x1 == 0 && delta.x1 < 0) delta.x1 = -delta.x1; // bounce off edges
  if(line[0].x2 == 0 && delta.x2 < 0) delta.x2 = -delta.x2;
  if(line[0].y1 == 0 && delta.y1 < 0) delta.y1 = -delta.y1;
  if(line[0].y2 == 0 && delta.y2 < 0) delta.y2 = -delta.y2;
  if(line[0].x1 == DISPLAY_WIDTH && delta.x1 > 0) delta.x1 = -delta.x1;
  if(line[0].x2 == DISPLAY_WIDTH && delta.x2 > 0) delta.x2 = -delta.x2;
  if(line[0].y1 == DISPLAY_HEIGHT && delta.y1 > 0) delta.y1 = -delta.y1;
  if(line[0].y2 == DISPLAY_HEIGHT && delta.y2 > 0) delta.y2 = -delta.y2;
}

void Display::addGraphPoints()
{
  if( hvac.m_inTemp == 0 || hvac.m_targetTemp == 0)
    return;
  m_temp_counter = 5*60;         // update every 5 minutes
  gPoint *p = &m_points[m_pointsIdx];

  uint32_t pdate = now() - ((ee.tz+hvac.m_DST)*3600);
  if(m_lastPDate == 0)
    m_lastPDate = pdate;
  p->bits.tmdiff = pdate - m_lastPDate;
  m_lastPDate = pdate;
  p->t.inTemp = hvac.m_inTemp;
  if(hvac.m_modeShadow == Mode_Heat)
    p->t.target = hvac.m_targetTemp;
  else // cool
    p->t.target = hvac.m_targetTemp - ee.cycleThresh[0];
  p->t.outTemp = hvac.m_outTemp;
  p->bits.rh = hvac.m_rh;
  p->bits.fan = hvac.getFanRunning();
  p->bits.state = hvac.getState(); 
  p->sens0 = hvac.m_Sensor[0].temp - p->t.inTemp;
  p->sens1 = hvac.m_Sensor[1].temp - p->t.inTemp;
  p->sens2 = hvac.m_Sensor[2].temp - p->t.inTemp;
  p->sens3 = hvac.m_Sensor[3].temp - p->t.inTemp;
  p->bits.sens4 = hvac.m_Sensor[4].temp - p->t.inTemp;
  if(++m_pointsIdx >= GPTS)
    m_pointsIdx = 0;
  m_points[m_pointsIdx].t.u = 0; // mark as invalid data/end
}

// Draw the last 25 hours
void Display::fillGraph()
{
  m_tempHigh = (ee.b.bCelcius ? 370:990);
  m_tempLow = (ee.b.bCelcius ? 240:750);

  int tempMin = minPointVal(0);
  if(tempMin < (ee.b.bCelcius ? 240:750) )
  {
    m_tempHigh = (ee.b.bCelcius ? 320:890);
    m_tempLow = (ee.b.bCelcius ? 180:650);
  }
  int tmpInc = (m_tempHigh - m_tempLow) / 4;
  int temp = m_tempLow;
  int y = DISPLAY_HEIGHT-4;
  tft.setTextColor(rgb16(0, 63, 31), rgb16(8,16,8));
  tft.setFreeFont(&FreeSans9pt7b);
  for(int i = 0; i < 5; i++) // draw temp range
  {
    tft.drawString( String(temp / 10), DISPLAY_WIDTH-18, y );
    if(i>0) tft.drawLine( 10, y+8, DISPLAY_WIDTH-10, y+8, rgb16(10, 20, 10) );
    y -= (DISPLAY_HEIGHT - 20) /4;
    temp += tmpInc;
  }

  int x = DISPLAY_WIDTH - 10 - (minute() / 5); // center over even hour, 5 mins per pixel
  int h = hourFormat12();

  while(x > 12 * 6)
  {
    x -= 12 * 6; // left 6 hours
    h -= 6;
    if( h <= 0) h += 12;
    tft.drawLine(x, 10, x, DISPLAY_HEIGHT-10, rgb16(10, 20, 10) );
    String s = String(h);
    s += ":00";
    tft.drawString( s, x-4, 0); // draw hour above chart
  }
  mus.service();
  drawPoints(0, rgb16( 22, 40, 10) ); // target (draw behind the other stuff)
  drawPoints(1, rgb16( 22, 40, 10) ); // target threshold
  drawPointsTemp(); // off/cool/heat colors
  mus.service();
  drawPointsRh( rgb16(  0, 53,  0) ); // rh green
  tft.setFreeFont(&FreeSans12pt7b); // set font back to normal
}

void Display::drawPoints(int w, uint16_t color)
{
  int i = m_pointsIdx - 1;
  if(i < 0) i = GPTS-1;
  const int yOff = DISPLAY_HEIGHT-10;
  int y, y2;

  for(int x = DISPLAY_WIDTH-11, x2 = DISPLAY_WIDTH-10; x >= 10; x--)
  {
    if(m_points[i].t.u == 0)
      return;
    switch(w)
    {
      case 0: y = m_points[i].t.target + ee.cycleThresh[(hvac.m_modeShadow == Mode_Heat) ? 1:0]; break;
      case 1: y = m_points[i].t.target; break;
    }

    y = (constrain(y, m_tempLow, m_tempHigh) - m_tempLow) * (DISPLAY_HEIGHT-20) / (m_tempHigh-m_tempLow); // scale to 0~300

    if(y != y2)
    {
      if(x != DISPLAY_WIDTH-11)
        tft.drawLine(x, yOff - y, x2, yOff - y2, color);
      y2 = y;
      x2 = x;
    }
    if(--i < 0)
      i = GPTS-1;
  }
}

void Display::drawPointsRh(uint16_t color)
{
  int i = m_pointsIdx - 1;
  if(i < 0) i = GPTS-1;
  const int yOff = DISPLAY_HEIGHT-10;
  int y, y2 = m_points[i].bits.rh;

  if(m_points[i].t.u == 0)
    return; // not enough data

  y2 = y2 * 300 / 1000; // 0.0~100.0 to 0~300

  for(int x = DISPLAY_WIDTH-11, x2 = DISPLAY_WIDTH-10; x >= 10; x--)
  {
    if(--i < 0)
      i = GPTS-1;

    y = m_points[i].bits.rh;
    if(m_points[i].t.u == 0)
      return;
    y = y * 300 / 1000;

    if(y != y2)
    {
      tft.drawLine(x, yOff - y, x2, yOff - y2, color);
      y2 = y;
      x2 = x;
    }
  }
}

void Display::drawPointsTemp()
{
  const int yOff = DISPLAY_WIDTH-10;
  int y, y2;
  int x2 = DISPLAY_WIDTH-10;
  int i = m_pointsIdx-1;

  if(i < 0) i = GPTS-1;

  for(int x = DISPLAY_WIDTH-11; x >= 10; x--)
  {
    if(m_points[i].t.u == 0)
      break; // end
    y = (constrain(m_points[i].t.inTemp, m_tempLow, m_tempHigh) - m_tempLow) * (DISPLAY_HEIGHT-20) / (m_tempHigh-m_tempLow);
    if(y != y2)
    {
      if(x != DISPLAY_WIDTH-11)
        tft.drawLine(x2, yOff - y2, x, yOff - y, stateColor(m_points[i].bits) );
      y2 = y;
      x2 = x;
    }
    if(--i < 0)
      i = GPTS-1;
  }
}

uint16_t Display::stateColor(gflags v) // return a color based on run state
{
  uint16_t color;

  if(v.fan) // fan
    color = rgb16(0, 50, 0); // green

  switch(v.state)
  {
    case State_Off: // off
      color = rgb16(20, 40, 20); // gray
      break;
    case State_Cool: // cool
      color = rgb16(0, 0, 31); // blue
      break;
    case State_HP:
    case State_NG:
      color = rgb16(31, 0, 0); // red
      break;
  }
  return color;
}

bool Display::getGrapthPoints(gPoint *pts, int n)
{
  if(n < 0 || n > GPTS-1) // convert 0-(GPTS-1) to reverse index circular buffer
    return false;
  int idx = m_pointsIdx - 1 - n; // 0 = last entry
  if(idx < 0) idx += GPTS;
  if(m_points[idx].t.u == 0) // invalid data
    return false;
  memcpy(pts, &m_points[idx], sizeof(gPoint));
  return true;
}

int Display::minPointVal(int n)
{
  int minv = 10000;
  int maxv = -1000;
  int i = m_pointsIdx - 1; // 0 = last entry
  if(i < 0) i = GPTS-1;

  while(i != m_pointsIdx)
  {
    int val;
    if(m_points[i].t.u == 0)
      break;
    switch(n)
    {
      default: val = m_points[i].t.inTemp; break;
      case 1: val = m_points[i].t.target; break;
      case 2: val = m_points[i].t.target + ee.cycleThresh[(hvac.m_modeShadow == Mode_Heat) ? 1:0]; break;
      case 3: val = m_points[i].bits.rh; break;
      case 4: val = m_points[i].t.outTemp; break;
    }
    if(minv > val) 
      minv = val;
    if(maxv < val) 
      maxv = val;
    if(--i < 0) i = GPTS-1;
  }
  m_tempMax = maxv;
  return minv;
}
