#include "HVAC.h"
#include "display.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include "jsonstring.h"
#include <time.h>
#include "eeMem.h"
#include "screensavers.h"
#include "music.h"
Music mus;
#include "Media.h"
#include <TFT_eSPI.h> // TFT_espi library
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite sprite = TFT_eSprite(&tft);
static_assert(USER_SETUP_ID==201, "User setup incorrect in TFT_eSPI");

#include "forecast.h"

// FT6206, FT6336U - touchscreen library                  // modified -> Wire.begin(18, 19)
#include "Adafruit_FT6206.h"
Adafruit_FT6206 ts = Adafruit_FT6206();

extern Forecast FC;
extern HVAC hvac;
extern tm gLTime;
extern void WsSend(String s);

ScreenSavers ss;

void Display::init(void)
{
  analogWrite(TFT_BL, 0);  // black out while display is noise 

  memset(m_points, 0, sizeof(m_points));
  pinMode(39, INPUT); // touch int

  ts.begin(40);                       // adafruit touch
  tft.init();                         // TFT_eSPI
  tft.setRotation(1);                 // set desired rotation
  tft.setTextDatum(TL_DATUM);
  sprite.createSprite(DISPLAY_WIDTH, DISPLAY_HEIGHT);
  mus.init();
  media.init();
  FC.init();
  screen(true);
}

bool Display::screen(bool bOn)
{
  static bool bOldOn = false;

  if(bOldOn && m_currPage) // not in sync
    bOldOn = false;

  if(bOn == false && m_currPage == Page_ScreenSaver) // alternate the thermo page and ss
    bOn = true;

  m_backlightTimer = DISPLAY_TIMEOUT; // update the auto backlight timer

  if(bOn)
  {
    if( bOn == bOldOn )
      return false; // no change occurred
    m_currPage = Page_Thermostat;
    media.loadImage("bg", 0, 0);
    sprite.fillRect(m_btn[Btn_SetTempH].x + 16, m_btn[Btn_SetTempH].y, m_btn[Btn_SetTempH].w, m_btn[Btn_SetTempH].h, TFT_BLACK );
    sprite.fillRect(m_btn[Btn_SetTempL].x + 16, m_btn[Btn_SetTempL].y, m_btn[Btn_SetTempL].w, m_btn[Btn_SetTempL].h, TFT_BLACK );
    refreshAll();
    sprite.pushSprite(0, 0);
  }
  else
  {
    m_currPage = Page_ScreenSaver;
    ss.select( random(0, SS_Count) );
  }
  bOldOn = bOn;
  return true;
}

