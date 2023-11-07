#include "screensavers.h"
#include <TimeLib.h>
#include "digitalFont.h"
#include "display.h"

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
    case SS_Cube:
        Cube(true);
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
      case SS_Cube: Cube(false); break;
    }
}

// Analog clock
void ScreenSavers::Clock(bool bInit)
{
  if(bInit)
    display.loadImage("/bgClock.png", 0, 0);

  static uint8_t sec;
  if(sec == second())
    return;
  sec = second();

  const float x = 159; // center
  const float y = 158;
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
  cspoint(x2, y2, x, y, (minute()+5) * 6, 12);
  cspoint(x3, y3, x, y, (minute()-5) * 6, 12);
  tft.fillTriangle(x1,y1, x2,y2, x3, y3, TFT_BLACK ); // minute hand

  tft.fillCircle(x, y, 12, TFT_BLACK ); // center cap

  cspoint(x2, y2, x, y, second() * 6, 91);
  cspoint(x3, y3, x, y, (second()+30) * 6, 24);
  tft.drawWideLine(x3, y3, x2, y2, 2.5, rgb16(31, 0, 0), bgColor ); // second hand

  tft.setTextColor(rgb16(16,63,0) );
  tft.setFreeFont(&FreeSans12pt7b);//&digitaldreamFatNarrow_14ptFont);

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

void ScreenSavers::cspoint(float &x2, float &y2, float x, float y, float angle, float size)
{
  float ang =  M_PI * (180-angle) / 180;
  x2 = x + size * sin(ang);
  y2 = y + size * cos(ang);  
}


void ScreenSavers::Lines(bool bInit)
{
  static Line *line = (Line *)m_buffer, delta;
  uint16_t color;
  static int16_t r=40, g=40, b=40;
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
    dly = 5; // every 5 runs
    delta.x1 = constrain(delta.x1 + (int16_t)random(-1,2), -4, 4); // random direction delta
    delta.x2 = constrain(delta.x2 + (int16_t)random(-1,2), -4, 4);
    delta.y1 = constrain(delta.y1 + (int16_t)random(-1,2), -4, 4);
    delta.y2 = constrain(delta.y2 + (int16_t)random(-1,2), -4, 4);
  }

  uint8_t next = (idx + 1) % LINES;
  tft.drawLine(line[next].x1, line[next].y1, line[next].x2, line[next].y2, 0);

  // add delta to positions
  line[next].x1 = constrain(line[idx].x1 + delta.x1, 0, tft.width()-1); // keep it on the screen
  line[next].x2 = constrain(line[idx].x2 + delta.x2, 0, DISPLAY_WIDTH-1);
  line[next].y1 = constrain(line[idx].y1 + delta.y1, 0, DISPLAY_HEIGHT-1);
  line[next].y2 = constrain(line[idx].y2 + delta.y2, 0, DISPLAY_HEIGHT-1);

  r += (int16_t)random(-1, 2);
  if(r > 255) r = 255;
  else if(r < 1) r = 1;
  g += (int16_t)random(-1, 2);
  if(g > 255) g = 255;
  else if(g < 1) g = 1;
  b += (int16_t)random(-1, 2);
  if(b > 255) b = 255;
  else if(b < 1) b = 1;
  color = tft.color565(r, g, b);

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

// 3D polygon code yanked from https://github.com/seaniefs/WT32-SC01-Exp

void ScreenSavers::Cube(bool bInit)
{
  /***********************************************************************************************************************************/
  // line segments to draw a cube. basically p0 to p1. p1 to p2. p2 to p3 so on.
  static const Line3d object[] = {
    // Front Face.
    {-50, -50,  50,  50, -50,  50},
    { 50, -50,  50,  50,  50,  50},
    { 50,  50,  50, -50,  50,  50},
    {-50,  50,  50, -50, -50,  50},
  
    //back face.
    {-50, -50, -50,  50, -50, -50},
    { 50, -50, -50,  50,  50, -50},
    { 50,  50, -50, -50,  50, -50},
    {-50,  50, -50, -50, -50, -50},
  
    // now the 4 edge lines.
    {-50, -50, 50, -50, -50, -50},
    { 50, -50, 50,  50, -50, -50},
    {-50,  50, 50, -50,  50, -50},
    { 50,  50, 50,  50,  50, -50},
  };
  uint8_t LinestoRender = 12; // lines in the object

  static Line2d *Render = (Line2d *)m_buffer;
  static Line2d *ORender = (Line2d *)m_buffer + (sizeof(Line2d) * LINES3DREND);

  if(bInit)
  {
    tft.fillScreen(TFT_BLACK);
    Xpos = tft.width() / 2; // Position the center of the 3d conversion space into the center of the TFT screen.
    Ypos = tft.height() / 2;
    Zpos = 550; // Z offset in 3D space (smaller = closer and bigger rendering)
    memset(m_buffer, 0, sizeof(m_buffer));
    return;
  }

  static uint8_t skipper = 4;
  if(--skipper)
      return;
  skipper = 4;

  static int8_t Zinc = -2, Xinc = -1, Yinc = -1, XanInc = 1, YanInc = 1;

  // Rotate around x and y axes in 1 degree increments
  Xan += XanInc;
  Yan += YanInc;

  Yan = Yan % 360;
  Xan = Xan % 360; // prevents overflow.

  SetVars(); //sets up the global vars to do the 3D conversion.

  // Zoom in and out on Z axis within limits
  // the cube intersects with the screen for values < 160
  Zpos += Zinc;
  if (Zpos > 700 && Zinc > 0) Zinc = -(Zinc + random(-1, 4));     // Switch to zoom in
  else if (Zpos < 230 && Zinc < 0 ) Zinc = -(Zinc + random(-1, 4)); // Switch to zoom out
  Zinc = constrain(Zinc, -4, 8);

  Xpos += Xinc;
  if(Xpos >= tft.width() - 40 && Xinc > 0 )
    Xinc = -(constrain(Xinc + random(-1, 4 ), 1, 12) );
  else if(Xpos <= 40 && Xinc <= 0)
  {
    Xinc = -(Xinc + random(1, 3 ));
    XanInc = random(-2, 4);
  }
  Ypos += Yinc;
  if(Ypos >= tft.height() - 40 && Yinc > 0)
    Yinc = -(constrain(Yinc + random(-1, 4), 1, 12) );
  else if(Ypos <= 40 && Yinc <= 0){
    Yinc = -(Yinc + random(1, 4));
    YanInc = random(-2, 4);
  }
  if(Xinc == 0) Xinc = random(-1, 3);
  if(Yinc == 0) Yinc = random(-1, 3);
  if(Zinc == 0) Zinc = random(-1, 3);

  for (int i = 0; i < LinestoRender ; i++)
  {
    ORender[i] = Render[i]; // stores the old line segment so we can erase it later.
    Transform(&Render[i], object[i]); // converts the 3d line segments to 2d.
  }

  // render all the lines after erasing the old ones.
  for (int i = 0; i < LinestoRender; i++ )
  {
    tft.drawLine(ORender[i].p0.x, ORender[i].p0.y, ORender[i].p1.x, ORender[i].p1.y, TFT_BLACK); // erase the old lines.
  }

  uint8_t r = 7, g = 63, b = 19;
  for (int i = 0; i < LinestoRender; i++ )
  {
    uint16_t color = rgb16(r, g, b);
    tft.drawLine(Render[i].p0.x, Render[i].p0.y, Render[i].p1.x, Render[i].p1.y, color);
    g -= 2;
    b ++;
  }
}

/***********************************************************************************************************************************/
// Sets the global vars for the 3d transform. Any points sent through "process" will be transformed using these figures.
// only needs to be called if Xan or Yan are changed.
void ScreenSavers::SetVars()
{
  const float fact = 180 / 3.14159259;
  float Xan2, Yan2, Zan2;
  float s1, s2, s3, c1, c2, c3;

  Xan2 = Xan / fact; // convert degrees to radians.
  Yan2 = Yan / fact;

  // Zan is assumed to be zero

  s1 = sin(Yan2);
  s2 = sin(Xan2);

  c1 = cos(Yan2);
  c2 = cos(Xan2);

  xx = c1;
  xy = 0;
  xz = -s1;

  yx = (s1 * s2);
  yy = c2;
  yz = (c1 * s2);

  zx = (s1 * c2);
  zy = -s2;
  zz = (c1 * c2);
}

/***********************************************************************************************************************************/
// processes x1,y1,z1 and returns rx1,ry1 transformed by the variables set in SetVars()
// fairly heavy on floating point here.
// uses a bunch of global vars. Could be rewritten with a struct but not worth the effort.
void ScreenSavers::Transform(struct Line2d *ret, struct Line3d vec)
{
  float zvt1;
  int xv1, yv1, zv1;

  float zvt2;
  int xv2, yv2, zv2;

  int rx1, ry1;
  int rx2, ry2;

  int x1 = vec.p0.x;
  int y1 = vec.p0.y;
  int z1 = vec.p0.z;

  int x2 = vec.p1.x;
  int y2 = vec.p1.y;
  int z2 = vec.p1.z;

  bool Ok = false; // defaults to not OK

  xv1 = (x1 * xx) + (y1 * xy) + (z1 * xz);
  yv1 = (x1 * yx) + (y1 * yy) + (z1 * yz);
  zv1 = (x1 * zx) + (y1 * zy) + (z1 * zz);

  zvt1 = zv1 - Zpos;

  if ( zvt1 < -5) {
    rx1 = 256 * (xv1 / zvt1) + Xpos;
    ry1 = 256 * (yv1 / zvt1) + Ypos;
    Ok = true; // ok we are alright for point 1.
  }

  xv2 = (x2 * xx) + (y2 * xy) + (z2 * xz);
  yv2 = (x2 * yx) + (y2 * yy) + (z2 * yz);
  zv2 = (x2 * zx) + (y2 * zy) + (z2 * zz);

  zvt2 = zv2 - Zpos;

  if ( zvt2 < -5) {
    rx2 = 256 * (xv2 / zvt2) + Xpos;
    ry2 = 256 * (yv2 / zvt2) + Ypos;
  } else
  {
    Ok = false;
  }

  if (Ok) {
    ret->p0.x = rx1;
    ret->p0.y = ry1;

    ret->p1.x = rx2;
    ret->p1.y = ry2;
  }
}
