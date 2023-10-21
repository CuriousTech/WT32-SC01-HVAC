#ifndef DISPLAY_H
#define DISPLAY_H

#include <Arduino.h>
#include <PNGdec.h> // From Library Manager
#include "Forecast.h"

// bit shifting for ease, to 5-6-5 format
#define rgb(r,g,b) ( (((uint16_t)r << 8) & 0xF800) | (((uint16_t)g << 3) & 0x07E0) | ((uint16_t)b >> 3) )
// from 5-6-5 to 16 bit value (max 31, 63, 31)
#define rgb16(r,g,b) ( ((uint16_t)r << 11) | ((uint16_t)g << 5) | (uint16_t)b )

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 320
#define DISPLAY_TIMEOUT  90  // 90 seconds

enum BTN
{
  Btn_Dow,
  Btn_Time,
  Btn_Fc,
  Btn_OutTemp,
  Btn_InTemp,
  Btn_Rh,
  Btn_TargetTemp,
  Btn_CoolTempH,
  Btn_CoolTempL,
  Btn_HeatTempH,
  Btn_HeatTempL,
  Btn_IndH, // p6 humidifier
  Btn_IndR, // p4 run
  Btn_IndCH,
  Btn_IndCL,
  Btn_IndHH,
  Btn_IndHL,
  Btn_Fan,
  Btn_Mode,
  Btn_HeatMode,
  Btn_Up,
  Btn_Dn,
  Btn_Note,
  Btn_RSSI,
};

enum Page
{
  Page_Thermostat,
  Page_Clock,
  Page_Graph,
  Page_ScreenSaver,
};

enum Saver
{
  SS_Lines,
  SS_Boing,
  SS_Count,
};

struct Line
{
  int16_t x1;
  int16_t y1;
  int16_t x2;
  int16_t y2;
};

struct Ball
{
  int16_t x;
  int16_t y;
  int16_t dx;
  int16_t dy;
  int16_t color;
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
  void updateTemps(void);
  bool drawForecast(bool bRef);
  void Note(char *cNote);
  void updateNotification(bool bRef);
  bool getGrapthPoints(gPoint *pt, int n);
  int  minPointVal(int n);

private:
  void buttonCmd(uint8_t btn);
  void dimmer(void);
  void updateModes(bool bForce); // update any displayed settings
  void buttonRepeat(void);
  void loadImage(char *pName, uint16_t x, uint16_t y);
  void refreshAll(void);
  void drawClock(bool bInit);
  void cspoint(float &x2, float &y2, float x, float y, float angle, float size);
  void updateAdjMode(bool bRef);  // current adjust indicator of the 4 temp settings
  void updateRSSI(void);
  void updateRunIndicator(bool bForce);
  void drawTime(void);
  void drawOutTemp(void);
  void addGraphPoints(void);
  void drawGraph(void);
  void Lines(void);
  void Boing(bool bInit);
  void drawPointsTarget(uint16_t color);
  void drawPointsRh(uint16_t color);
  void drawPointsTemp(void);
  uint16_t stateColor(gflags v);

  int  tween(int8_t t1, int8_t t2, int m, int r);

  uint16_t m_backlightTimer = 90; // backlight timers, seconds
#define GPTS 640 // 320 px width - (10+10) padding
  gPoint m_points[GPTS];
  uint16_t m_pointsIdx = 0;
  uint16_t m_temp_counter = 2*60;
  uint8_t m_btnMode = 0;
  uint8_t m_btnDelay = 0;
  int m_tempLow; // 66.0 base
  int m_tempHigh; // 90.0 top
  int m_tempMax;
  uint8_t m_currPage = 0;
  uint8_t m_brightness;
  uint8_t m_saver;
#define BTN_CNT 25
  const Button m_btn[BTN_CNT] = {
    {Btn_Dow, 30, 6, 50, 20},
    {Btn_Time, 110, 6, 180, 20},
    {Btn_Fc, 2, 34, 327, 78},
    {Btn_OutTemp, DISPLAY_WIDTH-76, 22, 46, 20},
    {Btn_InTemp, DISPLAY_WIDTH-76, 54, 46, 20},
    {Btn_Rh, DISPLAY_WIDTH-76, 82, 46, 20},
    {Btn_TargetTemp, DISPLAY_WIDTH-76, 130, 46, 20},
    {Btn_CoolTempH, DISPLAY_WIDTH-146, 174, 46, 20},
    {Btn_CoolTempL, DISPLAY_WIDTH-146, 209, 46, 20},
    {Btn_HeatTempH, DISPLAY_WIDTH-146, 244, 46, 20},
    {Btn_HeatTempL, DISPLAY_WIDTH-146, 279, 46, 20},

    {Btn_IndH, DISPLAY_WIDTH-143,  82, 20, 20},
    {Btn_IndR, DISPLAY_WIDTH-143, 130, 20, 20},

    {Btn_IndCH, DISPLAY_WIDTH-216, 174, 184, 20},
    {Btn_IndCL, DISPLAY_WIDTH-216, 209, 184, 20},
    {Btn_IndHH, DISPLAY_WIDTH-216, 244, 184, 20},
    {Btn_IndHL, DISPLAY_WIDTH-216, 279, 184, 20},

    {Btn_Fan,       25, 140, 60, 60},
    {Btn_Mode,      95, 140, 60, 60},
    {Btn_HeatMode, 165, 140, 60, 60},

    {Btn_Up,   402, 176, 60, 60},
    {Btn_Dn,   402, 240, 60, 60},

    {Btn_Note,  21, DISPLAY_HEIGHT-44, 230, 30},
    {Btn_RSSI, 220, 236, 24, 24},
    {0},
  };
public:
  uint32_t m_lastPDate = 0;
  forecastData m_fc;
  uint8_t m_adjustMode = 0; // which of 4 temps to adjust with rotary encoder/buttons
  bool    m_bUpdateFcst = true;
  bool    m_bUpdateFcstIdle = true;
  bool    m_bFcstUpdated = false;
};

#endif // DISPLAY_H