void Display::service(void)
{
  static int8_t currBtn;
  static bool bPress;

  mus.service(); // sound handler

  if(m_currPage == Page_ScreenSaver)
    ss.run();

  static uint32_t lastms;
  static uint32_t touchms;

  if (digitalRead(39) == LOW)// touch I/O causes SHT40 errors. SHT40 really not recommended. Use AM2320 or AM2322.
  {
    bool bSkip = (m_brightness < ee.brightLevel[1]);
    m_brightness = ee.brightLevel[1]; // increase brightness for any touch
    analogWrite(TFT_BL, m_brightness);

    m_backlightTimer = DISPLAY_TIMEOUT;
 
    touchms = millis();
    if(millis() - lastms < 100) // filter the pulses, also repeat speed
      return;
    lastms = millis();

    TS_Point p = ts.getPoint();
    uint16_t y = 320 - p.x;     // adjust touch point for rotation = 1
    uint16_t x = p.y;

    if(bPress == false) // only need 1 touch
    {
      if(bSkip); // just brigthten, ignore input
      else if(m_currPage != Page_Thermostat) // not on thermostat
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
            currBtn = i;
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
  switch(btn)
  {
    case Btn_SetTempH:
    case Btn_SetTempL:
      if(ee.b.bLock) break;
      m_bLink = !m_bLink;
      m_adjustMode = btn - Btn_SetTempH;
      updateTemps(false);
      sprite.pushSprite(0, 0);
      break;

    case Btn_Up: // Up button mode = 1
    case Btn_Dn: // Down button mode = 2
      if(ee.b.bLock) break;
      m_btnMode = btn - Btn_Up + 1;
      buttonRepeat();
      m_btnDelay = 7; // first repeat
      break;
    case Btn_Forecast:
      if(FC.forecastPage())
        m_currPage = Page_Forecast;
      break;
    case Btn_Override:
      if(ee.b.bLock) break;
      hvac.override(true);
      break;
    case Btn_History:
      historyPage();
      break;
    case Btn_Fan:
      if(ee.b.bLock) break;
      hvac.setFan( (hvac.getFan() == FM_On) ? FM_Auto : FM_On ); // Todo: Add 3rd icon
      updateModes(false); // faster feedback
      sprite.pushSprite(0, 0);
      break;
    case Btn_Mode:
      if(ee.b.bLock) break;
      hvac.setMode( (hvac.getSetMode() + 1) & 3 );
      updateModes(false); // faster feedback
      sprite.pushSprite(0, 0);
      break;
    case Btn_HeatMode:
      if(ee.b.bLock) break;
      hvac.setHeatMode( (hvac.getHeatMode() + 1) % 3 );
      updateModes(false); // faster feedback
      sprite.pushSprite(0, 0);
      break;
    case Btn_Humid:
      if(ee.b.bLock) break;
      hvac.setHumidifierMode( hvac.getHumidifierMode() + 1 );
      updateModes(false); // faster feedback
      sprite.pushSprite(0, 0);
      break;

    case Btn_Note:
      if(ee.b.bLock) break;
      hvac.m_notif = Note_None;
      break;
    case Btn_Time: // time
//      m_currPage = Page_ScreenSaver;
//      ss.select( SS_Clock );
      break;
    case Btn_TargetTemp:
      if(ee.b.bLock) break;
      hvac.enableRemote();
      break;
    case Btn_InTemp: // in
    case Btn_Rh: // rh
      if(m_displayLocal)
        m_displayLocal = 0;
      else
        m_displayLocal = 10; // might change this
      break;
    case Btn_Lock: // lock
      if(!ee.b.bLock)
        ee.b.bLock = 1;
      else
        m_lockDelay = 30; // press and hold ~5 seconds
      break;
  }
  mus.add(6000, 20); // beep on button press
}

// called each second
void Display::oneSec()
{
  if( m_backlightTimer ) // the dimmer thing
  {
    if(--m_backlightTimer == 0)
    {
      m_brightness = ee.brightLevel[0]; // dim level
      analogWrite(TFT_BL, m_brightness);
      screen(false);
    }
  }

  static uint8_t lastState;
  static bool lastFan;
  if(--m_temp_counter <= 0 || hvac.getState() != lastState || hvac.getFanRunning() != lastFan)
  {
    addGraphPoints();
    lastState = hvac.getState();
    lastFan = hvac.getFanRunning();
  }

  if(m_currPage == Page_Forecast)
    FC.forecastAnimate();

  if(m_currPage != Page_Thermostat)
    return;

  drawTime();    // time update every second
  updateModes(false);    // mode, heat mode, fan mode
  updateTemps(false);    // 
  updateNotification(false);
  updateRSSI();     //

  if(m_displayLocal) // timer for remote temp display
    m_displayLocal--;

  static uint8_t dly = 1;
  if(--dly == 0)
  {
    dly = 60; // uppdate it every minute
    drawOutTemp();
  }

  if(FC.m_bFcstUpdated)
  {
    FC.m_bFcstUpdated = false;
    drawOutTemp();
  }
  sprite.pushSprite(0, 0);

  if( m_bShowFC ) // Show FC page when update occurs
  {
    m_bShowFC = false;
    if(FC.forecastPage())
      m_currPage = Page_Forecast;
  }
}

void Display::drawOutTemp()
{
  FC.getMinMax(hvac.m_outMin, hvac.m_outMax, 0, ee.fcRange);

  int outTempShift;
  int outTempReal = FC.getCurrentTemp(outTempShift, ee.fcOffset[hvac.m_modeShadow == Mode_Heat] );

  // Summer/winter curve.  Summer is delayed 3 hours
  hvac.updateOutdoorTemp( outTempShift );

  if(m_currPage == Page_Thermostat)
  {
    drawFakeFloat(outTempReal, m_btn[Btn_OutTemp].x, m_btn[Btn_OutTemp].y, 17, rgb16(0, 63, 31), TFT_BLACK, "o" );

    static bool bInit = false; // make first time display update fast
    if(!bInit)
    {
      int16_t t = hvac.getSetTemp(Mode_Off, 0 );
      hvac.setTemp(Mode_Off, t, 0);
      bInit = true;
    }
  }
}

void Display::drawFakeFloat(uint16_t val, int16_t x, int16_t y, uint8_t h, uint16_t fg, uint16_t bg, char *pSpecialChar)
{
  uint8_t digits[5] = {0};
  bool bNone = true;

  if(pSpecialChar)
  {
    uint16_t xPos = x + 1 + (h * 6);
    uint16_t yPos = y;
    if(h > 20)
    {
      xPos += 16; // the big one
      sprite.setFreeFont(&FreeSans12pt7b);
    }
    else
    {
      sprite.setFreeFont(&FreeSans9pt7b);
    }
    if(pSpecialChar[0] == 'o') yPos -= 4; // The font has no degree symbol, so using o. Ugh.
    sprite.setTextColor(fg);
    sprite.drawString(pSpecialChar, xPos, yPos );
  }

  x -= 20;

  for(int8_t i = 0; i < 4; i++)
  {
    digits[i] = val % 10;
    val /= 10;
  }

  uint8_t nCnt = 4;
  if(digits[3] == 0) nCnt--;

  for(int8_t i = 0; i < nCnt; i++)
  {
    drawDigit(digits[i], 4 - i, x, y, h, fg, bg);
    if(i == 0)
    {
      x -= h>>2;
      drawDigit(10, 4 - i, x, y, h, fg, bg); // decimal place
    }
  }
}

void Display::drawDigit(uint8_t digit, uint8_t pos, int16_t x, int16_t y, uint8_t h, uint16_t fg, uint16_t bg)
{
  int8_t h2 = h << 1;
  int8_t w = h;
  y++;                          // 0    1     2     3      4     5     6     7     8     9 
  static uint8_t bitmask[12] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x00};

  uint8_t bit = bitmask[digit];
  x += pos * ( (w>>1) + w);

  int16_t yh = y + h;
  int16_t yh2= y + h2;
  int16_t xw = x + w;

  uint8_t thick = h / 5;

  if(digit == 10)
  {
    sprite.drawWideLine( x-3, yh2-1, x-2, yh2, thick, fg); // decimal
    return;
  }

  thick++;
  if((bit & 0x01) == 0) sprite.drawWideLine( x+ 3, y   , xw+1, y    , thick, bg); // T
  if((bit & 0x02) == 0) sprite.drawWideLine( xw+2, y +1, xw+1, yh -1, thick, bg); // R1
  if((bit & 0x04) == 0) sprite.drawWideLine( xw+1, yh+1, xw  , yh2-1, thick, bg); // R2
  if((bit & 0x08) == 0) sprite.drawWideLine( x+ 1, yh2 , xw-1, yh2  , thick, bg); // B
  if((bit & 0x10) == 0) sprite.drawWideLine( x+ 1, yh+1, x   , yh2-1, thick, bg); // L2
  if((bit & 0x20) == 0) sprite.drawWideLine( x+ 2, y +1, x +1, yh -1, thick, bg); // L1
  if((bit & 0x40) == 0) sprite.drawWideLine( x+ 2, yh  , xw  , yh   , thick, bg); // C

  thick--;
  if(bit & 0x01) sprite.drawWideLine( x+ 3, y   , xw+1, y    , thick, fg); // T
  if(bit & 0x02) sprite.drawWideLine( xw+2, y +1, xw+1, yh -1, thick, fg); // R1
  if(bit & 0x04) sprite.drawWideLine( xw+1, yh+1, xw  , yh2-1, thick, fg); // R2
  if(bit & 0x08) sprite.drawWideLine( x+ 1, yh2 , xw-1, yh2  , thick, fg); // B
  if(bit & 0x10) sprite.drawWideLine( x+ 1, yh+1, x   , yh2-1, thick, fg); // L2
  if(bit & 0x20) sprite.drawWideLine( x+ 2, y +1, x +1, yh -1, thick, fg); // L1
  if(bit & 0x40) sprite.drawWideLine( x+ 2, yh  , xw  , yh   , thick, fg); // C
}

void Display::updateTemps(bool bForce)
{
  static uint16_t last[7];  // only draw changes
  if(m_currPage || bForce)
    memset(last, 0, sizeof(last));

  int16_t inTemp = (m_displayLocal) ? hvac.m_localTemp : hvac.m_inTemp;
  int16_t rh = (m_displayLocal) ? hvac.m_localRh : hvac.m_rh;
  uint16_t fg = (m_displayLocal) ? rgb16(4, 63, 1) : rgb16(0, 63, 31);

  if(last[0] != inTemp)
    drawFakeFloat((last[0] = inTemp), m_btn[Btn_InTemp].x - 20, m_btn[Btn_InTemp].y, 28, fg, TFT_BLACK, "o" );

  if(last[1] != hvac.m_targetTemp)
    drawFakeFloat((last[1] = hvac.m_targetTemp), m_btn[Btn_TargetTemp].x, m_btn[Btn_TargetTemp].y, 17, fg, TFT_BLACK, "o" );

  if(last[2] != rh)
    drawFakeFloat((last[2] = rh), m_btn[Btn_Rh].x, m_btn[Btn_Rh].y, 17, fg, TFT_BLACK, "%" );

  uint8_t nMode = hvac.getSetMode();

  if(nMode == Mode_Auto)
    nMode = (hvac.getAutoMode() == Mode_Cool) ? Mode_Cool : Mode_Heat;

  static const uint16_t colors[] = {rgb16(21, 44, 21), rgb16(0, 4, 31), rgb16(31, 13, 10) };
  uint16_t fgm = colors[nMode];

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
        drawFakeFloat((last[3] = ee.coolTemp[1]), m_btn[Btn_SetTempH].x, m_btn[Btn_SetTempH].y+2, 17, fgm, TFT_BLACK, "o" );
        last[6] = 10; // force outline refresh 
      }
      if(last[4] != ee.coolTemp[0])
      {
        drawFakeFloat((last[4] = ee.coolTemp[0]), m_btn[Btn_SetTempL].x, m_btn[Btn_SetTempL].y+2, 17, fgm, TFT_BLACK, "o" );
        last[6] = 10;
      }
      break;
    case Mode_Heat:
      if(last[3] != ee.heatTemp[1])
      {
        drawFakeFloat((last[3] = ee.heatTemp[1]), m_btn[Btn_SetTempH].x, m_btn[Btn_SetTempH].y+2, 17, fgm, TFT_BLACK, "o" );
        last[6] = 10;
      }
      if(last[4] != ee.heatTemp[0])
      {
        drawFakeFloat((last[4] = ee.heatTemp[0]), m_btn[Btn_SetTempL].x, m_btn[Btn_SetTempL].y+2, 17, fgm, TFT_BLACK, "o" );
        last[6] = 10;
      }
      break;
  }

  if( last[6] != ((m_adjustMode<<1) | m_bLink) )
  {
    last[6] = (m_adjustMode<<1) | m_bLink;
    int8_t am = m_adjustMode;
    sprite.drawRect(m_btn[Btn_SetTempH+am].x + 14, m_btn[Btn_SetTempH+am].y, m_btn[Btn_SetTempH+am].w, m_btn[Btn_SetTempH+am].h, rgb16(0,31,0));
    sprite.drawRect(m_btn[Btn_SetTempH+(am^1)].x + 14, m_btn[Btn_SetTempH+(am^1)].y, m_btn[Btn_SetTempH+(am^1)].w, m_btn[Btn_SetTempH+(am^1)].h, (m_bLink) ? rgb16(0,31,0) : TFT_BLACK);
  }
}

