#ifndef SCREENSAVERS_H
#define SCREENSAVERS_H

#include <Arduino.h>
#include <TFT_eSPI.h>
extern TFT_eSPI tft;

#define DISPLAY_WIDTH 480
#define DISPLAY_HEIGHT 320

enum Saver
{
  SS_Clock,
  SS_Lines,
  SS_Boing,
  SS_Cube,
  SS_Count, // Last one
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
#define LINES 50
#define BALLS 8
#define LINES3DREND 20 // max 170 for 2048 bytes
#define BUFFER_SIZE 1024 // Caution: Also used by image loader (512 pixel width max)

static_assert(sizeof(Ball) * BALLS < BUFFER_SIZE, "m_buffer size too small");
static_assert(sizeof(Line) * LINES < BUFFER_SIZE, "m_buffer size too small");
static_assert(sizeof(Line2d) * 2 * LINES3DREND < BUFFER_SIZE, "m_buffer size too small");

public:
  ScreenSavers(){};
  void select(int n);
  void run(void);

  uint8_t m_saver;
  uint8_t m_buffer[BUFFER_SIZE];

private:
  void Clock(bool bInit);
  void cspoint(uint16_t &x2, uint16_t &y2, uint16_t x, uint16_t y, float angle, float size);
  void Lines(bool bInit);
  void Boing(bool bInit);
  void Cube(bool bInit);
  void SetVars(void);
  void Transform(struct Line2d *ret, struct Line3d vec);

  float xx, xy, xz;
  float yx, yy, yz;
  float zx, zy, zz;
  
  int Xan, Yan;
  
  int Xpos;
  int Ypos;
  int Zpos;
};


#endif // SCREENSAVERS_H
