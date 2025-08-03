#include "screensavers.h"
#include <Time.h>
#include "digitalFont.h"
#include "display.h"
#include "forecast.h"
#include "Media.h"

extern Forecast FC;

extern const char monthShortStr[12][4];
extern const char dayShortStr[7][4];

const char _dayStr[7][7] PROGMEM = {"Sun","Mon","Tues","Wednes","Thurs","Fri","Satur"};
const char _monthShortStr[12][4] PROGMEM = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};
const char _dayShortStr[7][4] PROGMEM = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char _monthStr[12][10] PROGMEM = {"January","February","March","April","May","June","July","August","September","October","November","December"};

extern void WsSend(String s);

const char *ScreenSavers::monthStr(uint8_t m)
{
  return _monthStr[m];
}
const char *ScreenSavers::monthShortStr(uint8_t m)
{
  return _monthShortStr[m];
}
const char *ScreenSavers::dayShortStr(uint8_t m)
{
  return _dayShortStr[m];
}

String ScreenSavers::localTimeString()
{
  tm timeinfo;
  getLocalTime(&timeinfo);

  String sTime = String( hourFormat12(timeinfo.tm_hour) );
  if(hourFormat12(timeinfo.tm_hour) < 10)
    sTime = " " + sTime;
  sTime += ":";
  if(timeinfo.tm_min < 10) sTime += "0";
  sTime += timeinfo.tm_min;
  sTime += ":";
  if(timeinfo.tm_sec < 10) sTime += "0";
  sTime += timeinfo.tm_sec;
  sTime += " ";
  sTime += (timeinfo.tm_hour >= 12) ? "PM":"AM";
  return sTime;
}

void ScreenSavers::select(int n)
{
  m_saver = n;
  randomSeed(micros());

  switch(m_saver)
  {
//    case SS_Clock:
//        Clock(true);
//        break;
    case SS_Lines:
        Lines(true);
        break;
    case SS_Boing:
        Boing(true);
        break;
    case SS_Calendar:
        Calendar(true);
        break;
  }
}

void ScreenSavers::run()
{
  switch(m_saver)
  {
//    case SS_Clock: Clock(false); break;
    case SS_Lines: Lines(false); break;
    case SS_Boing: Boing(false); break;
    case SS_Calendar: Calendar(false); break;
  }
}

uint8_t ScreenSavers::hourFormat12(uint8_t h)
{
  if(h > 12) return h - 12;
  if(h == 0) h = 12;
  return h;
}

