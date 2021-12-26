
/*

  SD card interface & Kernal ROM hacks for Teensy64

  Maciej 'YTM/Elysium' Witkowiak, <ytm@elysium.pl>

*/

void sd_init(void);
void sd_printdir(void);
size_t sd_load(String filename, char* mem, size_t size);