void Display::updateModes(bool bForce) // update any displayed settings
{
  static bool bFan = true; // set these to something other than default to trigger them all
  static bool bOverride = true;
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

    const char *pFan[] = {"fanOff", "fanOn", "fanAutoOn"};
    loadBtnImage(pFan[idx], Btn_Fan);
  }

  if(nMode != hvac.getSetMode() || nState != hvac.getState())
  {
    nMode = hvac.getSetMode();

    const char *szModes[] = {"off", "cool", "heat", "auto", "auto"};
    loadBtnImage(szModes[nMode], Btn_Mode);

    m_bLink = true;
    m_adjustMode = 0;
    updateTemps(bForce);

    nState = hvac.getState();
    if(nState) // draw the ON indicator on the button
      loadBtnImage("btnled", Btn_Mode);
  }

  if(heatMode != hvac.getHeatMode())
  {
    heatMode = hvac.getHeatMode();
    const char *szHeatModes[] = {"hp", "ng", "hauto"};
    loadBtnImage(szHeatModes[heatMode], Btn_HeatMode);
  }

  if(humidMode != (hvac.getHumidifierMode() + hvac.getHumidifierRunning()) )
  {
    humidMode = (hvac.getHumidifierMode() + hvac.getHumidifierRunning());

    loadBtnImage("humid", Btn_Humid);

    const char szHmode[][2] = {"", "F", "R", "1", "2", ""};
    sprite.setFreeFont(&FreeSans9pt7b);
    sprite.setTextColor( rgb16(0, 63, 31) );
    sprite.drawString((char*)szHmode[hvac.getHumidifierMode()], m_btn[Btn_Humid].x + 27, m_btn[Btn_Humid].y + 28);

    if(hvac.getHumidifierRunning())
      loadBtnImage("btnled", Btn_Humid);
  }

  static bool bLock = true;
  if(bLock != ee.b.bLock || bForce)
  {
    loadBtnImage( ( (ee.b.bLock) ? "lock" : "unlock" ), Btn_Lock);
    bLock = ee.b.bLock;
  }
  loadBtnImage( "over", Btn_Override);
  if((hvac.m_overrideTimer ? true:false) != bOverride)
  {
    loadBtnImage("btnled", Btn_Override);
    bOverride = (hvac.m_overrideTimer ? true:false);
  }
}