// Analog clock
/*
void ScreenSavers::Clock(bool bInit)
{
  tm timeinfo;
  getLocalTime(&timeinfo);

  if(bInit)
  {
    media.loadImage("bgClock", 0, 0);
    FC.drawIcon(0, timeinfo.tm_hour, DISPLAY_WIDTH - 50); // 0 = current day, current hour, right edge pos
  }

  static uint8_t sec;
  if(sec == timeinfo.tm_sec)
    return;
  sec = timeinfo.tm_sec;

  const int16_t x = 159; // center
  const int16_t y = 158;
  const uint16_t bgColor = rgb16(9,18,9);
  uint16_t xH,yH, xM,yM, xS,yS, xS2,yS2;

//  float hrD = (timeinfo.tm_hour + timeinfo.tm_min * 0.00833) * 30;
  uint16_t hrD = (timeinfo.tm_hour * 30) + (timeinfo.tm_min / 2);
  cspoint(xH, yH, x, y, hrD, 64);
  cspoint(xM, yM, x, y, timeinfo.tm_min * 6, 87);
  cspoint(xS, yS, x, y, timeinfo.tm_sec * 6, 91);
  cspoint(xS2, yS2, x, y, (timeinfo.tm_sec+30) * 6, 24);

  tft.fillCircle(x, y, 92, bgColor ); // no sprites
  tft.drawWedgeLine(x, y, xH, yH, 10, 2, TFT_BLACK, bgColor); // hour hand
  tft.drawWedgeLine(x, y, xM, yM, 6, 2, TFT_BLACK, bgColor); // minute hand
  tft.fillCircle(x, y, 12, TFT_BLACK ); // center cap
  tft.drawWideLine(xS2, yS2, xS, yS, 2.5, rgb16(31, 0, 0), bgColor ); // second hand

/// text
  tft.setTextColor(rgb16(16,63,0) );
  tft.setFreeFont(&FreeSans12pt7b);

  String sTime = localTimeString();

  tft.fillRect(311, 17, 151, 25, rgb16(1,8,4));
  tft.drawString(sTime, 320, 20);

  sTime = _dayStr[timeinfo.tm_wday];
  sTime += "day";
  tft.drawString(sTime, 320, 60);

  sTime = _monthShortStr[timeinfo.tm_mon];
  sTime += " ";
  sTime += String(timeinfo.tm_mday);
  sTime += " ";
  sTime += String(timeinfo.tm_year+1900);
  tft.drawString(sTime, 320, 100);
}

void ScreenSavers::cspoint(uint16_t &x2, uint16_t &y2, uint16_t x, uint16_t y, uint16_t angle, uint16_t size)
{
  float ang =  M_PI * (180-angle) / 180;
  x2 = x + size * sin(ang);
  y2 = y + size * cos(ang);  
}
*/
void ScreenSavers::Lines(bool bInit)
{
  static Line *line = (Line *)m_buffer, delta;
  uint16_t color;
  static int16_t rgb[3] = {40,40,40};
  static int8_t rgbd[3] = {0};
  static uint8_t idx;

  if(bInit)
  {
    tft.fillScreen(TFT_BLACK);
    memset(line, 0, sizeof(Line) * LINES);
    memset(&delta, 1, sizeof(delta));
    idx = 0;
    return;
  }

  static int8_t dly = 0;
  if(--dly <= 0)
  {
    dly = 25; // every 25 runs
    int16_t *pd = (int16_t*)&delta;
    for(uint8_t i = 0; i < 4; i++)
      pd[i] = constrain(pd[i] + (int16_t)random(-1,2), -4, 4); // random direction delta

    for(uint8_t i = 0; i < 3; i++)
    {
      rgbd[i] += (int8_t)random(-1, 2);
      rgbd[i] = (int8_t)constrain(rgbd[i], -3, 6);
    }
  }

  uint8_t next = (idx + 1) % LINES;
  tft.drawLine(line[next].x1, line[next].y1, line[next].x2, line[next].y2, TFT_BLACK);

  // add delta to positions
  line[next].x1 = constrain(line[idx].x1 + delta.x1, 0, DISPLAY_WIDTH-1); // keep it on the screen
  line[next].x2 = constrain(line[idx].x2 + delta.x2, 0, DISPLAY_WIDTH-1);
  line[next].y1 = constrain(line[idx].y1 + delta.y1, 0, DISPLAY_HEIGHT-1);
  line[next].y2 = constrain(line[idx].y2 + delta.y2, 0, DISPLAY_HEIGHT-1);

  for(uint8_t i = 0; i < 3; i++)
  {
    rgb[i] += rgbd[i];
    rgb[i] = (int16_t)constrain(rgb[i], 1, 255);
  }

  color = tft.color565(rgb[0], rgb[1], rgb[2]);

  // Draw new line
  tft.drawLine(line[next].x1, line[next].y1, line[next].x2, line[next].y2, color);

  if(line[next].x1 == 0 && delta.x1 < 0) delta.x1 = -delta.x1; // bounce off edges
  if(line[next].x2 == 0 && delta.x2 < 0) delta.x2 = -delta.x2;
  if(line[next].y1 == 0 && delta.y1 < 0) delta.y1 = -delta.y1;
  if(line[next].y2 == 0 && delta.y2 < 0) delta.y2 = -delta.y2;
  if(line[next].x1 == DISPLAY_WIDTH-1 && delta.x1 > 0) delta.x1 = -delta.x1;
  if(line[next].x2 == DISPLAY_WIDTH-1 && delta.x2 > 0) delta.x2 = -delta.x2;
  if(line[next].y1 == DISPLAY_HEIGHT-1 && delta.y1 > 0) delta.y1 = -delta.y1;
  if(line[next].y2 == DISPLAY_HEIGHT-1 && delta.y2 > 0) delta.y2 = -delta.y2;
  idx = next;
}

