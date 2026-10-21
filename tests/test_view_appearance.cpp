#include "view_appearance.h"
#include <assert.h>
#include <stdio.h>
#include <string>
#include <array>

using namespace view_appearance;
void testTaggedStoragePreservesCredentialsAndName() {
  static_assert(sizeof(device_config::Settings)==3732,"Old configuration layout changed");
  static_assert(patient_label::OFFSET==3732 && OFFSET==3836,"Old extensions moved");
  std::array<unsigned char,device_config::STORAGE_SIZE> storage;
  storage.fill(0xFF);
  device_config::Settings device{};
  device.magic=device_config::MAGIC;
  strcpy(device.url,"https://nightscout.example");
  strcpy(device.token,"fixture-readonly-token");
  strcpy(device.customCa,"-----BEGIN PUBLIC KEY-----\nfixture\n-----END PUBLIC KEY-----");
  patient_label::Label person; strcpy(person.name,"María");
  memcpy(storage.data(),&device,sizeof(device));
  memcpy(storage.data()+patient_label::OFFSET,&person,sizeof(person));
  const auto before=storage;
  Settings erased; memcpy(&erased,storage.data()+OFFSET,sizeof(erased));
  assert(!valid(erased));
  Settings defaults;
  assert(valid(defaults) && defaults.mode==Mode::Graph && defaults.theme==Theme::Mint);
  Settings selected;
  assert(update(defaults,"2","4",nullptr,nullptr,nullptr,selected));
  memcpy(storage.data()+OFFSET,&selected,sizeof(selected));
  assert(memcmp(storage.data(),before.data(),OFFSET)==0);
  Settings restored; memcpy(&restored,storage.data()+OFFSET,sizeof(restored));
  assert(valid(restored) && restored.mode==Mode::Centered && restored.theme==Theme::Cyan);
  memcpy(storage.data(),&device,sizeof(device)); // Old settings save must keep appearance.
  memcpy(storage.data()+patient_label::OFFSET,&person,sizeof(person));
  assert(memcmp(storage.data()+OFFSET,&selected,sizeof(selected))==0);
}
void testPresetAndQuantizedColorContrast() {
  for (unsigned i=0;i<5;++i) {
    Settings settings; settings.theme=static_cast<Theme>(i);
    assert(readable(palette(settings)));
    assert(contrast(palette(settings).number,palette(settings).background)>=4.5);
  }
  assert(rgb565(0xFFFFFF)==0xFFFF && rgb565(0)==0);
  assert(rgb565(0xFF0000)==0xF800 && rgb565(0x00FF00)==0x07E0 && rgb565(0x0000FF)==0x001F);
  assert(!readable({0x001122,0x001122,0x001122}));
  assert(!readable({0,0xFFFFFF,0})); // A light background hides fixed warnings.
  assert(!readable({0xFFFFFF,0,0x1000000}));
  assert(readable({0x72D9F5,0x0B1114,0xF2F7F6}));
}
void testPartialAtomicUpdatesAndValidation() {
  Settings current, output;
  assert(update(current,"1","5","#72D9f5","#0B1114","#F2F7F6",output));
  current=output;
  assert(update(current,"2",nullptr,nullptr,nullptr,nullptr,output));
  assert(output.mode==Mode::Centered && output.theme==Theme::Custom);
  assert(output.accent==current.accent && output.number==current.number);
  const Settings untouched=output;
  for (const char* invalid : {"", "3", "-1", "01", "1x"}) {
    assert(!update(current,invalid,nullptr,nullptr,nullptr,nullptr,output));
    assert(memcmp(&output,&untouched,sizeof(output))==0);
  }
  assert(!update(current,nullptr,"6",nullptr,nullptr,nullptr,output));
  assert(!update(current,"2","5","#000000","#000000","#000000",output));
  assert(memcmp(&output,&untouched,sizeof(output))==0);
  assert(update(current,nullptr,"1",nullptr,nullptr,nullptr,output));
  assert(palette(output).accent==0xFF675E && output.number==0xF2F7F6); // Presets retain custom colors.
  current=output;
  assert(update(current,nullptr,"5",nullptr,nullptr,nullptr,output));
  assert(palette(output).number==0xF2F7F6);
  uint32_t parsed=123;
  for (const char* invalid : {"red","#fff","#1234567","#12345g","#000000\r\n"}) {
    assert(!color(invalid,parsed) && parsed==123);
  }
  assert(color("#Ab12CD",parsed) && parsed==0xAB12CD);
}
int main() {
  testTaggedStoragePreservesCredentialsAndName();
  testPresetAndQuantizedColorContrast();
  testPartialAtomicUpdatesAndValidation();
  puts("3 appearance test groups passed");
}
