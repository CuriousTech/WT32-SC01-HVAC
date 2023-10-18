/**The MIT License (MIT)

Copyright (c) 2016 by Greg Cunningham, CuriousTech

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

// Build with Arduino IDE 1.8.19
//  ESP32: (2.0.13) ESP Wrover Module, 115200 baud, 80MHz Flash, QIO, Default 4MB with spiffs
// For remote unit, uncomment #define REMOTE in HVAC.h

#include <ESPAsyncWebServer.h> // https://github.com/me-no-dev/ESPAsyncWebServer
#include <TimeLib.h> // http://www.pjrc.com/teensy/td_libs_Time.html
#include <UdpTime.h> // https://github.com/CuriousTech/ESP07_WiFiGarageDoor/tree/master/libraries/UdpTime
#include "HVAC.h"
#include "WebHandler.h"
#include "display.h"
#include <Wire.h>
#include "eeMem.h"
#include "RunningMedian.h"

// Uncomment only one of these
//#include <SHT40.h> // //Move Libraries/SHT40 to Arduino/libraries
//#include <DHT.h>  // http://www.github.com/markruys/arduino-DHT
//#include <DallasTemperature.h> //DallasTemperature from library mamanger
#include <AM2320.h> // https://github.com/CuriousTech/ESP-HVAC/blob/master/Libraries/AM2320/AM2320.h

//----- Deffault i2c pins. Shared wiith touch. See HVAC.h for the rest -
#define SDA      18
#define SCL      19
#define AMPWR    4 // Used for resetting the sensor in case of failure
//------------------------

Display display;
eeMem ee;

HVAC hvac;

RunningMedian<int16_t,25> tempMedian; //median of 25 samples

#ifdef SHT40_H
SHT40 sht;
#endif
#ifdef dht_h
DHT dht;
#endif
#ifdef DallasTemperature_h
const int ds18Resolution = 12;
DeviceAddress ds18addr = { 0x28, 0xC1, 0x02, 0x64, 0x04, 0x00, 0x00, 0x35 };
unsigned int ds18delay;
unsigned long ds18lastreq = 1; //zero is special
const unsigned int ds18reqdelay = 5000; //request every 5 seconds
unsigned long ds18reqlastreq;
OneWire oneWire(2); //pin 2
DallasTemperature ds18(&oneWire);
#endif
#ifdef AM2303_H
AM2320 am;
#endif

UdpTime uTime;

extern void WsSend(String s);

void setup()
{
  pinMode(AMPWR, OUTPUT);
  digitalWrite(AMPWR, HIGH);
  Serial.begin(115200);  // Just for debug
  Serial.println("starting");
  ee.init();
  hvac.init();
  display.init();
  startServer();

#ifdef SHT40_H
  sht.init(4);
#endif
#ifdef dht_h
  dht.setup(UNSET, DHT::DHT22);
#endif
#ifdef DallasTemperature_h
  ds18.setResolution(ds18addr, ds18Resolution);
  ds18.setWaitForConversion(false); //this enables asyncronous calls
  ds18.requestTemperatures(); //fire off the first request
  ds18lastreq = millis();
  ds18delay = 750 / (1 << (12 - ds18Resolution)); //delay based on resolution
#endif
}

void loop()
{
  static uint8_t hour_save, min_save = 255, sec_save;
  static int8_t lastSec;
  static int8_t lastHour;
  static int8_t lastDay = -1;

  display.service();  // check for touch, etc.

  if(uTime.check(ee.tz))
  {
    hvac.m_DST = uTime.getDST();
    if(lastDay == -1)
      lastDay = day() - 1;
  }

  handleServer(); // handles mDNS, web

#ifdef DallasTemperature_h
  if(ds18lastreq > 0 && millis() - ds18lastreq >= ds18delay) { //new temp is ready
    tempMedian.add((ee.b.bCelcius ? ds18.getTempC(ds18addr):ds18.getTempF(ds18addr)) );
    ds18lastreq = 0; //prevents this block from firing repeatedly
    float temp;
    if (tempMedian.getAverage(temp) == tempMedian.OK) {
      hvac.updateIndoorTemp( temp * 10, 500); //fake 50%
    }
  }

  if(millis() - ds18reqlastreq >= ds18reqdelay) {
    ds18.requestTemperatures(); 
    ds18lastreq = millis();
    ds18reqlastreq = ds18lastreq;
  }
#endif
  if(sec_save != second()) // only do stuff once per second
  {
    sec_save = second();

    if(secondsServer()) // once per second stuff, returns true once on connect
      uTime.start();
    display.oneSec();
    hvac.service();   // all HVAC code

#ifdef SHT40_H
    static uint8_t read_delay = 2;
    static uint8_t errCnt = 0;

    if(--read_delay == 0)
    {
      if(digitalRead(AMPWR) == LOW)
        digitalWrite(AMPWR, HIGH); // Power the SHT40
      else if(sht.service())
      {
        tempMedian.add((ee.b.bCelcius ? sht.getTemperatureC():sht.getTemperatureF()) * 10);
        float temp;
        if (tempMedian.getAverage(2, temp) == tempMedian.OK) {
          hvac.updateIndoorTemp( temp, sht.getRh() * 10 );
        }
        errCnt = 0;
      }
      else
      {
        digitalWrite(AMPWR, LOW); // Cut power to the SHT40
        if(errCnt < 5)
          errCnt++;
        else
          hvac.m_notif = Note_Sensor;
      }
      read_delay = 5; // update every 5 seconds
    }
#endif

#ifdef dht_h
    static uint8_t read_delay = 2;

    if(--read_delay == 0)
    {
      float temp;
      if(ee.bCelcius)
        temp = dht.getTemperature() * 10;
      else
        temp = dht.toFahrenheit(dht.getTemperature()) * 10;

      if(dht.getStatus() == DHT::ERROR_NONE)
      {
        tempMedian.add(temp);
        if (tempMedian.getAverage(2, temp) == tempMedian.OK) {
          hvac.updateIndoorTemp( temp, dht.getHumidity() * 10);
        }
      }
      read_delay = 5; // update every 5 seconds
    }
#endif

#ifdef AM2303_H
    static uint8_t read_delay = 2;
    static uint8_t errCnt = 0;

    if(--read_delay == 0)
    {
      float temp;
      float rh;
      if(digitalRead(AMPWR) == LOW)
        digitalWrite(AMPWR, HIGH); // Power the AM2320
      else if(am.measure(temp, rh))
      {
        if(!ee.b.bCelcius)
          temp = temp * 9 / 5 + 32;
        tempMedian.add(temp * 10);
        if (tempMedian.getAverage(2, temp) == tempMedian.OK) {
          hvac.updateIndoorTemp( temp, rh * 10 );
        }
        errCnt = 0;
      }
      else
      {
        Serial.print("AM2320 error: ");
        Serial.println(am.code);
        digitalWrite(AMPWR, LOW); // Cut power to the AM2320
        if(errCnt < 5)
          errCnt++;
        else
          hvac.m_notif = Note_Sensor;
      }
      read_delay = 5; // update every 5 seconds
    }
#endif

    if(min_save != minute()) // only do stuff once per minute
    {
      min_save = minute();

      if(hour_save != hour()) // update our IP and time daily (at 2AM for DST)
      {
        hour_save = hour();
        if(hour_save == 2)
          uTime.start(); // update time daily at DST change
        if(hour_save == 0 && year() > 2020)
        {
          if(lastDay != -1)
          {
            hvac.dayTotals(lastDay);
            hvac.monthTotal(month() - 1, day());
          }
          lastDay = day() - 1;
          ee.iSecsDay[lastDay][0] = 0; // reset
          ee.iSecsDay[lastDay][1] = 0;
          ee.iSecsDay[lastDay][2] = 0;
          if(lastDay == 0) // new month
          {
            int m = (month() + 10) % 12; // last month: Dec = 10, Jan = 11, Feb = 0
            hvac.monthTotal(m, -1);
          }
        }
        if(ee.check())
        {
          if((hour_save & 1) == 0) // every other hour
          {
            ee.filterMinutes = hvac.m_filterMinutes;
            ee.update();
          }
        }
      }
    }
  }
  delay(2); // mostly to slow down Display::lines()
}
