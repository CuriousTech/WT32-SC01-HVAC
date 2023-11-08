#include "HVAC.h"
#include "display.h"
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include "jsonstring.h"
#include <TimeLib.h>
#include <SPIFFS.h>
#define FileSys SPIFFS   // SPIFFS is needed for AsyncWebServer
#include "eeMem.h"
#include "screensavers.h"
#include "music.h"
Music mus;
#include <TFT_eSPI.h> // TFT_espi library
TFT_eSPI tft = TFT_eSPI();
#include "digitalFont.h"

// FT6206, FT6336U - touchscreen library                  // modified -> Wire.begin(18, 19)
#include "Adafruit_FT6206.h"
Adafruit_FT6206 ts = Adafruit_FT6206();

ScreenSavers ss;

extern HVAC hvac;
extern void WsSend(String s);

PNG png;

void pngDraw(PNGDRAW *pDraw)
{
  ImageCtrl *pPos = (ImageCtrl *)pDraw->pUser;

  if(pPos->srcY)
  {
    if(pDraw->y < pPos->srcY) // skip lines above
      return;
    if(pPos->srcY + pPos->h >= pDraw->y) // skip lines below
      return;
  }

  // Uses shared bufffer (1024 bytes / 512 pixels max)
  png.getLineAsRGB565(pDraw, (uint16_t *)ss.m_buffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);

  uint16_t *pBuf = (uint16_t *)ss.m_buffer;
  uint16_t w = pDraw->iWidth;
  if(pPos->w) w = pPos->w; // crop to desired width (todo: should maybe to some checking)

  uint16_t y = pPos->y + pDraw->y;
  if(pPos->srcY) // offset lib incremented y pos to srcY offset
    y -= pPos->srcY;

  tft.pushImage(pPos->x, y, w, 1, pBuf + pPos->srcX);
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
  for(int i = 0; i < FC_CNT; i++)
   m_fc.Data[i].temp = -127;
  m_fc.Date = 0;

  pinMode(39, INPUT); // touch int

  setBrightness(0, 0);                // black out while display is noise 
  ts.begin(40);                       // adafruit touch
  tft.init();                         // TFT_eSPI
  tft.setRotation(1);                 // set desired rotation
  tft.setTextDatum(TL_DATUM);

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
    setBrightness(0, m_maxBrightness);
    loadImage("/bg.png", 0, 0);
    refreshAll();
  }
  else switch(m_currPage)
  {
    case Page_Forecast:
      m_currPage = Page_ScreenSaver;
      setBrightness(0, 180);
      ss.select( random(0, SS_Count) );
      break;
    case Page_ScreenSaver:
      historyPage();
      break;
    default: // from thermostat
      forecastPage();
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
  mus.service(); // sound handler

  if(m_currPage == Page_ScreenSaver)
    ss.run();

  static uint32_t lastms;
  static uint32_t touchms;

  if (digitalRead(39) == LOW)// touch I/O causes SHT40 errors. SHT40 really not recommended. Use AM2320 or AM2322.
  {
    touchms = millis();
    if(millis() - lastms < 100) // filter the pulses, also repeat speed
      return;
    lastms = millis();

    TS_Point p = ts.getPoint();
    uint16_t y = 320 - p.x;     // adjust touch point for rotation = 1
    uint16_t x = p.y;

    if(bPress == false) // only need 1 touch
    {
      if(m_currPage) // not on thermostat
      {
        screen(true); // switch back to thermostat screen
      }
      else
      {
        currBtn = -1;
        for(uint8_t i = 0; i < Btn_Count; i++)
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
    else if(m_btnMode) // up/dn being held
    {
      if(--m_btnDelay <= 0)
      {
        buttonRepeat();
        mus.add(7000, 15);
        m_btnDelay = 2; // repeat speed
      }
    }
    else if(m_lockDelay) // lock button held
    {
      if(--m_lockDelay == 0)
      {
        mus.add(3000, 25);
        ee.b.bLock = 0;
      }
    }
    
    bPress = true;
  }
  else if(bPress) // release
  {
    if(millis() - touchms < 30) // the touch interrupt pulses ~10ms
      return;

    bPress = false;
    m_btnMode = 0; // Up/Dn release
    currBtn = -1;
  }
  else // idle
  {
  }
}

void Display::buttonCmd(uint8_t btn)
{
  if( m_backlightTimer == 0) // if dimmed, undim and ignore click
  {
    m_backlightTimer = DISPLAY_TIMEOUT;
    setBrightness(m_brightness, m_maxBrightness);
    return;
  }

  m_backlightTimer = DISPLAY_TIMEOUT; // update the auto backlight timer

  mus.add(6000, 20);

  switch(btn)
  {
    case Btn_SetTempH:
      m_bLink = !m_bLink;
      m_adjustMode = 0;
      updateTemps();
      break;
    case Btn_SetTempL:
      m_bLink = !m_bLink;
      m_adjustMode = 1;
      updateTemps();
      break;

    case Btn_Up: // Up button
      m_btnMode = 1;
      buttonRepeat();
      m_btnDelay = 7; // first repeat
      break;
    case Btn_Dn: // Down button
      m_btnMode = 2;
      buttonRepeat();
      m_btnDelay = 7;
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
    case Btn_Humid:
      if(ee.b.bLock) break;
      hvac.setHumidifierMode( hvac.getHumidifierMode() + 1 );
      updateModes(false); // faster feedback
      break;
    case Btn_Note:
      if(ee.b.bLock) break;
      hvac.m_notif = Note_None;
      break;
    case Btn_Time: // time
      m_currPage = Page_ScreenSaver;
      setBrightness(0, 150);
      ss.select( SS_Clock );
      break;
    case Btn_TargetTemp:
      if(ee.b.bLock) break;
      hvac.enableRemote();
      break;
    case Btn_InTemp: // in
    case Btn_Rh: // rh
      historyPage();
      break;
    case Btn_OutTemp:
      forecastPage();
      break;
    case Btn_Lock: // lock
      if(!ee.b.bLock)
        ee.b.bLock = 1;
      else
        m_lockDelay = 30; // press and hold ~5 seconds
      break;
  }
}

// called each second
void Display::oneSec()
{
  drawTime();    // time update every second
  updateModes(false);    // mode, heat mode, fan mode
  updateTemps();    // 
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
    drawOutTemp();
    addGraphPoints();
    lastState = hvac.getState();
    lastFan = hvac.getFanRunning();
  }

  if(m_currPage == Page_Thermostat && m_bFcstUpdated)
  {
    m_bFcstUpdated = false;
    drawOutTemp();
  }
}

void Display::drawOutTemp()
{
  if(m_fc.Date == 0) // not read yet or time not set
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return;
  }

  int8_t fcOff = 0;
  int8_t fcCnt = 0;
  uint32_t tm = m_fc.Date;

  for(fcCnt = 0; fcCnt < FC_CNT && m_fc.Data[fcCnt].temp != -127; fcCnt++) // get current time in forecast and valid count
  {
    if( tm < now() )
    {
      fcOff = fcCnt;
      tm += m_fc.Freq;
    }
  }

  if(fcCnt >= FC_CNT || m_fc.Data[fcOff].temp == -127 ) // outdated
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return;
  }

  int rng = fcCnt;
  if(rng > ee.fcRange) rng = ee.fcRange;

  // Update min/max
  int16_t tmin = m_fc.Data[0].temp;
  int16_t tmax = m_fc.Data[0].temp;

  // Get min/max of current forecast
  for(int i = 1; i < rng && i < FC_CNT; i++)
  {
    int16_t t = m_fc.Data[i].temp;
    if(tmin > t) tmin = t;
    if(tmax < t) tmax = t;
  }

  if(tmin == tmax) tmax++;   // div by 0 check

  hvac.m_outMin = tmin; // set for hvac
  hvac.m_outMax = tmax;

  int iH = 0;
  int m = minute();
  uint32_t tmNow = now() - ((ee.tz+hvac.m_DST)*3600);
  uint32_t fcTm = m_fc.Date;

  if( tmNow >= fcTm)
  {
    for(iH = 0; tmNow > fcTm && m_fc.Data[iH].temp != -127 && iH < FC_CNT - 1; iH++)
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
  int outTempReal = tween(m_fc.Data[iH].temp, m_fc.Data[iH+1].temp, m, r);
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

  outTempShift = tween(m_fc.Data[iH].temp, m_fc.Data[iH+1].temp, m, r);

  tft.setTextColor( rgb16(0, 63, 31), TFT_BLACK );
  tft.setFreeFont(&digitaldreamSkew_28ptFont);
  // Summer/winter curve.  Summer is delayed 3 hours
  hvac.updateOutdoorTemp( outTempShift );

  if(m_currPage == Page_Thermostat)
  {
    tft.drawFloat((float)outTempReal/10, 1, m_btn[Btn_OutTemp].x, m_btn[Btn_OutTemp].y );

    static bool bInit = false; // make first time display update fast
    if(!bInit)
    {
      int16_t t = hvac.getSetTemp(Mode_Off, 0 );
      hvac.setTemp(Mode_Off, t, 0);
      bInit = true;
    }
  }
}

