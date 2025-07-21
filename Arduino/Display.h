#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include "Forecast.h"

// from 5-6-5 to 16 bit value (max 31, 63, 31)
#define rgb16(r,g,b) ( ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b )

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 320
#define DISPLAY_TIMEOUT  90  // 90 seconds

enum BTN
{
  Btn_Dow,
  Btn_Time,
  Btn_OutTemp,
  Btn_InTemp,
  Btn_Rh,
  Btn_TargetTemp,
  Btn_SetTempH,
  Btn_SetTempL,
  Btn_History,
  Btn_Override,
  Btn_Unused,
  Btn_Forecast,
  Btn_Fan,
  Btn_Mode,
  Btn_HeatMode,
  Btn_Humid,
  Btn_Up,
  Btn_Dn,
  Btn_Note,
  Btn_RSSI,
  Btn_Lock,
  Btn_Count,
};

enum Page
{
  Page_Thermostat,
  Page_Clock,
  Page_Graph,
  Page_ScreenSaver,
  Page_Forecast,
  Page_Usage,
};

struct Button
{
  uint8_t ID; // Warning: ID not used
  int16_t x;
  int16_t y;
  int16_t w;
  int16_t h;
};

union gflags
{
  uint32_t u;
  struct
  {
    uint32_t fan:1;
    uint32_t state:3;
    uint32_t rh:10;
    uint32_t tmdiff:9;
    int32_t sens4:9;
  };
};

union temps
{
  uint32_t u;
  struct
  {
    uint32_t inTemp:11;
    uint32_t target:10;
    int32_t  outTemp:11;
  };
};

struct gPoint
{
  temps t;
  int8_t sens0;
  int8_t sens1;
  int8_t sens2;
  int8_t sens3;
  gflags bits;
};

class Display
{
public:
  Display()
  {
  }
  void init(void);
  void oneSec(void);
  bool screen(bool bOn);
  void service(void);
  void goDark(void);
  void updateTemps(void);
  void updateNotification(bool bRef);
  bool getGrapthPoints(gPoint *pt, int n);
  int  minPointVal(int n, int &max);
  String makeName(uint8_t icon, uint8_t h);
  void drawFakeFloat(uint16_t val, uint16_t x, uint16_t y);

private:
  void buttonCmd(uint8_t btn);
  void dimmer(void);
  void updateModes(bool bForce); // update any displayed settings
  void buttonRepeat(void);
  void refreshAll(void);
  void loadBtnImage(const char *pszName, uint8_t btn);
  void updateRSSI(void);
  void drawTime(void);
  void drawOutTemp(void);
  void addGraphPoints(void);
  void historyPage(void);
  void drawPointsTarget(uint16_t color);
  void drawPointsRh(uint16_t color);
  void drawPointsTemp(void);
  uint16_t stateColor(gflags v);
  void outlineAllButtons(void);

  uint16_t m_backlightTimer = DISPLAY_TIMEOUT; // backlight timer, seconds
  uint8_t m_bright; // current brightness
  uint8_t m_lockDelay;
  uint8_t m_displayLocal; // local temp/rh display mode/timer
#define GPTS 640 // 480 px width - (10+10) padding
  gPoint m_points[GPTS];
  uint16_t m_pointsIdx = 0;
  uint16_t m_temp_counter = 2*60;
  uint8_t m_btnMode = 0;
  uint8_t m_btnDelay = 40; // for up/down repeat
  int m_tempLow; // used for chart
  int m_tempHigh; // ""
  uint8_t m_currPage = 0;
  const Button m_btn[Btn_Count] = {
    {Btn_Dow, 30, 8, 50, 20},
    {Btn_Time, 106, 8, 182, 20},
    {Btn_OutTemp, DISPLAY_WIDTH-126, 24, 103, 41},
    {Btn_InTemp, 25, 52, 174, 64},
    {Btn_Rh, 201, 52, 100, 40},
    {Btn_TargetTemp, DISPLAY_WIDTH-184, 124, 105, 41},
    {Btn_SetTempH, DISPLAY_WIDTH-185, 185, 105, 43},
    {Btn_SetTempL, DISPLAY_WIDTH-185, 250, 105, 43},

    {Btn_History,   15, 138, 60, 60},
    {Btn_Override,  82, 138, 60, 60},
    {Btn_Unused,   149, 138, 60, 60},
    {Btn_Forecast, 216, 138, 60, 60},

    {Btn_Fan,       15, 204, 60, 60},
    {Btn_Mode,      82, 204, 60, 60},
    {Btn_HeatMode, 149, 204, 60, 60},
    {Btn_Humid   , 216, 204, 60, 60},

    {Btn_Up,   DISPLAY_WIDTH-73, 179, 60, 60},
    {Btn_Dn,   DISPLAY_WIDTH-73, 243, 60, 60},

    {Btn_Note,  14, DISPLAY_HEIGHT-44, 265, 30},
    {Btn_RSSI, DISPLAY_WIDTH-38, 80, 24, 24},
    {Btn_Lock, DISPLAY_WIDTH-60, 80, 24, 25},
  };
public:
  uint32_t m_lastPDate = 0; // last data point added
  uint8_t m_adjustMode = 0; // which of 4 temps to adjust with rotary encoder/buttons
  bool    m_bLink;         // link adjust mode
  uint8_t m_brightness = 100; // initial brightness
  bool    m_bShowFC; // Show the forecast when it updates, also updates icons for clock
};

extern Display display;

#endif // DISPLAY_H
