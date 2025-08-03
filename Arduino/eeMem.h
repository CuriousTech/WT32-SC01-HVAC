#ifndef EEMEM_H
#define EEMEM_H

#include <Arduino.h>

struct flags_t
{
  uint16_t Mode:3;
  uint16_t heatMode:2;
  uint16_t humidMode:3;
  uint16_t nSchedMode:3; // 0=forecast, 1=sine, 2=flat
  uint16_t nFcstSource:2; // 0=local, 1=OpenWeatherMap
  uint16_t bCelcius:1;
  uint16_t bLock:1;
  uint16_t res:1;
};

#define EESIZE (offsetof(eeMem, end) - offsetof(eeMem, size) )

class eeMem
{
public:
  eeMem(){};
  bool init(void);
  bool check(void);
  bool update(void);
  uint16_t getSum(void);
  uint16_t Fletcher16( uint8_t* data, int count);

public:
  uint16_t size = EESIZE;            // if size changes, use defaults
  uint16_t sum = 0xAAAA;             // if sum is different from memory struct, write
  char     szSSID[24] = "";  // Enter you WiFi router SSID
  char     szSSIDPassword[24] = ""; // and password
  uint16_t coolTemp[2] = {850, 860}; // cool to temp *10 low/high F/C issue
  uint16_t heatTemp[2] = {740, 750}; // heat to temp *10 low/high F/C issue
  int8_t   cycleThresh[2] = {28, 8}; // temp range for cycle *10 [cool|heat] F/C issue
  flags_t  b =  {0,0,0,0,0,0,0,0};   // see flags_t
  uint8_t  eHeatThresh = 20;        // degree threshold to switch to gas    F/C issue
  int8_t   tz = -5;                // current timezone
  int8_t   calib;                   // temp sensor offset adjust by 0.1
  uint16_t cycleMin = 60*4;         // min time to run a cycle in seconds
  uint16_t cycleMax = 60*30;        // max time to run a cycle in seconds
  uint16_t idleMin = 60*8;          // min time to not run in seconds
  uint16_t fanPostDelay[2] = {60*2, 60*2}; // delay to run auto fan after [hp/cool] stops
  uint16_t fanPreTime[2] = {60*1, 60*1}; // fan pre-run before [cool/heat]
  uint16_t overrideTime = 60*10;    // time used for an override in seconds
  uint16_t rhLevel[2] = {450, 750}; // rh low/high 45%, 75%
  int8_t   awayDelta[2] = {40, -40}; // temp offset in away mode[cool][heat] by 0.1
  uint16_t awayTime = 60*8;         // time limit for away offset (in seconds)
  uint8_t  hostIp[4] = {192,168,31,100}; // Device to read local forecast info
  uint16_t hostPort = 80;
  char     cityID[8] = "4291945";   // For OpenWeatherMap  4311646
  char     password[24] = "password"; // Web interface password
  uint8_t  fcRange = 23;            // number in forecasts (3 hours)
  uint8_t  fcDisplay = 46;          // number in forecasts (3 hours)
  uint8_t  fanAutoRun = 5;          // 5 minutes on
  uint8_t  fanWatts = 250;          // Blower motor watts
  uint8_t  furnaceWatts = 140;      // 1.84A inducer motor mostly
  uint8_t  humidWatts = 150;
  uint8_t  brightLevel[2] = {30, 100}; // brightness {dim, highest}
  uint16_t ppkwh = 150;             // price per KWH in cents * 10000 ($0.15)
  uint16_t ccf = 1190;              // nat gas cost per 1000 cubic feet in 10th of cents * 1000 ($1.190)
  uint16_t cfm = 820;               // cubic feet per minute * 1000 of furnace (0.82)
  uint16_t compressorWatts = 2600;  // compressorWatts
  uint16_t furnacePost = 110;       // furnace internal fan timer
  uint16_t diffLimit = 200;         // in/out thermal differential limit. Set to 20 deg limit    F/C issue
  int16_t  fcOffset[2] = {-180,0};  // forecast offset adjust in minutes (cool/heat)
  uint16_t fanIdleMax = 60*4;       // fan idle max in minutes
  int16_t  sineOffset[2] = {0, 0};  // sine offset adjust (cool/heat)
  char     szSensorActive[8][12];   // sensor IDs for restart
  uint8_t  reserved[256];           // Note: To force an EEPROM update, just subtract 1 byte and flash again
  uint8_t  end;
}; // 512 bytes

extern eeMem ee;

#endif // EEMEM_H
