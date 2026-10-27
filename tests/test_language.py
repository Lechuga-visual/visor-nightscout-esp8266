import pathlib
import subprocess
import tempfile
import unittest
from tools.localization import header

ROOT=pathlib.Path(__file__).resolve().parents[1]


class LanguageTests(unittest.TestCase):
    def test_saved_language_only_changes_its_extension_and_requires_current_access(self):
        main=(ROOT/'src/main.cpp').read_text(encoding='utf-8')
        handler=main[main.index('void saveLanguage()'):main.index('void saveConfiguration()')]
        harness=r'''
#include <cassert>
#include <string>
#include <map>
#include <cstring>
#include <iostream>
#include "device_language.h"
#include "generated/ui_translations.h"
#define L(value) ui_translation::text(value, languageSettings.locale == device_language::Locale::English)
using String=std::string;
bool setupMode=false, allowed=true, current=true, storageReady=true;
String csrf="fixture",lastFrame="old";
device_language::Settings languageSettings;
struct Sink {
  int status=0;
  std::map<std::string,std::string> fields,headers;
  String arg(const char* key) {return fields[key];}
  void sendHeader(const char* key,const char* value){headers[key]=value;}
  void send(int value,const char* =nullptr,const char* =nullptr){status=value;}
} web;
bool authenticated(){if(!allowed)web.status=401;return allowed;}
bool currentForm(){if(!current)web.status=403;return current;}
struct Storage {
  unsigned writes=0,commits=0;bool fail=false;
  unsigned char bytes[4096];
  Storage(){memset(bytes,0xa5,sizeof(bytes));}
  template<typename T>void put(size_t offset,const T& value){++writes;memcpy(bytes+offset,&value,sizeof(T));}
  bool commit(){++commits;return !fail;}
} EEPROM;
'''+handler+r'''
int main(){
  using namespace device_language;
  static_assert(OFFSET==3872,"Old settings must not move");
  Settings erased;memset(&erased,0xff,sizeof(erased));assert(!valid(erased));
  assert(valid(languageSettings)&&languageSettings.locale==Locale::Spanish);
  auto old=languageSettings;assert(!parse("EN",languageSettings));assert(!parse("en<script>",languageSettings));
  assert(!memcmp(&old,&languageSettings,sizeof(old)));
  assert(!parse(nullptr,languageSettings));
  assert(parse("en",languageSettings)&&valid(languageSettings));
  languageSettings.reserved[0]=1;assert(!valid(languageSettings));languageSettings=old;
  web.fields["language"]="en";allowed=false;saveLanguage();assert(web.status==401&&EEPROM.writes==0);
  allowed=true;current=false;saveLanguage();assert(web.status==403&&EEPROM.writes==0);
  current=true;web.fields["language"]="xx";saveLanguage();assert(web.status==400&&EEPROM.writes==0);
  web.fields["language"]="en";storageReady=false;saveLanguage();assert(web.status==503&&EEPROM.writes==0);storageReady=true;
  unsigned char before[4096];memcpy(before,EEPROM.bytes,sizeof(before));
  saveLanguage();assert(web.status==303&&web.headers["Location"]=="/settings"&&EEPROM.commits==1);
  assert(languageSettings.locale==Locale::English&&lastFrame.empty());
  assert(!memcmp(before,EEPROM.bytes,OFFSET));assert(!memcmp(before+OFFSET+8,EEPROM.bytes+OFFSET+8,4096-OFFSET-8));
  Settings rebooted;memcpy(&rebooted,EEPROM.bytes+OFFSET,sizeof(rebooted));assert(valid(rebooted)&&rebooted.locale==Locale::English);
  saveLanguage();assert(EEPROM.commits==1); // Idempotent, no flash wear.
  web.fields["language"]="es";EEPROM.fail=true;saveLanguage();assert(web.status==500&&languageSettings.locale==Locale::English);
  assert(!memcmp(EEPROM.bytes+OFFSET,&languageSettings,sizeof(languageSettings))); // Restore buffer on failure.
  EEPROM.fail=false;setupMode=true;allowed=false;saveLanguage();assert(web.status==303&&web.headers["Location"]=="/"&&languageSettings.locale==Locale::Spanish);
  assert(!strcmp(ui_translation::text("Conectado",true),"Connected"));
  assert(!strcmp(ui_translation::text("Conectado",false),"Conectado"));
  assert(!strcmp(ui_translation::text("JSON: object_json",true),"JSON: object_json"));
  assert(!strcmp(ui_translation::text("Subiendo rápido",true),"Rising quickly"));
  std::cout<<"PASS: erased/corrupt storage, strict locale validation, unchanged old settings, auth/CSRF, commit failure, restart persistence, idempotence, setup access and translated status\n";
}
'''
        with tempfile.TemporaryDirectory() as directory:
            source=pathlib.Path(directory)/'language.cpp';target=pathlib.Path(directory)/'language'
            source.write_text(harness, encoding='utf-8')
            subprocess.run(['g++','-std=c++11','-Wall','-Wextra','-Werror','-Wno-class-memaccess','-I',str(ROOT/'include'),str(source),'-o',str(target)],check=True)
            subprocess.run([str(target)],check=True)

    def test_generated_translation_catalog_is_current(self):
        self.assertEqual((ROOT/'include/generated/ui_translations.h').read_text(encoding='utf-8'),header())