void ScreenSavers::Boing(bool bInit)
{
  const int16_t rad = 12;
  static Ball *ball = (Ball *)m_buffer;
  static uint8_t skipper = 4;

  const uint16_t palette[] = {TFT_MAROON,TFT_PURPLE,TFT_OLIVE,TFT_LIGHTGREY,
        TFT_DARKGREY,TFT_BLUE,TFT_GREEN,TFT_CYAN,TFT_RED,TFT_MAGENTA,
        TFT_YELLOW,TFT_WHITE,TFT_ORANGE,TFT_GREENYELLOW,TFT_PINK,
        TFT_GOLD,TFT_SILVER,TFT_SKYBLUE,TFT_VIOLET};

  if(bInit)
  {
    tft.fillScreen(TFT_BLACK);
    for(uint8_t i = 0; i < BALLS; i++)
    {
      ball[i].x = DISPLAY_WIDTH/2;
      ball[i].y = rad;
      ball[i].dx = (int16_t)random(-2, 5);
      ball[i].dy = (int16_t)random(3, 6);
      ball[i].color = palette[ random(0, sizeof(palette) / sizeof(uint16_t) ) ];
    }
    return;
  }

  if(--skipper) // slow it down by x*loop delay of 2ms
    return;
  skipper = 8;

  static double  bounce = 1.0;
  static double  energy = 0.6;

  // bouncy
  for(uint8_t i = 0; i < BALLS; i++)
  {
    int x1 = ball[i].x;
    int y1 = ball[i].y;

    // check for wall collision
    if(y1 <= rad && ball[i].dy < 0)   // top of screen
      ball[i].dy = -ball[i].dy + (int16_t)random(0, 4);

    if(y1 >= DISPLAY_HEIGHT - rad && ball[i].dy > 0) // bottom
      ball[i].dy = -ball[i].dy - (int16_t)random(5, 17);

    if(x1 <= rad && ball[i].dx < 0)  // left wall
      ball[i].dx = -ball[i].dx;

    if(x1 >= DISPLAY_WIDTH - rad && ball[i].dx > 0)  // right wall
      ball[i].dx = -ball[i].dx;

    static uint8_t dly = 1;
    if(--dly == 0) // gravity
    {
      ball[i].dy++;
      dly = 1;
    }
    if(ball[i].dx < 2 && ball[i].dx > -2)
      ball[i].dx += (int16_t)random(-2, 4); // keep balls from sitting still

    if(ball[i].dy < 2 && ball[i].dy > -2)
      ball[i].dy += (int16_t)random(-2, 4); // keep balls from sitting still

    // check for ball to ball collision
    for(uint8_t i2 = i+1; i2 < BALLS; i2++)
    {
      double n, n2;
      int x2, y2;
  
      x2 = ball[i2].x;
      y2 = ball[i2].y;
      if(x1 + rad >= x2 && x1 <= x2 + rad && y1 + rad >= y2 && y1 <= y2 + rad){
        if(x1-x2 > 0){
          n = ball[i].dx;
          n2 = ball[i2].dx;
          ball[i].dx = ((n *bounce) + (n2 * energy));
          ball[i2].dx = -((n2 * bounce) - (n*energy));
        }else{
          n = ball[i].dx;
          n2 = ball[i2].dx;
          ball[i].dx = -((n * bounce) - (n2 * energy));
          ball[i2].dx = ((n2 * bounce) + (n * energy));
        }
        if(y1-y2 > 0){
          n = ball[i].dy;
          ball[i].dy = ((n * bounce) + (ball[i2].dy * energy));
          ball[i2].dy = -((ball[i2].dy * bounce) - (n * energy));
        }else{
          n = ball[i].dy;
          ball[i].dy = -((n * bounce) - (ball[i2].dy * energy));
          ball[i2].dy = ((ball[i2].dy * bounce) + (n * energy));
        }
      }
    }

    ball[i].dx = constrain(ball[i].dx, -20, 40); // speed limit
    ball[i].dy = constrain(ball[i].dy, -20, 40);
  }

  // Erase last balls
  for(uint8_t i = 0; i < BALLS; i++)
  {
    tft.drawCircle(ball[i].x, ball[i].y, rad, TFT_BLACK );
  }
  
  // Draw balls
  for(uint8_t i = 0; i < BALLS; i++)
  {
    ball[i].x += ball[i].dx;
    ball[i].y += ball[i].dy;

    // Draw at new positions
    tft.drawCircle(ball[i].x, ball[i].y, rad, ball[i].color );
  }
}

