#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <PNGdec.h>  // From Library Manager
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
  Btn_CoolTempH,
  Btn_CoolTempL,
  Btn_HeatTempH,
  Btn_HeatTempL,
  Btn_IndR,
  Btn_IndCH,
  Btn_IndCL,
  Btn_IndHH,
  Btn_IndHL,
  Btn_Fan,
  Btn_Mode,
  Btn_HeatMode,
  Btn_Humid,
  Btn_Up,
  Btn_Dn,
  Btn_Note,
  Btn_RSSI,
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

struct ImageCtrl
{
  int16_t x;
  int16_t y;
  int16_t srcX;
  int16_t srcY;
  int16_t w;
  int16_t h;
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
  void loadImage(char *pName, uint16_t x, uint16_t y);
  void loadImage(char *pName, uint16_t x, uint16_t y, uint16_t srcX, uint16_t srcY, uint16_t w, uint16_t h);
  void updateTemps(void);
  void Note(char *cNote);
  void updateNotification(bool bRef);
  bool getGrapthPoints(gPoint *pt, int n);
  int  minPointVal(int n, int &max);

private:
  void buttonCmd(uint8_t btn);
  void setBrightness(uint8_t immediate, uint8_t deferred);
  void dimmer(void);
  void updateModes(bool bForce); // update any displayed settings
  void buttonRepeat(void);
  void refreshAll(void);
  void updateAdjMode(bool bRef);  // current adjust indicator of the 4 temp settings
  void updateRSSI(void);
  void updateRunIndicator(bool bForce);
  void drawTime(void);
  void drawOutTemp(void);
  void forecastPage(void);
  void addGraphPoints(void);
  void historyPage(void);
  void drawPointsTarget(uint16_t color);
  void drawPointsRh(uint16_t color);
  void drawPointsTemp(void);
  uint16_t stateColor(gflags v);
  void outlineAllButtons(void);
  int  tween(int16_t t1, int16_t t2, int m, int r);

  uint16_t m_backlightTimer = 90; // backlight timers, seconds
  uint8_t m_bright; // current brightness;
#define GPTS 640 // 480 px width - (10+10) padding
  gPoint m_points[GPTS];
  uint16_t m_pointsIdx = 0;
  uint16_t m_temp_counter = 2*60;
  uint8_t m_btnMode = 0;
  uint8_t m_btnDelay = 40;
  int m_tempLow; // used for chart
  int m_tempHigh; // ""
  uint8_t m_currPage = 0;
  const Button m_btn[Btn_Count] = {
    {Btn_Dow, 30, 6, 50, 20},
    {Btn_Time, 110, 6, 180, 20},
    {Btn_OutTemp, DISPLAY_WIDTH-130, 22, 112, 40},
    {Btn_InTemp, 24, 50, 170, 64},
    {Btn_Rh, 204, 42, 100, 38},
    {Btn_TargetTemp, 204, 86, 100, 38},
    {Btn_CoolTempH, DISPLAY_WIDTH-146, 174, 60, 20},
    {Btn_CoolTempL, DISPLAY_WIDTH-146, 209, 60, 20},
    {Btn_HeatTempH, DISPLAY_WIDTH-146, 244, 60, 20},
    {Btn_HeatTempL, DISPLAY_WIDTH-146, 279, 60, 20},

    {Btn_IndR, DISPLAY_WIDTH-143, 130, 28, 24},

    {Btn_IndCH, DISPLAY_WIDTH-216, 174, 130, 20}, // used for cool/heat adjust select
    {Btn_IndCL, DISPLAY_WIDTH-216, 209, 130, 20},
    {Btn_IndHH, DISPLAY_WIDTH-216, 244, 130, 20},
    {Btn_IndHL, DISPLAY_WIDTH-216, 279, 130, 20},

    {Btn_Fan,       24, 140, 60, 60},
    {Btn_Mode,     100, 140, 60, 60},
    {Btn_HeatMode, 176, 140, 60, 60},
    {Btn_Humid   ,  24, 204, 60, 60},

    {Btn_Up,   402, 176, 60, 60},
    {Btn_Dn,   402, 240, 60, 60},

    {Btn_Note,  21, DISPLAY_HEIGHT-44, 230, 30},
    {Btn_RSSI, 220, 236, 24, 24},
  };
public:
  uint32_t m_lastPDate = 0;
  forecastData m_fc;
  uint8_t m_adjustMode = 0; // which of 4 temps to adjust with rotary encoder/buttons
  bool    m_bUpdateFcst = true;
  bool    m_bUpdateFcstIdle = true;
  bool    m_bFcstUpdated = false;
  uint8_t m_brightness;
};

extern Display display;

#endif // DISPLAY_H