void Display::loadBtnImage(const char *pszName, uint8_t btn)
{
  media.loadImage(pszName, m_btn[btn].x, m_btn[btn].y);  
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
  updateTemps(false);
}

// time and dow on main page
void Display::drawTime()
{
  String sTime = ss.localTimeString();
  sTime += " ";

#define TIME_X_OFFSET 80
  sprite.setTextColor(rgb16(0,63,16), rgb16(8,16,8) );
  sprite.setFreeFont(&FreeSans12pt7b);
  sprite.drawString(sTime, m_btn[Btn_Time].x + TIME_X_OFFSET, m_btn[Btn_Time].y);

  sTime = ss.monthShortStr(gLTime.tm_mon);
  sTime += " ";
  sTime += String(gLTime.tm_mday);
  sprite.drawString(sTime, m_btn[Btn_Time].x, m_btn[Btn_Time].y);
  sprite.drawString(ss.dayShortStr(gLTime.tm_wday), m_btn[Btn_Dow].x, m_btn[Btn_Dow].y);
}

const char *pNotes[] = {
  "",  //  Note_None,
  "Connecting to WiFi", //  Note_Connecting,
#ifdef REMOTE
   "Searching for HVAC",
#else
  "Connected",  //  Note_Connected,
#endif
  "", //  Note_HVAC_connected,
  "Remote Off", //  Note_RemoteOff,
  "Remote On",  //  Note_RemoteOn,
  "Cycle Limit",  //  Note_CycleLimit,
  "HVAC Not Connected", //  Note_Network, // Sound errors below this point
  "Forecast Error", //  Note_Forecast,
  "Replace Filter", //  Note_Filter,
  "Use EspTouch App", //  Note_EspTouch,
  "Sensor Falied",  //  Note_Sensor,
  "HVAC Found", //  Note_HVACFound,
  "Updating Firmware",  //  Note_Updating,
  "HP Malfunction", //  Note_HPHeatError,
  "NG Malfunction", //  Note_NGHeatError,
  "Cool Malfunction", //  Note_CoolError,
};