void ScreenSavers::Calendar(bool bInit)
{
  const uint16_t xOffset = 60;
  const uint16_t width = DISPLAY_WIDTH - (xOffset * 2);
  const uint16_t yOffset = 68;

  if(bInit)
  {
    tm timeinfo;
    getLocalTime(&timeinfo);

    uint8_t month_days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if(timeinfo.tm_year % 4 == 0) month_days[1] = 29;

    uint8_t day = timeinfo.tm_mday;
    timeinfo.tm_mday = 1; // day of month = 14
    time_t tt = mktime(&timeinfo);
    tm *tmWd = gmtime( &tt ); // get wday of 1st

    uint8_t firstDay = timeinfo.tm_wday;
    uint8_t lastDay = month_days[ timeinfo.tm_mon ];
    uint8_t rows = (firstDay + lastDay + 6) / 7;

    tft.fillRect(0, 0, DISPLAY_WIDTH, 34, rgb16(24,48,24)); // fill 3 areas
    tft.fillRect(0, 34, DISPLAY_WIDTH, 34, rgb16(9,18,24));
    tft.fillRect(0, yOffset, DISPLAY_WIDTH, DISPLAY_HEIGHT - yOffset, TFT_BLACK);

    tft.setTextDatum(TC_DATUM);
    tft.setFreeFont(&FreeSans12pt7b);
    tft.setTextColor( TFT_BLACK, rgb16(24,48,24) );

    // Top bar
    String s = _monthStr[ timeinfo.tm_mon ]; s += " "; s += timeinfo.tm_year+1900;
    tft.drawString(s, DISPLAY_WIDTH/2, 5);

    // Weekday bar
    tft.setTextColor( rgb16(0, 63, 31), rgb16(9,18,24) );
    for (uint8_t dayNum = 0; dayNum < 7; ++dayNum)
      tft.drawString( _dayShortStr[dayNum], dayNum * (width / 7) + xOffset + 22, 40 );

    // Days
    uint8_t digit = 1;
    uint8_t curCell = 0;
  
    for (uint8_t row = 1; row <= rows; row++)
    {
      for (uint8_t col = 0; col < 7; col++)
      {
        if (digit > lastDay) break;
        if (curCell < firstDay)
        {
          curCell++;
        }
        else
        {
          uint16_t x = col * (width / 7) + xOffset;
          uint16_t y = row * ((DISPLAY_HEIGHT - yOffset - 3)/ rows) + yOffset - 34;

          // rounded box around each day
          uint16_t boxColor = (digit == day) ? rgb16(20, 40, 10) : rgb16(10, 20, 10);
          tft.fillRoundRect( x, y-10, 44, 40, 5, boxColor);
          uint16_t txtColor = rgb16(0, 63, 31); // cyan
          tft.setTextColor( txtColor, boxColor );
          tft.drawString( String(digit), x+22, y);
          digit++;
        }
      }
    }

    tft.setTextDatum(TL_DATUM);
  }
}
