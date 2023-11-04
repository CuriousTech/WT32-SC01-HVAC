/*
  LibHumidity - A Humidity Library for Arduino.

  Supported Sensor modules:
    SHT21-Breakout Module - https://moderndevice.com/products/sht21-humidity-sensor

  Created by Christopher Ladden at Modern Device on December 2009.
  modified by Paul Badger March 2010

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include <inttypes.h>
#include <Wire.h>
#include "SHT40.h"
#include "Arduino.h"

/**********************************************************
 * Initialize the sensor based on the specified type.
 **********************************************************/
SHT40::SHT40() {
}

void SHT40::init(uint8_t seconds) {
//  Wire.begin();
  m_mil = millis();
  m_interval = seconds * 10;
}

/**********************************************************
 *  The SHT40 humidity sensor datasheet says:
 *  Parameter Resolution typ max Units
 *    14 bit      66        85      ms
 *    13 bit      33        43      ms
 *    12 Bit      17        22      ms
 *    11 bit       8        11      ms
 *    10 bit       4         6      ms
 *
 *      Measurement time
 *      (max values for -40°C
 *        125°C.)
 *      8 bit 1 3 ms
 *
 **********************************************************/

bool SHT40::service()
{
  static uint8_t state = 0;
  bool bRc = false;
  static bool bErr = false;
  int c;

  if(millis() - m_mil < 100)
    return false;

  switch(state)
  {
    case 0:
      Wire.beginTransmission(eSHT40Address);   //begin
      Wire.write(eRH40HighNoHoldCmd);              //send the pointer location
      Wire.write(0x00);              //send the pointer location
      Wire.endTransmission();                  //end
      bErr = false;
      break;
    case 1: // 100ms later...
      Wire.requestFrom(eSHT40Address, 6);
      break;
    case 2: // 100ms later...
      if(Wire.available() < 6)
        break; // not ready
      c = Wire.read();
      if(c == -1) bErr = true;
      else m_temp = c << 8;
      c = Wire.read();
      if(c == -1) bErr = true;
      if(!bErr) m_temp |= c;
      c = Wire.read(); // CRC

      c = Wire.read();
      m_rh = c << 8;
      c = Wire.read();
      m_rh |= c;
      c = Wire.read(); // CRC
      bRc = true;

      break;
  }

  if(++state > m_interval)
    state = 0;
  m_mil = millis();
  return bRc;
}

float SHT40::getTemperatureC()
{
  return calculateTemperatureC(m_temp);
}

float SHT40::getTemperatureF()
{
  return calculateTemperatureF(m_temp);
}

float SHT40::getRh()
{
  return calculateHumidity(m_rh, m_temp);
}

/******************************************************************************
 * Private Functions
 ******************************************************************************/

float SHT40::calculateTemperatureC(uint16_t analogTempValue) {

  return (((175.72/65536.0) * (float)analogTempValue) - 46.85); //T= -46.85 + 175.72 * ST/2^16
}

float SHT40::calculateTemperatureF(uint16_t analogTempValue) {

  return (((175.72/65536.0) * (float)analogTempValue) - 46.85) * 9/5 + 32; //T= -46.85 + 175.72 * ST/2^16
}

float SHT40::calculateHumidity(uint16_t analogHumValue, uint16_t analogTempValue)
{
  float srh = analogHumValue;
  float humidityRH;                       // variable for result

  //-- calculate relative humidity [%RH] --
  humidityRH = -6.0 + 125.0/65536.0 * srh;       // RH= -6 + 125 * SRH/2^16
  return humidityRH;
}
