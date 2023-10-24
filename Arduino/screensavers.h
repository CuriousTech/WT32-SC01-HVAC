#ifndef SCREENSAVERS_H
#define SCREENSAVERS_H

#include <Arduino.h>
#include <TFT_eSPI.h>
extern TFT_eSPI tft;

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 320

enum Saver
{
  SS_Lines,
  SS_Boing,
  SS_Cube,
  SS_Count, // Last one
};

struct intLine
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

struct Point3d
{
  int x;
  int y;
  int z;
};

struct Point2d
{
  int x;
  int y;
};

struct Line3d
{
  Point3d p0;
  Point3d p1;
};

struct Line2d
{
  Point2d p0;
  Point2d p1;
};

class ScreenSavers
{
public:
  ScreenSavers(){};
  void init(void);
  void select(int n);
  void reset(void);
  void run(void);
  void end(void);
private:
  void Lines(bool bInit);
  void Boing(bool bInit);
  void Cube(bool bInit);
  void SetVars(void);
  void Transform(struct Line2d *ret, struct Line3d vec);

  uint8_t m_saver;

  float xx, xy, xz;
  float yx, yy, yz;
  float zx, zy, zz;
  
  int Xan, Yan;
  
  int Xpos;
  int Ypos;
  int Zpos;

};


#endif // SCREENSAVERS_H
