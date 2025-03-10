#ifndef MEDIA_H
#define MEDIA_H

#include <Arduino.h>
#include <FS.h>
#include <PNGdec.h>  // From Library Manager

#include <SPIFFS.h> // SPIFFS has no subdirs but takes less code space

#define INTERNAL_FS SPIFFS // Make it the same as the partition scheme uploaded (SPIFFS, LittleFS, FFat)

struct ImageCtrl
{
  int16_t x;
  int16_t y;
  int16_t srcX;
  int16_t srcY;
  int16_t w;
  int16_t h;
};

class Media
{
public:
  Media(void);
  void init(void);

  void deleteFile(char *pszFilename);
  fs::File createFile(String sFilename); //only ESP32 needs this namespace decl
  void createDir(char *pszName);
  void setPath(char *szPath);
  uint32_t freeSpace(void);

  void loadImage(String Name, uint16_t x, uint16_t y);
  void loadImage(String Name, uint16_t x, uint16_t y, uint16_t srcX, uint16_t srcY, uint16_t w, uint16_t h);
};

extern Media media;

#endif // MEDIA_H
