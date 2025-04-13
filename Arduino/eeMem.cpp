#include "eeMem.h"
#include "Media.h"

const char szSettings[] = "/settings.bin";

bool eeMem::init()
{
  if(!INTERNAL_FS.begin(true))
  {
    return false;
  }

  uint8_t data[EESIZE];
  uint16_t *pwTemp = (uint16_t *)data;

  if(INTERNAL_FS.exists("/eemem.bin") ) // old name
  {
    INTERNAL_FS.rename("/eemem.bin", szSettings);
  }

  File F = INTERNAL_FS.open(szSettings, "r");
  if(F)
  {
    F.read((byte*)data, EESIZE);
    F.close();
  }

  if(pwTemp[0] != EESIZE)
  {
    return true; // revert to defaults if struct size changes
  }

  uint16_t sum = pwTemp[1];
  pwTemp[1] = 0;
  pwTemp[1] = Fletcher16(data, EESIZE );
  if(pwTemp[1] != sum)
  {
    return true; // revert to defaults if sum fails
  }
  memcpy(this + offsetof(eeMem, size), data, EESIZE );
  return true;
}

bool eeMem::update() // write the settings if changed
{
  if(!check()) // make sure sum is correct, and if save needed
    return false;

  File F;

  if(F = INTERNAL_FS.open(szSettings, "w"))
  {
    F.write((byte*) this + offsetof(eeMem, size), EESIZE);
    F.close();
    return true;
  }
  return false;
}

bool eeMem::check()
{
  uint16_t old_sum = ee.sum;
  ee.sum = 0;
  ee.sum = getSum();
  return (old_sum == ee.sum) ? false:true;
}

uint16_t eeMem::getSum()
{
  return Fletcher16((uint8_t*)this + offsetof(eeMem, size), EESIZE);
}

uint16_t eeMem::Fletcher16( uint8_t* data, int count)
{
   uint16_t sum1 = 0;
   uint16_t sum2 = 0;

   for( int index = 0; index < count; ++index )
   {
      sum1 = (sum1 + data[index]) % 255;
      sum2 = (sum2 + sum1) % 255;
   }

   return (sum2 << 8) | sum1;
}