// update the notification text box
void Display::updateNotification(bool bRef)
{
  static uint8_t note_last = Note_None; // Needs a clear after startup
  static uint16_t nTimer = 0;

  if(m_currPage || (note_last == Note_None && hvac.m_notif == Note_None))
    return;
  if(nTimer)
  {
    if(--nTimer == 0)
      hvac.m_notif = Note_None; // auto-clear notif
  }

  nTimer = 0;
  String s = pNotes[hvac.m_notif];
  
  switch(hvac.m_notif)
  {
    case Note_Connecting:
    case Note_Connected:
    case Note_HVAC_connected:
    case Note_RemoteOff:
    case Note_RemoteOn:
      nTimer = 30;
      break;
    case Note_CycleLimit:
    case Note_HVACFound:
    case Note_HPHeatError:
    case Note_NGHeatError:
    case Note_CoolError:
      nTimer = 60;
      break;
    case Note_Updating:
      m_brightness = 20;
      analogWrite(TFT_BL, m_brightness); // dim it a lot. flashing takes power, so this evens it out a bit
      break;
  }
  sprite.fillRect(m_btn[Btn_Note].x, m_btn[Btn_Note].y, m_btn[Btn_Note].w, m_btn[Btn_Note].h, rgb16(0,3,4));
  sprite.setTextColor(rgb16(31, 5, 10), rgb16(0,3,4));
  sprite.setFreeFont(&FreeSans12pt7b);
  sprite.drawString(s, m_btn[Btn_Note].x+2, m_btn[Btn_Note].y+6);
  if(s.length())
  {
    if(note_last != hvac.m_notif) // don't send multiples
    {
      jsonString js("alert");
      js.Var("text", s);
      WsSend(js.Close());
    }
    if(note_last != hvac.m_notif || hvac.m_notif >= Note_Network) // once / repeats if important
    {
      mus.add(3500, 180);
      mus.add(2500, 90); // notification sound
      mus.add(3500, 180);
    }
  }
  note_last = hvac.m_notif;

  if(bRef) // normally from external code
    sprite.pushSprite(0, 0);
}

