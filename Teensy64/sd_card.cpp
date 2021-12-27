
/*

  SD card interface & Kernal ROM hacks for Teensy64

  Maciej 'YTM/Elysium' Witkowiak, <ytm@elysium.pl>

*/

#include <SD.h>

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

#define FILENAME_LENGTH 16

typedef enum filetype:uint8_t {
  FILE_NONE = 0x00,
  FILE_DIR,

  FILE_PRG,
  FILE_TCRT,

  FILE_TAP,
  FILE_D64,

  FILE_UNKNOWN = 0xFF
} file_t;

typedef struct direlement {
    char name[FILENAME_LENGTH+1];
    uint32_t size;
    uint8_t type;
} __attribute__((packed)) DirElement;

#define MAX_DIR_ELEMENTS 1000

size_t sd_browser_dir(struct direlement *f, File dir) {
  uint16_t fcount = 0;
  while (fcount<MAX_DIR_ELEMENTS) {
    File entry = dir.openNextFile();
    if (!entry) break; // no more files
    strncpy(f[fcount].name, entry.name(), FILENAME_LENGTH);
    f[fcount].size = (uint32_t)entry.size();
    if (entry.isDirectory()) {
      f[fcount].type = FILE_DIR;
    } else {
      String fn(entry.name());
      fn.toUpperCase();
      f[fcount].type = FILE_UNKNOWN;
      if (fn.endsWith(".PRG")) f[fcount].type = FILE_PRG;
      if (fn.endsWith(".D64")) f[fcount].type = FILE_D64;
      if (fn.endsWith(".BIN")) f[fcount].type = FILE_TCRT;
      if (fn.endsWith(".CRT")) f[fcount].type = FILE_TCRT;
      if (fn.endsWith(".TCRT")) f[fcount].type = FILE_TCRT;
    }
    fcount++;
    Serial.print(fcount);
    Serial.print(" - "); Serial.print((uint64_t)&f[fcount], HEX);
    Serial.print(" - "); Serial.print(entry.name());
    Serial.print(" - "); Serial.print(entry.size());
    Serial.print(" - "); Serial.println(entry.isDirectory());
    entry.close();
  }
  // one more empty with FILE_NONE
  f[fcount].name[0] = '\0';
  f[fcount].size = 0;
  f[fcount].type = FILE_NONE;
  fcount++;
//  count[0] = fcount;
  Serial.print("returning with num bytes="); Serial.println(fcount*sizeof(struct direlement), HEX);
  return (fcount*sizeof(struct direlement));
}

size_t sd_load(String filename, char* mem, uint8_t lfn, uint8_t sa, bool loadmode, uint16_t *loadaddr) {
  File f;
  size_t bytes = 0;
  String fullpath;

  Serial.print("in sd_load with filename=["); Serial.print(filename); Serial.print("], lfn="); Serial.print(lfn); Serial.print(" sa="); Serial.print(sa); Serial.print(" loadmode="); Serial.print(loadmode); Serial.print(" loadaddr="); Serial.println(loadaddr[0], HEX);

  // handle cd .. and pass through
  if (filename.equals("..")) {
    // strip cwd up until first slash
    Serial.print("..:["); Serial.print(cwd); Serial.println("]");
    int i = cwd.lastIndexOf('/');
    if (i<=0) { cwd = "/"; }; // not found or only slash
    if (i>0) { cwd = cwd.substring(0, i); }; // trim up to last slash
    Serial.print("after:["); Serial.print(cwd); Serial.println("]");
    filename = cwd;
  }

  // setup full path to the file
  if (filename.charAt(0)=='/') {
    fullpath = filename;
  } else {
    fullpath = cwd;
    if (!cwd.endsWith('/')) { fullpath += '/'; };
    fullpath += filename;
  }

  f = SD.open(fullpath.c_str());
  if (f) {
    if (f.isDirectory()) {
      bytes = 0;
      if (lfn==15 && sa==0 && loadmode) {
        Serial.print("CD to ["); Serial.print(fullpath); Serial.println("] and list");
        bytes = sd_browser_dir((struct direlement *)&mem[loadaddr[0]], f);
        cwd = fullpath;
      }
      f.close();
      return bytes;
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