void Display::updateTemps(void)
{
  static uint16_t last[7];  // only draw changes
  if(m_currPage)
  {
    memset(last, 0, sizeof(last));
    return;
  }

  tft.setTextColor( rgb16(0, 63, 31), TFT_BLACK );

  tft.setFreeFont(&digitaldreamSkew_48ptFont);
  if(last[0] != hvac.m_inTemp)
    tft.drawFloat((float)(last[0] = hvac.m_inTemp)/10, 1, m_btn[Btn_InTemp].x, m_btn[Btn_InTemp].y );
  tft.setFreeFont(&digitaldreamSkew_28ptFont);
  if(last[1] != hvac.m_targetTemp)
    tft.drawFloat((float)(last[1] = hvac.m_targetTemp)/10, 1, m_btn[Btn_TargetTemp].x, m_btn[Btn_TargetTemp].y );

  if(last[2] != hvac.m_rh)
  {
    tft.drawFloat((float)(last[2] = hvac.m_rh)/10, 1, m_btn[Btn_Rh].x, m_btn[Btn_Rh].y );
    tft.setFreeFont(&FreeSans12pt7b);//&digitaldreamFatNarrow_14ptFont);
    tft.drawString("%", m_btn[Btn_Rh].x + 101, m_btn[Btn_Rh].y );
  }

  tft.setFreeFont(&digitaldreamSkew_28ptFont);

  uint8_t nMode = hvac.getSetMode();

  if(nMode == Mode_Auto)
    nMode = (hvac.getAutoMode() == Mode_Cool) ? Mode_Cool : Mode_Heat;

  static const uint16_t colors[] = {rgb16(21, 44, 21), rgb16(0, 4, 31), rgb16(31, 13, 10) };
  tft.setTextColor( colors[nMode], TFT_BLACK );

  if( last[5] != nMode || last[6] != ((m_adjustMode<<1) | m_bLink) ) // force temp redraw
  {
    last[3] = last[4] = 0;
    last[5] = nMode;
  }

  switch(nMode)
  {
    case Mode_Off:
    case Mode_Cool:
      if(last[3] != ee.coolTemp[1])
      {
        tft.drawFloat((float)(last[3] = ee.coolTemp[1])/10, 1, m_btn[Btn_SetTempH].x+1, m_btn[Btn_SetTempH].y+2 );
        last[6] = 10; // force outline refresh 
      }
      if(last[4] != ee.coolTemp[0])
      {
        tft.drawFloat((float)(last[4] = ee.coolTemp[0])/10, 1, m_btn[Btn_SetTempL].x+1, m_btn[Btn_SetTempL].y+2 );
        last[6] = 10;
      }
      break;
    case Mode_Heat:
      if(last[3] != ee.heatTemp[1])
      {
        tft.drawFloat((float)(last[3] = ee.heatTemp[1])/10, 1, m_btn[Btn_SetTempH].x+1, m_btn[Btn_SetTempH].y+2 );
        last[6] = 10;
      }
      if(last[4] != ee.heatTemp[0])
      {
        tft.drawFloat((float)(last[4] = ee.heatTemp[0])/10, 1, m_btn[Btn_SetTempL].x+1, m_btn[Btn_SetTempL].y+2 );
        last[6] = 10;
      }
      break;
  }

  if( last[6] != ((m_adjustMode<<1) | m_bLink) )
  {
    last[6] = (m_adjustMode<<1) | m_bLink;
    int8_t am = m_adjustMode;
    tft.drawRect(m_btn[Btn_SetTempH+am].x, m_btn[Btn_SetTempH+am].y, m_btn[Btn_SetTempH+am].w, m_btn[Btn_SetTempH+am].h, rgb16(0,31,0));
    tft.drawRect(m_btn[Btn_SetTempH+(am^1)].x, m_btn[Btn_SetTempH+(am^1)].y, m_btn[Btn_SetTempH+(am^1)].w, m_btn[Btn_SetTempH+(am^1)].h, (m_bLink) ? rgb16(0,31,0) : TFT_BLACK);
 
  }
}

