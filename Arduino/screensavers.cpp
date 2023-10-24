#include "screensavers.h"

void ScreenSavers::init()
{
  
}

void ScreenSavers::select(int n)
{
  m_saver = n;
}

void ScreenSavers::reset()
{
  randomSeed(micros());
  tft.fillScreen(TFT_BLACK);

  switch(m_saver)
  {
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
      case SS_Lines: Lines(false); break;
      case SS_Boing: Boing(false); break;
      case SS_Cube: Cube(false); break;
    }
}

void ScreenSavers::end()
{
    switch(m_saver)
    {
      case SS_Lines: break;
      case SS_Boing: break;
      case SS_Cube:  break;
    }
  
}

void ScreenSavers::Lines(bool bInit)
{
#define LINES 50
  static intLine line[LINES], delta;
  uint16_t color;
  static int16_t r=40, g=40, b=40;
  static int8_t cnt = 0;

  if(bInit)
  {
    memset(&line, 10, sizeof(line));
    memset(&delta, 1, sizeof(delta));
    return;
  }

  // Erase oldest line
  tft.drawLine(line[LINES - 1].x1, line[LINES - 1].y1, line[LINES - 1].x2, line[LINES - 1].y2, 0);

  // FIFO the lines
  for(int i = LINES - 2; i >= 0; i--)
    line[i+1] = line[i];

  if(--cnt <= 0)
  {
    cnt = 5; // every 5 runs
    delta.x1 = constrain(delta.x1 + (int16_t)random(-1,2), -4, 4); // random direction delta
    delta.x2 = constrain(delta.x2 + (int16_t)random(-1,2), -4, 4);
    delta.y1 = constrain(delta.y1 + (int16_t)random(-1,2), -4, 4);
    delta.y2 = constrain(delta.y2 + (int16_t)random(-1,2), -4, 4);
  }

  // add delta to positions
  line[0].x1 = constrain(line[0].x1 + delta.x1, 0, DISPLAY_WIDTH-1); // keep it on the screen
  line[0].x2 = constrain(line[0].x2 + delta.x2, 0, DISPLAY_WIDTH-1);
  line[0].y1 = constrain(line[0].y1 + delta.y1, 0, DISPLAY_HEIGHT-1);
  line[0].y2 = constrain(line[0].y2 + delta.y2, 0, DISPLAY_HEIGHT-1);

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

  tft.drawLine(line[0].x1, line[0].y1, line[0].x2, line[0].y2, color); // draw the new line

  if(line[0].x1 == 0 && delta.x1 < 0) delta.x1 = -delta.x1; // bounce off edges
  if(line[0].x2 == 0 && delta.x2 < 0) delta.x2 = -delta.x2;
  if(line[0].y1 == 0 && delta.y1 < 0) delta.y1 = -delta.y1;
  if(line[0].y2 == 0 && delta.y2 < 0) delta.y2 = -delta.y2;
  if(line[0].x1 == DISPLAY_WIDTH-1 && delta.x1 > 0) delta.x1 = -delta.x1;
  if(line[0].x2 == DISPLAY_WIDTH-1 && delta.x2 > 0) delta.x2 = -delta.x2;
  if(line[0].y1 == DISPLAY_HEIGHT-1 && delta.y1 > 0) delta.y1 = -delta.y1;
  if(line[0].y2 == DISPLAY_HEIGHT-1 && delta.y2 > 0) delta.y2 = -delta.y2;
}

void ScreenSavers::Boing(bool bInit)
{
#define BALLS 8
  const int16_t rad = 12;
  static Ball ball[BALLS];
  static uint8_t skipper = 4;

  const uint16_t palette[] = {TFT_MAROON,TFT_PURPLE,TFT_OLIVE,TFT_LIGHTGREY,
        TFT_DARKGREY,TFT_BLUE,TFT_GREEN,TFT_CYAN,TFT_RED,TFT_MAGENTA,
        TFT_YELLOW,TFT_WHITE,TFT_ORANGE,TFT_GREENYELLOW,TFT_PINK,
        TFT_GOLD,TFT_SILVER,TFT_SKYBLUE,TFT_VIOLET};

  if(bInit)
  {
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
      ball[i].dx = -ball[i].dx - (int16_t)random(0, 1);

    if(x1 >= DISPLAY_WIDTH - rad && ball[i].dx > 0)  // right wall
      ball[i].dx = -ball[i].dx + (int16_t)random(0, 1);

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
  static Line3d object[] = {
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
 
  static Line2d Render[20];
  static Line2d ORender[20];

  if(bInit)
  {
    Xpos = tft.width() / 2; // Position the center of the 3d conversion space into the center of the TFT screen.
    Ypos = tft.height() / 2;
    Zpos = 550; // Z offset in 3D space (smaller = closer and bigger rendering)
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

  for (int i = 0; i < LinestoRender; i++ )
  {
    uint16_t color = TFT_BLUE;
    if (i < 4) color = TFT_RED;
    if (i > 7) color = TFT_GREEN;
    tft.drawLine(Render[i].p0.x, Render[i].p0.y, Render[i].p1.x, Render[i].p1.y, color);
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

  int x1;
  int y1;
  int z1;

  int x2;
  int y2;
  int z2;

  bool Ok;

  x1 = vec.p0.x;
  y1 = vec.p0.y;
  z1 = vec.p0.z;

  x2 = vec.p1.x;
  y2 = vec.p1.y;
  z2 = vec.p1.z;

  Ok = false; // defaults to not OK

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