// things to update on page change to thermostat
void Display::refreshAll()
{
  updateNotification(false);
  drawTime();
  updateTemps(true);
  updateModes(true);
  drawOutTemp();
  loadBtnImage("up", Btn_Up);
  loadBtnImage("dn", Btn_Dn);
  loadBtnImage("bbtn", Btn_Unused);
  loadBtnImage("weather", Btn_Forecast);
  loadBtnImage("history", Btn_History);
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
  rssiT = rssiAvg;
  int sigStrength = 127 + rssiAvg;
  int wh = m_btn[Btn_RSSI].w; // width and height
  int x = m_btn[Btn_RSSI].x; // X/Y position
  int y = m_btn[Btn_RSSI].y;
  int sect = 127 / 5; //
  int dist = wh  / 5; // distance between blocks

  y += wh;

  for (int i = 1; i < 6; i++)
  {
    sprite.fillRect( x + i*dist, y - i*dist, dist-2, i*dist, (sigStrength > i * sect) ? rgb16(0, 63,31) : rgb16(5, 10, 5) );
  }
}

void Display::addGraphPoints()
{
  if( hvac.m_inTemp == 0 || hvac.m_targetTemp == 0)
    return;
  
  m_temp_counter = 5*60;         // update every 5 minutes
  gPoint *p = &m_points[m_pointsIdx];

  uint32_t pdate = time(nullptr);
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
  media.loadImage("bgBlank", 0, 0);
  
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
  sprite.setTextColor( rgb16(0, 63, 31 ));
  sprite.setFreeFont(&FreeSans9pt7b);

  drawPointsTarget(rgb16( 6, 8, 4) ); // target (draw behind the other stuff)

  int x = DISPLAY_WIDTH - RPAD - (gLTime.tm_min / 5); // center over even hour, 5 mins per pixel
  int h = ss.hourFormat12(gLTime.tm_hour);

  while(x > 12 * 6)
  {
    x -= 12 * 6; // move left 6 hours
    h -= 6;
    if( h <= 0) h += 12;
    sprite.drawLine(x, 10, x, DISPLAY_HEIGHT-10, rgb16(10, 20, 10) );
    String s = String(h);
    s += ":00";
    sprite.drawString( s, x-4, 0); // draw hour above chart
  }

  for(int i = 0; i < 5; i++) // draw temp range over thresh
  {
    sprite.drawString( String(temp / 10), DISPLAY_WIDTH-20, y );
    if(i > 0) sprite.drawLine( 10, y+10, DISPLAY_WIDTH-RPAD, y+8, rgb16(10, 20, 10) );
    y -= (DISPLAY_HEIGHT - 30) /4;
    temp += tmpInc;
  }

  drawPointsRh( rgb16(  0, 48,  0) ); // rh green
  drawPointsTemp(); // off/cool/heat colors
  sprite.pushSprite(0, 0);
}

