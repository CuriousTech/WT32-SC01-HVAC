#include "HVAC.h"
#include "Media.h"
#include "screensavers.h"

extern void WsSend(String s);
int16_t _srcX, _srcY;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if ( y >= tft.height() ) return 0; // Stop further decoding as image is running off bottom of screen
  if(_srcY)
  {
    if(y < _srcY) // skip lines above
      return 1;
    if(_srcY + h >= y) // skip lines below
      return 1;
  }
  if(_srcY) // offset lib incremented y pos to srcY offset
    y -= _srcY;
  if( y < _srcY) return 1; // skip source lines not rendered
  // This function will clip the image block rendering automatically at the TFT boundaries
  sprite.pushImage(x, y, w, h, bitmap + _srcX);
  return 1;  // Return 1 to decode next block
}

Media::Media()
{ 
}

void Media::init()
{
//  INTERNAL_FS.begin(true); // started in eemem
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);  // The byte order can be swapped (set true for TFT_eSPI)
  TJpgDec.setCallback(tft_output);   // The decoder must be given the exact name of the rendering function above
}

uint32_t Media::freeSpace()
{
  return INTERNAL_FS.totalBytes() - INTERNAL_FS.usedBytes();
}

void Media::setPath(char *szPath) // SPIFFS has no dirs
{
  File Path = INTERNAL_FS.open("/");
  if (!Path)
    return;

  uint16_t fileCount = 0;
  File file = Path.openNextFile();

  String s = "{\"cmd\":\"files\",\"list\":[";

  while (file)
  {
    if(fileCount)
      s += ",";
    s += "[\"";
    s += file.name();
    s += "\",";
    s += file.size();
    s += ",";
    s += file.isDirectory();
    s += ",";
    s += file.getLastWrite();
    s += "]";
    fileCount++;
    file = Path.openNextFile();
  }
  Path.close();
  s += "]}";
  WsSend(s);
}

void Media::deleteFile(char *pszFilename)
{
  INTERNAL_FS.remove(pszFilename);
}

File Media::createFile(String sFilename)
{
  return INTERNAL_FS.open(sFilename, "w");
}

void Media::createDir(char *pszName)
{
  if( !INTERNAL_FS.exists(pszName) )
    INTERNAL_FS.mkdir(pszName);
}

void Media::loadImage(String sName, uint16_t x, uint16_t y)
{
  loadImage(sName, x, y, 0, 0, 0, 0);
}

void Media::loadImage(String sName, uint16_t x, uint16_t y, int16_t srcX, int16_t srcY, uint16_t w, uint16_t h)
{
  _srcX = srcX;
  _srcY = srcY;

  String sPath = "/";
  sPath += sName;
  sPath += ".jpg";
  uint16_t wReal, hReal;
  TJpgDec.getFsJpgSize(&wReal, &hReal, sPath);
  if(w == 0) w = wReal;
  if(h == 0) h = hReal;
  if(w == 0 || h == 0)
  {
//    String s = "File not found: ";
//    s += sPath;
//    Serial.println(s);
    return;
  }
  TJpgDec.drawFsJpg(x, y, sPath);
}
