
/*

  SD card interface & Kernal ROM hacks for Teensy64

  Maciej 'YTM/Elysium' Witkowiak, <ytm@elysium.pl>

*/

#include <SD.h>

File root;

bool sd_valid = false;

void sd_init(void) {
    if (!SD.begin(BUILTIN_SDCARD)) {
        Serial.println("SD initialization failed!");
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
    root = SD.open("/");
    printDirectory(root, 0);
}

size_t sd_load(String filename, char* mem, size_t size) {
  File f;
  size_t bytes;
  uint8_t b;

  f = SD.open(filename.c_str());
  bytes = 0;
  if (f) {
    // skip loadaddress
    f.read();
    f.read();
    while (f.available() && (bytes<size)) {
      mem[bytes++] = f.read();
    }
    f.close();
    Serial.print("got "); Serial.print(bytes, HEX); Serial.println(" bytes");
  } else {
    Serial.print("ERR: can't open/find file ["); Serial.print(filename); Serial.println("]");
  }
  return bytes;
}
