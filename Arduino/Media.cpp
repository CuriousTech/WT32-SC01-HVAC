#include "HVAC.h"
#include "Media.h"
#include "screensavers.h"

extern void WsSend(String s);

uint16_t pngbuffer[512]; // 512 pixel width max
File pngfile;
PNG png;

void *pngOpen(const char *filename, int32_t *size) {
  pngfile = INTERNAL_FS.open(filename, "r");
  *size = pngfile.size();
  return &pngfile;
}

void pngClose(void *handle) {
  File pngfile = *((File*)handle);
  if (pngfile) pngfile.close();
}

int32_t pngRead(PNGFILE *page, uint8_t *buffer, int32_t length) {
  if (!pngfile) return 0;
  page = page; // Avoid warning
  return pngfile.read(buffer, length);
}

int32_t pngSeek(PNGFILE *page, int32_t position) {
  if (!pngfile) return 0;
  page = page; // Avoid warning
  return pngfile.seek(position);
}

void pngDraw(PNGDRAW *pDraw)
{
  ImageCtrl *pPos = (ImageCtrl *)pDraw->pUser;

  if(pPos->srcY)
  {
    if(pDraw->y < pPos->srcY) // skip lines above
      return;
    if(pPos->srcY + pPos->h >= pDraw->y) // skip lines below
      return;
  }

  png.getLineAsRGB565(pDraw, pngbuffer, PNG_RGB565_BIG_ENDIAN, 0xffffffff);

  uint16_t w = pDraw->iWidth;
  if(pPos->w) w = pPos->w; // crop to desired width (todo: should maybe do some checking)

  uint16_t y = pPos->y + pDraw->y;
  if(pPos->srcY) // offset lib incremented y pos to srcY offset
    y -= pPos->srcY;

  tft.pushImage(pPos->x, y, w, 1, pngbuffer + pPos->srcX);
}

Media::Media()
{ 
}

void Media::init()
{
//  INTERNAL_FS.begin(true); // started in eemem
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

void Media::loadImage(String Name, uint16_t x, uint16_t y)
{
  loadImage(Name, x, y, 0, 0, 0, 0);
}

void Media::loadImage(String Name, uint16_t x, uint16_t y, uint16_t srcX, uint16_t srcY, uint16_t w, uint16_t h)
{
  static ImageCtrl pos;
  pos.x = x;
  pos.y = y;
  pos.srcX = srcX;
  pos.srcY = srcY;
  pos.w = w;
  pos.h = h;

  String sPath = "/";
  sPath += Name;

  int16_t rc = png.open(sPath.c_str(), pngOpen, pngClose, pngRead, pngSeek, pngDraw);

  if(rc == PNG_SUCCESS)
  {
    tft.startWrite();
//    Serial.printf("image specs: (%d x %d), %d bpp, pixel type: %d\r\n", png.getWidth(), png.getHeight(), png.getBpp(), png.getPixelType());
//    uint32_t dt = millis();
    rc = png.decode((void*)&pos, 0);
    png.close();
    tft.endWrite();
    // How long did rendering take...
//    Serial.print(millis()-dt); Serial.println("ms");
  }
  else
  {
//    Serial.print(Name);
//    Serial.print(" loadImage error: ");
//    Serial.println(rc);
  }
}
