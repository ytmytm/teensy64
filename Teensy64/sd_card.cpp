
/*

  SD card interface & Kernal ROM hacks for Teensy64

  Maciej 'YTM/Elysium' Witkowiak, <ytm@elysium.pl>

*/

#include <SD.h>

File root;

bool sd_valid = false;
String cwd("/");

void sd_init(void) {
    if (!SD.begin(BUILTIN_SDCARD)) {
        Serial.println("SD initialization failed!");
        return;
    }
    Serial.println("SD initialization done.");
    sd_valid = true;
}

void printDirectory(File dir, int numTabs) {
  while (true) {

    File entry =  dir.openNextFile();
    if (! entry) {
      // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}


void sd_printdir(void) {
    root = SD.open(cwd.c_str());
    printDirectory(root, 0);
}

size_t sd_load(String filename, char* mem, uint8_t lfn, uint8_t sa, bool loadmode, uint16_t *loadaddr) {
  File f;
  size_t bytes = 0;
  String fullpath;

Serial.print("in sd_load with filename=["); Serial.print(filename); Serial.print("], lfn="); Serial.print(lfn); Serial.print(" sa="); Serial.print(sa); Serial.print(" loadmode="); Serial.print(loadmode); Serial.print(" loadaddr="); Serial.println(loadaddr[0], HEX);

  if (filename.charAt(0)=='/') {
    fullpath = filename;
  } else {
    fullpath = cwd + "/" + filename;
  }

  f = SD.open(fullpath.c_str());
  if (f) {
    if (f.isDirectory() && lfn==15 && sa==1 && !loadmode) {
      Serial.print("CD to ["); Serial.print(fullpath); Serial.println("]");
      cwd = fullpath;
      f.close();
      return 0;
    }
    if (f.isDirectory() && lfn==15 && sa==0 && loadmode) {
      Serial.print("CD to ["); Serial.print(fullpath); Serial.println("] and list");
    }
    if (sa==0) {
      // skip loadaddress
      f.read();
      f.read();
    } else {
      loadaddr[0] = f.read();
      loadaddr[0] |= f.read() << 8;
    }
    Serial.print("loading to "); Serial.println(loadaddr[0], HEX);
    while (f.available() && ((loadaddr[0]+bytes)<0x10000)) {
      mem[loadaddr[0]+bytes] = f.read();
      bytes++;
    }
    f.close();
    Serial.print("got "); Serial.print(bytes, HEX); Serial.println(" bytes");
  } else {
    Serial.print("ERR: can't open/find file ["); Serial.print(filename); Serial.println("]");
  }
  return bytes;
}
