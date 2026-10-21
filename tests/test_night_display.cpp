#include <cassert>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <initializer_list>
#include "night_display.h"

int main() {
  using namespace night_display;
  Settings s, output;
  assert(valid(s) && !s.enabled && s.day==100 && s.night==25);
  Settings erased; memset(&erased,0xff,sizeof(erased)); assert(!valid(erased));
  assert(OFFSET==3856 && sizeof(Settings)==16);
  unsigned char eeprom[device_config::STORAGE_SIZE], original[device_config::STORAGE_SIZE];
  memset(eeprom,0xa5,sizeof(eeprom));memcpy(original,eeprom,sizeof(eeprom));
  memcpy(eeprom+OFFSET,&s,sizeof(s));assert(!memcmp(eeprom,original,OFFSET));
  assert(!memcmp(eeprom+OFFSET+sizeof(s),original+OFFSET+sizeof(s),sizeof(eeprom)-OFFSET-sizeof(s)));
  assert(update("22:00","07:00","100","25","0",true,output));s=output;
  for (const char* value : {"", "24:00", "23:60", "2:00", "09:0a", "09:00x", "-1:00"}) {
    Settings before=output; assert(!update(value,"07:00","100","25","0",true,output));
    assert(!memcmp(&before,&output,sizeof(output)));
  }
  assert(!update("22:00","22:00","100","25","0",true,output));
  for (const char* value : {"0","9","101","-1","25x","999999999"})
    assert(!update("22:00","07:00",value,"25","0",true,output));
  assert(!update("22:00","07:00","100","25","8",true,output));
  assert(intensity(s,1319,true)==100 && intensity(s,1320,true)==25);
  assert(intensity(s,1439,true)==25 && intensity(s,0,true)==25);
  assert(intensity(s,419,true)==25 && intensity(s,420,true)==100);
  assert(intensity(s,0,false)==100 && intensity(s,-1,false)==100);
  s.enabled=0;assert(intensity(s,0,true)==100);s.enabled=1;
  s.start=8*60;s.end=12*60;
  assert(intensity(s,479,true)==100 && intensity(s,480,true)==25);
  assert(intensity(s,719,true)==25 && intensity(s,720,true)==100);
  // Preserve exact pixels at 100%, channel monotonicity and warning hues when dimming.
  for (unsigned color=0;color<=65535;++color) {
    assert(scale565(color,100)==color);
    uint16_t previous=0;
    for (unsigned percent : {10u,25u,50u,75u,100u}) {
      uint16_t scaled=scale565(color,percent);
      assert((scaled>>11)>=(previous>>11));
      assert(((scaled>>5)&63)>=((previous>>5)&63));
      assert((scaled&31)>=(previous&31)); previous=scaled;
    }
  }
  assert(scale565(0,25)==0);
  assert((scale565(0xf800,25)&0x7ff)==0);
  Preview preview; assert(!preview.active(0));
  preview.start(25,0xfffffff0);assert(preview.active(0xfffffff0+1));
  assert(preview.active(static_cast<uint32_t>(0xfffffff0u+4999u)));
  assert(!preview.active(static_cast<uint32_t>(0xfffffff0u+5000u)));
  preview.start(40,100);preview.start(25,200);assert(preview.active(5199));assert(!preview.active(5200));
  auto local=[](unsigned zone,int year,int month,int day,int hour,int minute) {
    struct tm utc={};utc.tm_year=year-1900;utc.tm_mon=month-1;utc.tm_mday=day;utc.tm_hour=hour;utc.tm_min=minute;
    time_t epoch=timegm(&utc);setenv("TZ",timezoneRule(zone),1);tzset();struct tm result;localtime_r(&epoch,&result);return result;
  };
  assert(local(0,2026,7,15,10,30).tm_hour==6); // Santiago winter UTC-4.
  assert(local(0,2026,10,21,10,30).tm_hour==7); // Santiago summer UTC-3.
  assert(local(0,2026,9,6,3,59).tm_hour==23);
  assert(local(0,2026,9,6,4,0).tm_hour==1); // Midnight skipped at DST start.
  assert(local(0,2026,4,5,2,59).tm_hour==23);
  assert(local(0,2026,4,5,3,0).tm_hour==23); // Repeated hour at DST end.
  assert(local(1,2026,7,15,10,30).tm_hour==7); // Magallanes/Aysen stay UTC-3.
  assert(local(7,2026,10,21,10,30).tm_hour==10);
  assert(local(6,2026,7,15,10,30).tm_hour==12 && local(6,2026,1,15,10,30).tm_hour==11);
  std::cout << "Storage, atomic validation, midnight/boundaries, clock fallback, RGB565 scaling, preview wraparound and DST passed\n";
}