void Display::drawPointsTarget(uint16_t color)
{
  int i = m_pointsIdx - 1;
  if(i < 0) i = GPTS - 1;
  int x, x2, tOld;
  const int h = DISPLAY_HEIGHT - 20;

  if(m_tempHigh == m_tempLow) // divide by zero check
    return;

  x2 = DISPLAY_WIDTH-RPAD;
  tOld = m_points[i].t.target;
  int16_t yH, yL;

  for(int x = DISPLAY_WIDTH-RPAD; x >= 10; x--)
  {
    if(m_points[i].t.u == 0)
    {
      if(x2 != DISPLAY_WIDTH-RPAD) // draw last bit if valid
        sprite.fillRect(x, DISPLAY_HEIGHT - 10 - yH, x2 - x, yH - yL, color);
      return;
    }
    yL = (m_points[i].t.target - m_tempLow) * h / (m_tempHigh - m_tempLow); // scale to 0~300
    yH = (m_points[i].t.target + ee.cycleThresh[(hvac.m_modeShadow == Mode_Heat) ? 1:0] - m_tempLow) * h / (m_tempHigh - m_tempLow);

    int lastI = i;
    if(--i < 0)
      i = GPTS-1;
    if( (tOld != m_points[i].t.target && x != DISPLAY_WIDTH-RPAD) || x == 10)
    {
      sprite.fillRect(x, DISPLAY_HEIGHT - 10 - yH, x2 - x, yH - yL, color);
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
      sprite.drawLine(x, yOff - y, x2, yOff - y2, color);
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
    if(x != DISPLAY_WIDTH-RPAD)
    {
      sprite.drawLine(x2, yOff - y2, x, yOff - y, stateColor(m_points[i].bits) );
    }
    y2 = y;
    x2 = x;
    if(--i < 0)
      i = GPTS-1;
  }
}

uint16_t Display::stateColor(gflags v) // return a color based on run state
{
                                //   gray, blue, yellow, red
const uint16_t nColors[4] PROGMEM = {rgb16(25, 50, 25), rgb16(0, 0, 31), rgb16(31, 50, 0), rgb16(31, 0, 0)};

  return nColors[v.state];
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
    sprite.drawRect(m_btn[i].x, m_btn[i].y, m_btn[i].w, m_btn[i].h, rgb16(0,31,0));
  }
}
