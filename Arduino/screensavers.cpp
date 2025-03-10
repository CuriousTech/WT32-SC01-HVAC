#include "screensavers.h"
#include <TimeLib.h>
#include "digitalFont.h"
#include "display.h"
#include "forecast.h"
#include "Media.h"

extern Forecast FC;

extern void WsSend(String s);

void ScreenSavers::select(int n)
{
  m_saver = n;
  randomSeed(micros());

  switch(m_saver)
  {
    case SS_Clock:
        Clock(true);
        break;
    case SS_Lines:
        Lines(true);
        break;
    case SS_Boing:
        Boing(true);
        break;
  }
}

void ScreenSavers::run()
{
    switch(m_saver)
    {
      case SS_Clock: Clock(false); break;
      case SS_Lines: Lines(false); break;
      case SS_Boing: Boing(false); break;
    }
}

// Analog clock
void ScreenSavers::Clock(bool bInit)
{
  if(bInit)
  {
    media.loadImage("bgClock.png", 0, 0);
    FC.drawIcon(0, hour(), DISPLAY_WIDTH - 50); // 0 = current day, current hour, right edge pos
  }

  static uint8_t sec;
  if(sec == second())
    return;
  sec = second();

  const float x = 159; // center
  const float y = 158;
  const uint16_t bgColor = rgb16(9,18,9);
  uint16_t xH,yH, xM,yM, xS,yS, xS2,yS2;

  float a = (hour() + (minute() * 0.00833) ) * 30;
  cspoint(xH, yH, x, y, a, 64);
  cspoint(xM, yM, x, y, minute() * 6, 87);
  cspoint(xS, yS, x, y, second() * 6, 91);
  cspoint(xS2, yS2, x, y, (second()+30) * 6, 24);

  tft.fillCircle(x, y, 92, bgColor ); // no sprites
  tft.drawWedgeLine(x, y, xH, yH, 10, 2, TFT_BLACK, bgColor); // hour hand
  tft.drawWedgeLine(x, y, xM, yM, 6, 2, TFT_BLACK, bgColor); // minute hand
  tft.fillCircle(x, y, 12, TFT_BLACK ); // center cap
  tft.drawWideLine(xS2, yS2, xS, yS, 2.5, rgb16(31, 0, 0), bgColor ); // second hand

/// text
  tft.setTextColor(rgb16(16,63,0) );
  tft.setFreeFont(&FreeSans12pt7b);

  String sTime = (hourFormat12() < 10) ? " ":"";
  sTime += String( hourFormat12() );
  sTime += ":";
  if(minute() < 10) sTime += "0";
  sTime += minute();
  sTime += ":";
  if(second() < 10) sTime += "0";
  sTime += second();
  sTime += " ";
  sTime += isPM() ? "PM":"AM";

  tft.fillRect(311, 17, 151, 25, rgb16(1,8,4));
  tft.drawString(sTime, 320, 20);

  tft.drawString(dayStr(weekday()), 320, 60);

  sTime = monthShortStr(month());
  sTime += " ";
  sTime += String(day());
  sTime += " ";
  sTime += String(year());
  tft.drawString(sTime, 320, 100);
}

void ScreenSavers::cspoint(uint16_t &x2, uint16_t &y2, uint16_t x, uint16_t y, float angle, float size)
{
  float ang =  M_PI * (180-angle) / 180;
  x2 = x + size * sin(ang);
  y2 = y + size * cos(ang);  
}

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