void Display::updateModes(bool bForce) // update any displayed settings
{
  static bool bFan = true; // set these to something other than default to trigger them all
  static int8_t FanMode = 4;
  static uint8_t nMode = 10;
  static uint8_t nState = 3;
  static uint8_t heatMode = 10;
  static int8_t humidMode = 10;

  if(m_currPage || bForce)
  {
    nState = humidMode = FanMode = nMode = heatMode = 10;
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

  if(nMode != hvac.getSetMode() || nState != hvac.getState())
  {
    nMode = hvac.getSetMode();

    const char *szModes[] = {"/off.png", "/cool.png", "/heat.png", "/auto.png", "/auto.png"};
    loadImage((char*)szModes[nMode], m_btn[Btn_Mode].x, m_btn[Btn_Mode].y);

    m_bLink = true;
    m_adjustMode = 0;
    updateTemps();

    nState = hvac.getState();
    if(nState) // draw the ON indicator on the button
      loadImage("/btnled.png", m_btn[Btn_Mode].x + 1, m_btn[Btn_Mode].y);

  }

  if(heatMode != hvac.getHeatMode())
  {
    heatMode = hvac.getHeatMode();
    const char *szHeatModes[] = {"/hp.png", "/ng.png", "/hauto.png"};
    loadImage((char*)szHeatModes[heatMode], m_btn[Btn_HeatMode].x, m_btn[Btn_HeatMode].y);
  }

  if(humidMode != (hvac.getHumidifierMode() + hvac.getHumidifierRunning()) )
  {
    humidMode = (hvac.getHumidifierMode() + hvac.getHumidifierRunning());

    loadImage("/humid.png", m_btn[Btn_Humid].x, m_btn[Btn_Humid].y);

    const char *szHmode[] = {"", "F", "R", "1", "2", ""};
    tft.setFreeFont(&FreeSans9pt7b);
    tft.setTextColor( rgb16(0, 63, 31) );
    tft.drawString((char*)szHmode[hvac.getHumidifierMode()], m_btn[Btn_Humid].x + 27, m_btn[Btn_Humid].y + 28);

    if(hvac.getHumidifierRunning())
      loadImage("/btnled.png", m_btn[Btn_Humid].x + 1, m_btn[Btn_Humid].y);
  }

  loadImage( (char*) ( (ee.b.bLock) ? "/lock.png" : "/unlock.png" ), m_btn[Btn_Lock].x, m_btn[Btn_Lock].y);

}

void Display::buttonRepeat()
{
  int8_t m = hvac.getMode();
  if(m == Mode_Auto) m = hvac.getAutoMode();
  if(m == Mode_Off)
    return;

  int8_t hilo = (m_adjustMode ^ 1) & 1; // hi or low of set
  int16_t t = hvac.getSetTemp(m, hilo );

  t += (m_btnMode==1) ? 1:-1; // inc by 0.1
  hvac.setTemp(m, t, hilo);

  if(m_bLink) // adjust both high and low
  {
    t = hvac.getSetTemp(m, hilo^1 ) + ((m_btnMode==1) ? 1:-1); // adjust opposite hi/lo the same
    hvac.setTemp(m, t, hilo^1);
  }
  updateTemps();
}

void Display::loadImage(char *pName, uint16_t x, uint16_t y)
{
  loadImage(pName, x, y, 0, 0, 0, 0);
}

void Display::loadImage(char *pName, uint16_t x, uint16_t y, uint16_t srcX, uint16_t srcY, uint16_t w, uint16_t h)
{
  static ImageCtrl pos;
  pos.x = x;
  pos.y = y;
  pos.srcX = srcX;
  pos.srcY = srcY;
  pos.w = w;
  pos.h = h;

  int16_t rc = png.open(pName, pngOpen, pngClose, pngRead, pngSeek, pngDraw);

  if(rc == PNG_SUCCESS)
  {
    tft.startWrite();
//    Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\r\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
//    uint32_t dt = millis();
    rc = png.decode((void*)&pos, 0);
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
void Display::drawTime()
{
  static bool bRefresh = true;
  if(m_currPage) // not main page
  {
    bRefresh = true;
    return;
  }
  
  String sTime = String( hourFormat12() );
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

#define TIME_OFFSET 80
  tft.fillRect(m_btn[Btn_Time].x + TIME_OFFSET, m_btn[Btn_Time].y, m_btn[Btn_Time].w - TIME_OFFSET, m_btn[Btn_Time].h, rgb16(8,16,8));
  tft.setTextColor(rgb16(16,63,0), rgb16(8,16,8) );
  tft.setFreeFont(&FreeSans12pt7b);
  tft.drawString(sTime, m_btn[Btn_Time].x + TIME_OFFSET, m_btn[Btn_Time].y);

  if(bRefresh) // Cut down on flicker a bit
  {
    sTime = monthShortStr(month());
    sTime += " ";
    sTime += String(day());
    tft.fillRect(m_btn[Btn_Time].x, m_btn[Btn_Time].y, TIME_OFFSET - 20, m_btn[Btn_Time].h, rgb16(8,16,8));
    tft.drawString(sTime, m_btn[Btn_Time].x, m_btn[Btn_Time].y);

    tft.fillRect(m_btn[Btn_Dow].x, m_btn[Btn_Dow].y, m_btn[Btn_Dow].w, m_btn[Btn_Dow].h, rgb16(8,16,8));
    tft.drawString(dayShortStr(weekday()), m_btn[Btn_Dow].x, m_btn[Btn_Dow].y);
  }
  bRefresh = false;
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
  if(m_currPage)
    return;
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
    case Note_Sensor:
      s = "Sensor Falied";
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

// Set screen brightness(first level, second level)
void Display::setBrightness(uint8_t immediate, uint8_t deferred)
{
  m_bright = immediate;
  m_brightness = deferred;
  analogWrite(TFT_BL, m_bright);
}

// smooth adjust brigthness (0-255)
void Display::dimmer()
{
  if(m_bright == m_brightness)
    return;

  if(m_brightness > m_bright + 1)
    m_bright += 2;
  else if(m_brightness > m_bright)
    m_bright ++;
  else
    m_bright --;

  analogWrite(TFT_BL, m_bright);
}

// things to update on page change to thermostat
void Display::refreshAll()
{
  updateNotification(true);
  updateTemps();
  updateModes(true);
  drawOutTemp();
  loadImage("/up.png", m_btn[Btn_Up].x, m_btn[Btn_Up].y);
  loadImage("/dn.png", m_btn[Btn_Dn].x, m_btn[Btn_Dn].y);
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
  int wh = m_btn[Btn_RSSI].w; // width and height
  int x = m_btn[Btn_RSSI].x; // X/Y position
  int y = m_btn[Btn_RSSI].y;
  int sect = 127 / 5; //
  int dist = wh  / 5; // distance between blocks

  y += wh;

  for (int i = 1; i < 6; i++)
  {
    tft.fillRect( x + i*dist, y - i*dist, dist-2, i*dist, (sigStrength > i * sect) ? rgb16(0, 63,31) : rgb16(5, 10, 5) );
  }
}

#define FC_Left     37
#define FC_Top      34
#define FC_Width   430
#define FC_Height  143

void Display::forecastPage()
{
  if(m_fc.Date == 0) // no data yet
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return;
  }

  m_currPage = Page_Forecast;
  setBrightness(0, 150);
  loadImage("/bgForecast.png", 0, 0);

  int8_t fcOff = 0;
  int8_t fcDispOff = 0;
  int8_t fcCnt = 0;
  uint32_t tm = m_fc.Date;

  for(fcCnt = 0; fcCnt < FC_CNT && m_fc.Data[fcCnt].temp != -127; fcCnt++) // get current time in forecast and valid count
  {
    if( tm < now() )
    {
      fcOff = fcCnt;
      tm += m_fc.Freq;
    }
  }

  if(fcCnt >= FC_CNT || m_fc.Data[fcOff].temp == -127 ) // outdated
  {
    if(m_bUpdateFcstIdle)
      m_bUpdateFcst = true;
    return;
  }

  tft.fillRect(FC_Left, FC_Top, FC_Width, FC_Height - 1, TFT_BLACK); // clear graph area

  if(fcOff > 8) // more than a of day history
  {
    fcDispOff = fcOff - 8;
  }

  // Update min/max
  int16_t tmin = m_fc.Data[fcDispOff].temp;
  int16_t tmax = tmin;

  int hrng = fcCnt;
  if(hrng > fcDispOff + ee.fcDisplay)
    hrng = fcDispOff + ee.fcDisplay; // shorten to user display range

  // Get min/max of current forecast for display
  for(int i = fcDispOff + 1; i < hrng && i < FC_CNT; i++)
  {
    int16_t t = m_fc.Data[i].temp;
    if(tmin > t) tmin = t;
    if(tmax < t) tmax = t;
  }

  if(tmin == tmax) tmax++;   // div by 0 check

  int16_t y = FC_Top+1;
  int16_t incy = (FC_Height-20) / 2;
  int16_t dec = (tmax - tmin) / 2;
  int16_t t = tmax;

  // temp scale
  tft.setFreeFont(&FreeSans9pt7b);
  tft.setTextColor(rgb16(0, 63, 31)); // cyan text
  for(int i = 0; i < 3; i++)
  {
    tft.drawNumber(t/10, 8, y + 4);
    y += incy;
    t -= dec;
  }

  int hrs = hrng * m_fc.Freq / 3600; // normally 180ish hours
  uint16_t day_x = 0;
  if(hrs <= 0) // divide by 0
    return;

  tft.setTextDatum(TC_DATUM); // center day on noon

  tmElements_t tmE;
  breakTime(m_fc.Date + (fcDispOff * m_fc.Freq), tmE);

  uint8_t day = tmE.Wday - 1;              // current day

  for(int i = 0, h = tmE.Hour; i < hrs; i++, h++)
  {
    int x = i * FC_Width / hrs + FC_Left;
    h %= 24;
    if(h == 12)
    {
      tft.drawLine(x, FC_Top, x, FC_Top+FC_Height, rgb16(7, 14, 7) ); // dark gray
      if(day_x < FC_Left + FC_Width - 100) // skip last day if too far right
      {
        tft.drawString( dayShortStr(day+1), day_x = x, 10);
      }
    }
    else if(h==0) // new day (draw line)
    {
      tft.drawLine(x, FC_Top+1, x, FC_Top+FC_Height-2, rgb16(14, 28, 14) ); // (light gray)
      if(++day > 6) day = 0;
    }
  }
  tft.setTextDatum(TL_DATUM);

  uint16_t x2, y2, rh2;

  for(uint16_t i = fcDispOff; i < hrng && m_fc.Data[i].temp != -127; i++) // should be 41 data points
  {
    uint16_t y1 = FC_Top+FC_Height - 1 - (m_fc.Data[i].temp - tmin) * (FC_Height-2) / (tmax-tmin);
    uint16_t x1 = i * FC_Width / hrng + FC_Left;
    int rhY = (FC_Top + FC_Height) - (FC_Height * m_fc.Data[i].humidity / 1000);

    if(i)
    {
//      if(i == fcOff)  // current 3 hour time
//      {
//        int x3 = (i+1) * FC_Width / hrng + FC_Left;
//        tft.drawRect(x2, FC_Top + 30, x3-x2, 20, rgb16(0, 22, 11) );
//      }

      tft.drawLine(x2, rh2, x1, rhY, rgb16(0, 30, 0) ); // rh (green)

      tft.drawLine(x2, y2, x1, y1, (i== fcOff) ? rgb16(20, 0, 8) : rgb16(31, 0, 0) ); // red (current purple)
    }
    x2 = x1;
    y2 = y1;
    rh2 = rhY;
  }
}

// get value at current minute between hours
int Display::tween(int16_t t1, int16_t t2, int m, int r)
{
  if(r == 0) r = 1; // div by zero check
  float t = (float)(t2 - t1) * (m * 100 / r) / 100;
  return (int)(t + (float)t1);
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

#define RPAD 22 // right edge

// Draw the last 25 hours
void Display::historyPage()
{
  m_currPage = Page_Graph; // chart thing
  setBrightness(0, 150);
  loadImage("/bgBlank.png", 0, 0);
  
  int minTh, maxTh, maxTemp;

  m_tempLow  = minPointVal(0, maxTemp);
  (void)minPointVal(2, maxTh); // keep threshold high in frame
  if(maxTemp < maxTh) maxTemp = maxTh;
  m_tempHigh = (maxTemp + 9) / 10 * 10; // round up

  minTh = minPointVal(1, maxTh); // keep threshold low in frame
  if(m_tempLow > minTh) m_tempLow = minTh;
  m_tempLow = m_tempLow / 10 * 10; // round down

  if(m_tempHigh - m_tempLow < 50) // make sure the range is at least 5F
    m_tempHigh = m_tempLow + 50;

  int tmpInc = (m_tempHigh - m_tempLow) / 4;
  int temp = m_tempLow;
  int y = DISPLAY_HEIGHT-20;
  tft.setTextColor( rgb16(0, 63, 31 ));
  tft.setFreeFont(&FreeSans9pt7b);

  drawPointsTarget(rgb16( 6, 8, 4) ); // target (draw behind the other stuff)

  int x = DISPLAY_WIDTH - RPAD - (minute() / 5); // center over even hour, 5 mins per pixel
  int h = hourFormat12();

  while(x > 12 * 6)
  {
    x -= 12 * 6; // move left 6 hours
    h -= 6;
    if( h <= 0) h += 12;
    tft.drawLine(x, 10, x, DISPLAY_HEIGHT-10, rgb16(10, 20, 10) );
    String s = String(h);
    s += ":00";
    tft.drawString( s, x-4, 0); // draw hour above chart
  }

  for(int i = 0; i < 5; i++) // draw temp range over thresh
  {
    tft.drawString( String(temp / 10), DISPLAY_WIDTH-RPAD-4, y );
    if(i > 0) tft.drawLine( 10, y+10, DISPLAY_WIDTH-RPAD, y+8, rgb16(10, 20, 10) );
    y -= (DISPLAY_HEIGHT - 30) /4;
    temp += tmpInc;
  }

  drawPointsRh( rgb16(  0, 48,  0) ); // rh green
  drawPointsTemp(); // off/cool/heat colors
}

void Display::drawPointsTarget(uint16_t color)
{
  int i = m_pointsIdx - 1;
  if(i < 0) i = GPTS - 1;
  int x, x2, y, y2, tOld;
  const int h = DISPLAY_HEIGHT - 20;

  if(m_tempHigh == m_tempLow) // divide by zero check
    return;

  x2 = DISPLAY_WIDTH-RPAD;
  tOld = m_points[i].t.target;

  for(int x = DISPLAY_WIDTH-RPAD; x >= 10; x--)
  {
    if(m_points[i].t.u == 0)
    {
      if(x2 != DISPLAY_WIDTH-RPAD) // draw last bit
        tft.fillRect(x, DISPLAY_HEIGHT - 10 - y, x2 - x, y2 - y, color);
      return;
    }
    y = constrain( (m_points[i].t.target - m_tempLow) * h / (m_tempHigh - m_tempLow), 0, h); // scale to 0~300
    y2 = constrain( (m_points[i].t.target + ee.cycleThresh[(hvac.m_modeShadow == Mode_Heat) ? 1:0] - m_tempLow) * h
      / (m_tempHigh - m_tempLow), 0, h );

    int lastI = i;
    if(--i < 0)
      i = GPTS-1;
    if( (tOld != m_points[i].t.target && x != DISPLAY_WIDTH-RPAD) || x == 10)
    {
      tft.fillRect(x, DISPLAY_HEIGHT - 10 - y, x2 - x, y2 - y, color);
      tOld = m_points[lastI].t.target;
      x2 = x;
    }
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

  y2 = y2 * (DISPLAY_HEIGHT-20) / 1000; // 0.0~100.0 to 0~300

  for(int x = DISPLAY_WIDTH-RPAD-1, x2 = DISPLAY_WIDTH-RPAD; x >= 10; x--)
  {
    if(--i < 0)
      i = GPTS-1;

    y = m_points[i].bits.rh;
    if(m_points[i].t.u == 0)
      return;
    y = y * (DISPLAY_HEIGHT-20) / 1000;

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
  const int yOff = DISPLAY_HEIGHT-10;
  int y, y2;
  int x2 = DISPLAY_WIDTH-RPAD;
  int i = m_pointsIdx-1;

  if(i < 0) i = GPTS-1;

  for(int x = DISPLAY_WIDTH-RPAD; x >= 10; x--)
  {
    if(m_points[i].t.u == 0)
      break; // end
    y = constrain(m_points[i].t.inTemp - m_tempLow, 0, m_tempHigh - m_tempLow) * (DISPLAY_HEIGHT-20) / (m_tempHigh - m_tempLow);
//    if(y != y2)
//    {
      if(x != DISPLAY_WIDTH-RPAD)
      {
        tft.drawLine(x2, yOff - y2, x, yOff - y, stateColor(m_points[i].bits) );
      }
      y2 = y;
      x2 = x;
//    }
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
      color = rgb16(25, 50, 25); // gray
      break;
    case State_Cool: // cool
      color = rgb16(0, 0, 31); // blue
      break;
    case State_HP:
      color = rgb16(31, 50, 0); // yellow
      break;
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

int Display::minPointVal(int n, int &max)
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
  max = maxv;
  return minv;
}

void Display::outlineAllButtons()
{
  for(int i = 0; i < Btn_Count; i++)
  {
    tft.drawRect(m_btn[i].x, m_btn[i].y, m_btn[i].w, m_btn[i].h, rgb16(0,31,0));
  }
}
