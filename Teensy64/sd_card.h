
/*

  SD card interface & Kernal ROM hacks for Teensy64

  Maciej 'YTM/Elysium' Witkowiak, <ytm@elysium.pl>

*/

#include <String.h>

#ifndef __SD_CARD_H
#define __SD_CARD_H
void sd_init(void);
size_t sd_load(String filename, char* mem, uint8_t lfn, uint8_t sa, bool loadmode, uint16_t *loadaddr);
#endif
