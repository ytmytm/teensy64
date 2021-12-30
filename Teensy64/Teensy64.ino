
// IDE settings: Teensy 4.1, Serial, 600MHz, Faster

//
//
//  File Name   :  MCL64.c
//  Used on     :  
//  Author      :  Ted Fried, MicroCore Labs
//  Creation    :  3/12/2021
//
//   Description:
//   ============
//   
//  MOS 6510 emulator with bus interface.
//
// The acceleration modes can be changed via the UART from the host.
// Entering a 0,1,2,3 will change the acceleration mode to this value
// and it will be echoed back to the host.
//
// Entering mode 2 or 4 could result in video corruption, but the CPU will still 
// be running.  When returning to mode-0 or mode-1 the video should return to normal.
//
//------------------------------------------------------------------------
//
// Modification History:
// =====================
//
// Revision 1 3/12/2021
// Initial revision
//
// Revision 2 11/28/2021
// Improved undocumented opcodes
//
// Revision 3 12/10/2021
// Made optimiations for acceleration and UART control
//
//
//------------------------------------------------------------------------
//
// Copyright (c) 2021 Ted Fried
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.
//
//------------------------------------------------------------------------

#include <stdint.h>

// Teensy 4.1 pin assignments
//
#define PIN_CLK0            24
#define PIN_READY_n         26
#define PIN_IRQ             25
#define PIN_NMI             41
#define PIN_RESET           40
#define PIN_RDWR_n          12
#define PIN_P0              22
#define PIN_P1              13
#define PIN_P2              39
                    
#define PIN_ADDR0           27 
#define PIN_ADDR1           38 
#define PIN_ADDR2           28 
#define PIN_ADDR3           37 
#define PIN_ADDR4           29 
#define PIN_ADDR5           36 
#define PIN_ADDR6           30 
#define PIN_ADDR7           35         
#define PIN_ADDR8           31 
#define PIN_ADDR9           34 
#define PIN_ADDR10          32 
#define PIN_ADDR11          33 
#define PIN_ADDR12          1  
#define PIN_ADDR13          0  
#define PIN_ADDR14          2  
#define PIN_ADDR15          23 
        
#define PIN_DATAIN0         14 
#define PIN_DATAIN1         15 
#define PIN_DATAIN2         16 
#define PIN_DATAIN3         17 
#define PIN_DATAIN4         18 
#define PIN_DATAIN5         19 
#define PIN_DATAIN6         20 
#define PIN_DATAIN7         21 
        
#define PIN_DATAOUT0        11 
#define PIN_DATAOUT1        10 
#define PIN_DATAOUT2        9 
#define PIN_DATAOUT3        8 
#define PIN_DATAOUT4        7 
#define PIN_DATAOUT5        6 
#define PIN_DATAOUT6        5 
#define PIN_DATAOUT7        4 
#define PIN_DATAOUT_OE_n    3 


// 6502 Flags
//
#define flag_n    (register_flags & 0x80) >> 7    // register_flags[7]
#define flag_v    (register_flags & 0x40) >> 6    // register_flags[6]
#define flag_b    (register_flags & 0x10) >> 4    // register_flags[4]
#define flag_d    (register_flags & 0x08) >> 3    // register_flags[3]
#define flag_i    (register_flags & 0x04) >> 2    // register_flags[2]
#define flag_z    (register_flags & 0x02) >> 1    // register_flags[1]
#define flag_c    (register_flags & 0x01) >> 0    // register_flags[0]



#define Page_128_159  ( (current_address >= 0x8000) && (current_address <= 0x9FFF) ) ? 0x1 : 0x0 
#define Page_160_191  ( (current_address >= 0xA000) && (current_address <= 0xBFFF) ) ? 0x1 : 0x0 
#define Page_224_255  ( (current_address >= 0xE000) && (current_address <= 0xFFFF) ) ? 0x1 : 0x0 

// 6502 stack always in Page 1
//
#define register_sp_fixed  (0x0100 | register_sp)


// CPU register for direct reads of the GPIOs
//
uint8_t   register_flags=0x34; 
uint8_t   register_a=0;
uint8_t   register_x=0;
uint8_t   register_y=0;
uint8_t   register_sp=0xFF;
uint8_t   direct_datain=0;
uint8_t   direct_reset=0;
uint8_t   direct_ready_n=0;
uint8_t   direct_irq=0;
uint8_t   direct_nmi=0;
uint8_t   global_temp=0;
uint8_t   last_access_internal_RAM=0;
uint8_t   ea_data=0;
uint8_t   mode=1;
uint8_t   current_address_mode=1;
uint8_t   current_p=0x7;
uint8_t   bank_mode=0x1f;
uint8_t   io_enabled=1;
uint8_t   EXROM=1;
uint8_t   GAME=1;
bool      load_trap_enabled=true;
bool      reu_emulation_enabled=true;
bool      internal_io_address=false;

uint16_t  register_pc=0;
uint16_t  current_address=0;
uint16_t  effective_address=0;

uint8_t   internal_RAM[65536];
#include "rom_basic.h"
#include "rom_kernal.h"

// include 8k diagnostics cartridge from http://www.zimmers.net/anonftp/pub/cbm/schematics/cartridges/c64/diag/index.html
// send 'e' to set EXROM to 0, send 'E' to disable cart (EXROM=1)
#include "diag-c64-8k.h" // CART_LOW_ROM at $8000
uint8_t   CART_HIGH_ROM[0x2000]; // CART_HIGH_ROM empty

#define CHAREN_BIT	0x04
#define HIRAM_BIT	0x02
#define LORAM_BIT	0x01

#define TEENSY64_REGISTER_BASE	0xd0f0
#define TEENSY64_REGISTER_SIZE	0x03

uint8_t teensy64_registers[TEENSY64_REGISTER_SIZE];
/* register set at $d030 (53296)
 *  0x00 - xxxxxxx0 - clear: cycle exact, set: enable speedup mode from register 0x01 (by default mode 1) (for C128 2MHz compatibility)
 *  0x01 - xxxxxx10 - mode number for speedup (1, 2 or 3), no speedup with mode 1
 *  0x02 - xxxxxxx0 - clear: enable mode 0 (external cartridge) and RESET, set: enable mode 1 (internal RAM), no RESET
 *         xxxxxx1x - clear: no LOAD trap, set: enable LOAD trap (default: enabled)
 *         xxxxx2xx - clear: no REU emulation, set: emulate REU (default: enabled)
 */

// REU emulation based on https://codebase64.org/doku.php?id=base:reu_registers
#define REU_REGISTER_BASE 0xdf00
#define REU_REGISTER_SIZE 32 // REU has 5 address lines connected, mirrors every 32 bytes
uint8_t reu_registers[REU_REGISTER_SIZE] = { 0x10, 0x10, 0, 0, 0, 0, 0xf8, 0xff, 0xff, 0x1f, 0x3f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
uint8_t reu_bank0[65536*4]; // 256K because we run out of RAM in Teensy RAM0
uint8_t *reu_bank1; // has to be dynamically allocated

#include "sd_card.h"
// include C64 browser code for SHIFT+RUN/STOP
#include "sdbrowser.h"

// ------------------------------------------------------------------------------
// ------------------------------------------------------------------------------

// Setup Teensy 4.1 IO's
//
void setup() {
  
  pinMode(PIN_CLK0,        INPUT);  
  pinMode(PIN_RESET,       INPUT);  
  pinMode(PIN_READY_n,     INPUT);  
  pinMode(PIN_IRQ,         INPUT);  
  pinMode(PIN_NMI,         INPUT);  
  pinMode(PIN_RDWR_n,      OUTPUT); 
  pinMode(PIN_P0,          OUTPUT); 
  pinMode(PIN_P1,          OUTPUT); 
  pinMode(PIN_P2,          OUTPUT); 
  
  pinMode(PIN_ADDR0,       OUTPUT); 
  pinMode(PIN_ADDR1,       OUTPUT); 
  pinMode(PIN_ADDR2,       OUTPUT); 
  pinMode(PIN_ADDR3,       OUTPUT); 
  pinMode(PIN_ADDR4,       OUTPUT); 
  pinMode(PIN_ADDR5,       OUTPUT); 
  pinMode(PIN_ADDR6,       OUTPUT); 
  pinMode(PIN_ADDR7,       OUTPUT);
  pinMode(PIN_ADDR8,       OUTPUT); 
  pinMode(PIN_ADDR9,       OUTPUT); 
  pinMode(PIN_ADDR10,      OUTPUT); 
  pinMode(PIN_ADDR11,      OUTPUT); 
  pinMode(PIN_ADDR12,      OUTPUT); 
  pinMode(PIN_ADDR13,      OUTPUT); 
  pinMode(PIN_ADDR14,      OUTPUT); 
  pinMode(PIN_ADDR15,      OUTPUT);  
  
  pinMode(PIN_DATAIN0,     INPUT); 
  pinMode(PIN_DATAIN1,     INPUT); 
  pinMode(PIN_DATAIN2,     INPUT); 
  pinMode(PIN_DATAIN3,     INPUT); 
  pinMode(PIN_DATAIN4,     INPUT); 
  pinMode(PIN_DATAIN5,     INPUT); 
  pinMode(PIN_DATAIN6,     INPUT); 
  pinMode(PIN_DATAIN7,     INPUT);
    
  pinMode(PIN_DATAOUT0,    OUTPUT); 
  pinMode(PIN_DATAOUT1,    OUTPUT); 
  pinMode(PIN_DATAOUT2,    OUTPUT); 
  pinMode(PIN_DATAOUT3,    OUTPUT); 
  pinMode(PIN_DATAOUT4,    OUTPUT); 
  pinMode(PIN_DATAOUT5,    OUTPUT); 
  pinMode(PIN_DATAOUT6,    OUTPUT); 
  pinMode(PIN_DATAOUT7,    OUTPUT);
  pinMode(PIN_DATAOUT_OE_n,  OUTPUT); 

  digitalWriteFast(PIN_RDWR_n, 0x1);

  // setup registers and state
  teensy64_registers[0x00] = 0x00; // no speedup
  teensy64_registers[0x01] = 0x02; // use mode 2 (read from IRAM, write to DRAM, enable CPU speedups) for speedup
  teensy64_registers[0x02] = 0x07; // enable mode 1, enable REU emulation, enable LOAD trap

  update_teensy64_setup();
  reu_init();

  write_cpu_port(7);

  Serial.begin(115200);

  sd_init();
}

void reu_init() {
  // allocate upper 256K bank in RAM1
  reu_bank1 = malloc(4*65536); // 256K
  memset(reu_bank0, 0, 4*65536);
  memset(reu_bank1, 0, 4*65536);
}

inline uint8_t reu_read_byte(uint32_t reu_addr) {
   uint32_t bankaddr = reu_addr & 0x0003ffff;
   bool highbank = reu_addr & 0x00040000;
   if (highbank) {
      return reu_bank1[bankaddr];
   } else {
      return reu_bank0[bankaddr];
   }
}

inline void reu_write_byte(uint32_t reu_addr, uint8_t b) {
   uint32_t bankaddr = reu_addr & 0x0003ffff;
   bool highbank = reu_addr & 0x00040000;
   if (highbank) {
      reu_bank1[bankaddr] = b;
   } else {
      reu_bank0[bankaddr] = b;
   }
}

void reu_execute(uint8_t op) {
  uint16_t c64_addr = reu_registers[2] + (reu_registers[3] << 8);
  uint32_t reu_addr = reu_registers[4] + (reu_registers[5] << 8) + (reu_registers[6] << 16);
  uint16_t reu_len  = reu_registers[7] + (reu_registers[8] << 8);
  uint8_t verify_error = 0;
  uint16_t verify_pos = 0;
  if (reu_len==0) reu_len = 0x10000; // full 64K
  Serial.print("REU:[");
  for (uint8_t i=0; i<11; i++) {
    Serial.print(reu_registers[i], HEX); Serial.print(" ");
  }
  Serial.print("] c64:$"); Serial.print(c64_addr, HEX); Serial.print(" REU:"); Serial.print(reu_addr, HEX); Serial.print(" len:$"); Serial.print(reu_len, HEX); Serial.print(" ");
  switch (op) {
    case 0x00:
	    Serial.println("C64->REU");
	    for (uint16_t i=0; i<reu_len; i++) {
	      reu_write_byte(reu_addr+i, read_byte(c64_addr+i));
	    }
	    break;
    case 0x01:
      Serial.println("REU->C64");
	    for (uint16_t i=0; i<reu_len; i++) {
	      write_byte(c64_addr+i, reu_read_byte(reu_addr+i));
	    }
	    break;
    case 0x02:
      Serial.println("swap");
	    for (uint16_t i=0; i<reu_len; i++) {
	      uint8_t b = reu_read_byte(reu_addr+i);
	      reu_write_byte(reu_addr+i, read_byte(c64_addr+i));
	      write_byte(c64_addr+i, b);
	    }
	    break;
    case 0x03:
      Serial.println("verify");
	    for (uint16_t i=0; i<reu_len; i++) {
	      if ((verify_error==0) && (reu_read_byte(reu_addr+i) != read_byte(c64_addr+i))) {
	        verify_error = 1;
	        verify_pos = i+1; // one past the difference
          break;
	      }
	    }
	    break;
  }
  if ((op==0x03) && verify_error) {
    reu_len = verify_pos; // this is where verify error happened
  }
  if ((reu_registers[1] & 0x20)==0) {
    // if no autoload then update addresses
    // XXX this is wrong, it's a 'FIX' during transfer, not after
    // after transfer with no autoload - length register is 1, addresses are one past the last copied byte
    switch (reu_registers[0x0a] & 0xc0) {
      case 0x00: // update both addresses
  	    reu_addr += reu_len;
  	    c64_addr += reu_len;
  	    break;
      case 0x80: // fix C64 addr, update REU addr
  	    reu_addr += reu_len;
  	    break;
      case 0x40: // fix REU addr, update C64 addr
  	    c64_addr += reu_len;
  	    break;
      case 0xc0: // fix both addresses
  	    break;
    }
  }
  // reload registers with original or updated addresses
  // XXX autoload shadow not emulated here
  reu_registers[2] = c64_addr & 0xff;
  reu_registers[3] = c64_addr >> 8;
  reu_registers[4] = reu_addr & 0xff;
  reu_registers[5] = (reu_addr >> 8) & 0xff;
  reu_registers[6] = (reu_addr >> 16) & 0xff;

  reu_registers[0] = (reu_registers[0] & 0xcf) | ((1 << 6) | (verify_error << 5)) ; // set bit 6, transfer complete; bit 5 - verify error
  return;
}

FASTRUN inline void update_teensy64_setup() {
  if (teensy64_registers[0x01]==0) teensy64_registers[0x01]=1; // correct speedup mode to 1, can't be 0
  if (teensy64_registers[0x02] & 0x01) {
    if (teensy64_registers[0x00] & 0x01) { // is speedup enabled?
      mode = teensy64_registers[0x01]; // yes, use specified speedup mode
    } else {
      mode = 1; // no, mode 1 (cycle exact with internal RAM/ROM/CRT)
    }
  } else {
    mode = 0; // without any speedup, pass all accesses to external bus
  }
  load_trap_enabled =     teensy64_registers[0x02] & 0x02;
  reu_emulation_enabled = teensy64_registers[0x02] & 0x04;
}

// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------
//
// Begin 6502 Bus Interface Unit 
//
// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------


// ----------------------------------------------------------
// Address range check
//  Return: 0x0 - All external memory accesses
//          0x1 - Reads and writes are cycle accurate using internal memory with writes passing through to motherboard
//          0x2 - Enable speedups, Reads accelerated using internal memory with writes passing through to motherboard
//          0x3 - Enable speedups, All read and write accesses use accelerated internal memory
// ----------------------------------------------------------
FASTRUN inline uint8_t internal_address_check(uint16_t local_address) {

  internal_io_address = false;
  if ((local_address >= 0xD000) && (local_address <= 0xDFFF)) {
    if (!io_enabled) return mode;
    // I/O enabled
    // is this one of intercepted areas?
    if ((local_address >= TEENSY64_REGISTER_BASE) && (local_address < (TEENSY64_REGISTER_BASE+TEENSY64_REGISTER_SIZE))) {
      internal_io_address = true;
      return mode;
    }
    if (reu_emulation_enabled && ((local_address >= REU_REGISTER_BASE) && (local_address <= (REU_REGISTER_BASE+0x100)))) {
      internal_io_address = true;
      return mode;
    }
    // real I/O
    return 0x0;
  }

  if (mode==0) return 0; // shortcut for external accesses

//  if ( (local_address > 0x0001 ) && (local_address <= 0x03FF) ) return mode;            //   Zero-Page up to video
//  if ( (local_address >= 0x0400) && (local_address <= 0x07FF) && mode>1) return 0x1;    //   C64 Video Memory // can't be 3 on boot
//  if ( (local_address >= 0x0800) && (local_address <= 0x7FFF) ) return mode;            //   C64 RAM
//  if ( (local_address >= 0x8000) && (local_address <= 0x9FFF) ) return mode;            //   C64 CART_LOW & RAM
//  if ( (local_address >= 0xA000) && (local_address <= 0xBFFF) ) return mode;            //   C64 BASIC ROM & RAM
//  if ( (local_address >= 0xC000) && (local_address <= 0xCFFF) ) return mode;            //   C64 RAM
//  if ( (local_address >= 0xD000) && (local_address <= 0xDFFF) ) return io_enabled ? 0x0 : mode ; //   C64 I/O // if I/O is enabled this must be r/w cycle-exact
//  if ( (local_address >= 0xE000) && (local_address <= 0xE4FF) ) return mode;            //   C64 KERNAL ROM
//  if ( (local_address >= 0xE500) && (local_address <= 0xFF7F) && mode>1) return 0x1;    //   C64 KERNAL ROM  // can't be 2 nor 3 on boot, why?
//  if ( (local_address >= 0xFF80) && (local_address <= 0xFFFF) ) return mode;            //   C64 KERNAL ROM

  return mode;

}


// -------------------------------------------------
// Wait for the CLK1 rising edge and sample signals
// -------------------------------------------------
FASTRUN inline void wait_for_CLK_rising_edge() {
  register uint32_t GPIO6_data=0;
  register uint32_t GPIO6_data_d1=0;
  uint32_t   d10, d2, d3, d4, d5, d76;

    cli();
    while (((GPIO6_DR >> 12) & 0x1)!=0) {}            // Teensy 4.1 Pin-24  GPIO6_DR[12]     CLK
    
    while (((GPIO6_DR >> 12) & 0x1)==0) {GPIO6_data=GPIO6_DR;}                  // This method is ok for VIC-20 and Apple-II+ non-DRAM ranges 
    sei();

    //do {  GPIO6_data_d1=GPIO6_DR;   } while (((GPIO6_data_d1 >> 12) & 0x1)==0);   // This method needed to support Apple-II+ DRAM read data setup time
    //GPIO6_data=GPIO6_data_d1;
    
    d10             = (GPIO6_data&0x000C0000) >> 18;  // Teensy 4.1 Pin-14  GPIO6_DR[19:18]  D1:D0
    d2              = (GPIO6_data&0x00800000) >> 21;  // Teensy 4.1 Pin-16  GPIO6_DR[23]     D2
    d3              = (GPIO6_data&0x00400000) >> 19;  // Teensy 4.1 Pin-17  GPIO6_DR[22]     D3
    d4              = (GPIO6_data&0x00020000) >> 13;  // Teensy 4.1 Pin-18  GPIO6_DR[17]     D4
    d5              = (GPIO6_data&0x00010000) >> 11;  // Teensy 4.1 Pin-19  GPIO6_DR[16]     D5
    d76             = (GPIO6_data&0x0C000000) >> 20;  // Teensy 4.1 Pin-20  GPIO6_DR[27:26]  D7:D6
    
    direct_irq      = (GPIO6_data&0x00002000) >> 13;  // Teensy 4.1 Pin-25  GPIO6_DR[13]     IRQ
    direct_ready_n  = (GPIO6_data&0x40000000) >> 30;  // Teensy 4.1 Pin-26  GPIO6_DR[30]     READY
    direct_reset    = (GPIO6_data&0x00100000) >> 20;  // Teensy 4.1 Pin-40  GPIO6_DR[20]     RESET
    direct_nmi      = (GPIO6_data&0x00200000) >> 21;  // Teensy 4.1 Pin-41  GPIO6_DR[21]     NMI
    
    direct_datain = d76 | d5 | d4 | d3 | d2 | d10;
    
    return; 
}


// -------------------------------------------------
// Wait for the CLK1 falling edge 
// -------------------------------------------------
FASTRUN inline void wait_for_CLK_falling_edge() {

  cli();
  while (((GPIO6_DR >> 12) & 0x1)==0) {}   // Teensy 4.1 Pin-24  GPIO6_DR[12]  CLK
  while (((GPIO6_DR >> 12) & 0x1)!=0) {}
  sei();
  return; 
}


// -------------------------------------------------
// Drive the 6502 Address pins
// -------------------------------------------------
FASTRUN inline void send_address(uint32_t local_address) {
  register uint32_t writeback_data=0;
  
    writeback_data = (0x6DFFFFF3 & GPIO6_DR);   // Read in current GPIOx register value and clear the bits we intend to update
    writeback_data = writeback_data | (local_address & 0x8000)<<10 ;  // 6502_Address[15]   TEENSY_PIN23   GPIO6_DR[25]
    writeback_data = writeback_data | (local_address & 0x2000)>>10 ;  // 6502_Address[13]   TEENSY_PIN0    GPIO6_DR[3]
    writeback_data = writeback_data | (local_address & 0x1000)>>10 ;  // 6502_Address[12]   TEENSY_PIN1    GPIO6_DR[2]
    writeback_data = writeback_data | (local_address & 0x0002)<<27 ;  // 6502_Address[1]    TEENSY_PIN38   GPIO6_DR[28]
    GPIO6_DR       = writeback_data | (local_address & 0x0001)<<31 ;  // 6502_Address[0]    TEENSY_PIN27   GPIO6_DR[31]
    
    writeback_data = (0xCFF3EFFF & GPIO7_DR);   // Read in current GPIOx register value and clear the bits we intend to update
    writeback_data = writeback_data | (local_address & 0x0400)<<2  ;  // 6502_Address[10]   TEENSY_PIN32   GPIO7_DR[12]
    writeback_data = writeback_data | (local_address & 0x0200)<<20 ;  // 6502_Address[9]    TEENSY_PIN34   GPIO7_DR[29]
    writeback_data = writeback_data | (local_address & 0x0080)<<21 ;  // 6502_Address[7]    TEENSY_PIN35   GPIO7_DR[28]
    writeback_data = writeback_data | (local_address & 0x0020)<<13 ;  // 6502_Address[5]    TEENSY_PIN36   GPIO7_DR[18]
    GPIO7_DR       = writeback_data | (local_address & 0x0008)<<16 ;  // 6502_Address[3]    TEENSY_PIN37   GPIO7_DR[19]
                
    writeback_data = (0xFF3BFFFF & GPIO8_DR);   // Read in current GPIOx register value and clear the bits we intend to update
    writeback_data = writeback_data | (local_address & 0x0100)<<14 ;  // 6502_Address[8]    TEENSY_PIN31   GPIO8_DR[22]
    writeback_data = writeback_data | (local_address & 0x0040)<<17 ;  // 6502_Address[6]    TEENSY_PIN30   GPIO8_DR[23]
    GPIO8_DR       = writeback_data | (local_address & 0x0004)<<16 ;  // 6502_Address[2]    TEENSY_PIN28   GPIO8_DR[18]
    
    writeback_data = (0x7FFFFF6F & GPIO9_DR);   // Read in current GPIOx register value and clear the bits we intend to update
    writeback_data = writeback_data | (local_address & 0x4000)>>10 ;  // 6502_Address[14]   TEENSY_PIN2    GPIO9_DR[4]
    writeback_data = writeback_data | (local_address & 0x0800)>>4  ;  // 6502_Address[11]   TEENSY_PIN33   GPIO9_DR[7]
    GPIO9_DR       = writeback_data | (local_address & 0x0010)<<27 ;  // 6502_Address[4]    TEENSY_PIN29   GPIO9_DR[31]
    
    return;
}

        
// -------------------------------------------------
// Send the address for a read cyle
// -------------------------------------------------
FASTRUN inline void start_read(uint32_t local_address) {

  current_address = local_address;
  current_address_mode = internal_address_check(current_address);

  if (current_address_mode>0x1) {
      //last_access_internal_RAM=1;
  } else {
    if (last_access_internal_RAM==1) wait_for_CLK_rising_edge();
    last_access_internal_RAM=0;

    digitalWriteFast(PIN_RDWR_n,  0x1);
    send_address(local_address);
  }
  return;
}



// -------------------------------------------------
// Fetch data from the correct Bank
// -------------------------------------------------
FASTRUN inline uint8_t fetch_byte_from_bank() {

    if (internal_io_address) {
      if ((current_address >= TEENSY64_REGISTER_BASE) && (current_address < (TEENSY64_REGISTER_BASE+TEENSY64_REGISTER_SIZE))) {
        return teensy64_registers[current_address-TEENSY64_REGISTER_BASE];
      }
      if (reu_emulation_enabled) {
        if ((current_address >= REU_REGISTER_BASE) && (current_address <= (REU_REGISTER_BASE+0x100))) {
          return reu_registers[current_address & 0x001f]; // 5 address bits connected, shadow every $20 bytes
        }
      }
    }

    if ((Page_128_159) && ((EXROM==1 && GAME==0) || (EXROM==0 && ((bank_mode & (HIRAM_BIT|LORAM_BIT))==(HIRAM_BIT|LORAM_BIT))))) {
      return CART_LOW_ROM[current_address & 0x1FFF];
    }

    if (Page_160_191) {
      if ( EXROM==0 && (bank_mode & HIRAM_BIT)) {
        return CART_HIGH_ROM[current_address & 0x1FFF];
      }
      if ( GAME==1 && ((bank_mode & (HIRAM_BIT|LORAM_BIT))==(HIRAM_BIT|LORAM_BIT))) {
        return BASIC_ROM[current_address & 0x1FFF];
      }
    }

    if (Page_224_255) {
      if (EXROM==1 && GAME==0) {
        return CART_HIGH_ROM[current_address & 0x1FFF];
      }
      if (bank_mode & HIRAM_BIT) {
        return KERNAL_ROM[current_address & 0x1FFF];
      }
    }
    return internal_RAM[current_address];
}


// -------------------------------------------------
// On the rising CLK edge, read in the data
// -------------------------------------------------
FASTRUN inline uint8_t finish_read_byte() {

  if (current_address_mode>0x1) {
    last_access_internal_RAM=1;
    return fetch_byte_from_bank();
  }

  if (last_access_internal_RAM==1) wait_for_CLK_rising_edge();
  last_access_internal_RAM=0;

  do {  wait_for_CLK_rising_edge();  }  while (direct_ready_n == 0x1);  // Delay a clock cycle until ready is active


  if (internal_io_address) return fetch_byte_from_bank();
  if (current_address==0x1) return read_cpu_port();
  if (current_address_mode==0) return direct_datain;

  return fetch_byte_from_bank();

}


// -------------------------------------------------
// Full read cycle with address and data read in
// -------------------------------------------------
inline uint8_t read_byte(uint16_t local_address) {

  current_address = local_address;
  current_address_mode = internal_address_check(current_address);

  if (current_address_mode>0x1) {
    last_access_internal_RAM=1;
    return fetch_byte_from_bank();
  }

  if (last_access_internal_RAM==1) wait_for_CLK_rising_edge();
  last_access_internal_RAM=0;

  digitalWriteFast(PIN_RDWR_n,  0x1);
  send_address(local_address);
  do {  wait_for_CLK_rising_edge();  }  while (direct_ready_n == 0x1);  // Delay a clock cycle until ready is active

  if (internal_io_address) return fetch_byte_from_bank();
  if (current_address==0x1) return read_cpu_port();
  if (current_address_mode==0) return direct_datain;
  return fetch_byte_from_bank();

}

// -------------------------------------------------
// CPU port and internal memory config update
// -------------------------------------------------

FASTRUN inline void write_cpu_port(uint8_t local_write_data) {
  digitalWriteFast(PIN_P0,  (local_write_data & 0x01) );
  digitalWriteFast(PIN_P1,  (local_write_data & 0x02) >> 1 );
  digitalWriteFast(PIN_P2,  (local_write_data & 0x04) >> 2 );
  current_p = local_write_data;
  bank_mode = (EXROM<<4) | (GAME<<3) | (current_p&0x7);
  // I/O is enabled if BASIC and KERNAL are not both unmapped
  io_enabled = ((current_p & CHAREN_BIT) && ((current_p & (HIRAM_BIT | LORAM_BIT)) != 0));
  return;
}

FASTRUN inline uint8_t read_cpu_port() {
  return current_p | 0x10;
}

// -------------------------------------------------
// Full write cycle with address and data written
// -------------------------------------------------
inline void write_byte(uint16_t local_address , uint8_t local_write_data) {

  // Teensy64 Control Registers, don't pass them to outside bus
  // if I/O is enabled and the address is one of the special adresses handle the control value
  if (io_enabled) {
    if ((local_address >= TEENSY64_REGISTER_BASE) && (local_address < (TEENSY64_REGISTER_BASE+TEENSY64_REGISTER_SIZE))) {
       internal_io_address = true;
       Serial.print("R: $"); Serial.print(local_address, HEX), Serial.print(" <- $"); Serial.println(local_write_data, HEX);
       switch(local_address) {
         case TEENSY64_REGISTER_BASE+0: teensy64_registers[0x00] = local_write_data & 0x01; break; // 1 bit
         case TEENSY64_REGISTER_BASE+1: teensy64_registers[0x01] = local_write_data & 0x03; break; // 1, 2 or 3, 0 will be filpped to 1
         case TEENSY64_REGISTER_BASE+2: teensy64_registers[0x02] = local_write_data & 0x07; break; // 3 bits
         default: break;
       }
       update_teensy64_setup();
       return;
    }
    if (reu_emulation_enabled) {
      if ((local_address >= REU_REGISTER_BASE) && (local_address <= (REU_REGISTER_BASE+0x100))) {
        internal_io_address = true;
        reu_registers[local_address & 0x001f] = local_write_data;
	      if (((local_address & 0x001f)==0x01) && (local_write_data & 0x80)) { // command register with 7th bit set?
	        reu_execute(local_write_data & 0x03);
	      }
        return;
      }
    }
  }
  // if I/O enabled but we don't write to I/O space
  // if I/O not enabled always write to RAM
  if ((!io_enabled) || (io_enabled && ((local_address <= 0xD000) || (local_address >=0xE000)))) { internal_RAM[local_address] = local_write_data; };

  // Internal RAM
  if (internal_address_check(local_address)>0x2)  {
    last_access_internal_RAM=1;
  }
  else 
  {
       if (last_access_internal_RAM==1) {
         last_access_internal_RAM=0;
         if (mode<2) {
            wait_for_CLK_rising_edge(); // in mode0 always write, no need to wait, other peripherals need to wait until last write cycle to halt CPU
         } else {
            // in mode 2 and higher we need to sync again, we can't write while VIC blocks the bus
            do {  wait_for_CLK_rising_edge();  }  while (direct_ready_n == 0x1);  // Delay a clock cycle until ready is active
         }
       }

       digitalWriteFast(PIN_RDWR_n,  0x0);
       send_address(local_address);

       
     // Drive the data bus pins from the Teensy to the bus driver which is inactive
     //
       digitalWriteFast(PIN_DATAOUT0,  (local_write_data & 0x01)    );
       digitalWriteFast(PIN_DATAOUT1,  (local_write_data & 0x02)>>1 ); 
       digitalWriteFast(PIN_DATAOUT2,  (local_write_data & 0x04)>>2 ); 
       digitalWriteFast(PIN_DATAOUT3,  (local_write_data & 0x08)>>3 ); 
       digitalWriteFast(PIN_DATAOUT4,  (local_write_data & 0x10)>>4 ); 
       digitalWriteFast(PIN_DATAOUT5,  (local_write_data & 0x20)>>5 ); 
       digitalWriteFast(PIN_DATAOUT6,  (local_write_data & 0x40)>>6 ); 
       digitalWriteFast(PIN_DATAOUT7,  (local_write_data & 0x80)>>7 ); 
       
       // During the second CLK phase, enable the data bus output drivers
       //
       wait_for_CLK_falling_edge();
       digitalWriteFast(PIN_DATAOUT_OE_n,  0x0 ); 
       
       wait_for_CLK_rising_edge();
       digitalWriteFast(PIN_DATAOUT_OE_n,  0x1 );   

       // restore read mode (needed?)
       digitalWriteFast(PIN_RDWR_n,  0x1);

  }
  // handle CPU port write
  if (local_address==0x1) write_cpu_port(local_write_data);
  return;
}

  
// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------
//
// End 6502 Bus Interface Unit
//
// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------


void push(uint8_t push_data) {    
    write_byte(register_sp_fixed, push_data); 
    register_sp = register_sp - 1;
    return;
}


uint8_t pop() {
    uint8_t temp=0;
    register_sp = register_sp + 1;
    temp = read_byte(register_sp_fixed);
    return temp;
}
    
  
void Calc_Flags_NEGATIVE_ZERO(uint8_t local_data) {
    
    if (0x80&local_data)   register_flags = register_flags | 0x80;              // Set the N flag
    else                   register_flags = register_flags & 0x7F;              // Clear the N flag
    
    if (local_data==0)     register_flags = register_flags | 0x02;              // Set the Z flag
    else                   register_flags = register_flags & 0xFD;              // Clear the Z flag 
    
    return;
}


uint16_t Sign_Extend16(uint16_t reg_data)  {
    if ((reg_data&0x0080)== 0x0080)   { return (reg_data | 0xFF00); } 
    else                              { return (reg_data & 0x00FF); }  
}


inline void Begin_Fetch_Next_Opcode()  {
    register_pc++;
    start_read(register_pc);
    return;
}


// -------------------------------------------------
// Addressing Modes
// -------------------------------------------------
inline uint8_t Fetch_Immediate()  {
    register_pc++;
    ea_data = read_byte(register_pc);
    return ea_data;
}

inline uint8_t Fetch_ZeroPage()  { 
    effective_address = Fetch_Immediate(); 
    ea_data = read_byte(effective_address);
    return ea_data;
}

inline uint8_t Fetch_ZeroPage_X()  {   
    uint16_t bal;
    bal = Fetch_Immediate();
    if (mode<2) read_byte(register_pc+1); 
    effective_address = (0x00FF & (bal + register_x));
    ea_data = read_byte(effective_address);
    return ea_data;
}

inline uint8_t Fetch_ZeroPage_Y()  {   
    uint16_t bal;
    bal = Fetch_Immediate();
    if (mode<2) read_byte(register_pc+1); 
    effective_address = (0x00FF & (bal + register_y)); 
    ea_data = read_byte(effective_address);
    return ea_data;
}

inline uint16_t Calculate_Absolute()  { 
    uint16_t adl, adh;

    adl = Fetch_Immediate();
    adh = Fetch_Immediate()<<8;
    effective_address = adl + adh;  
    return effective_address;
}

inline uint8_t Fetch_Absolute()  { 
    uint16_t adl, adh;

    adl = Fetch_Immediate();
    adh = Fetch_Immediate()<<8;
    effective_address = adl + adh;  
    ea_data = read_byte(effective_address);
    return ea_data;
}

inline uint8_t Fetch_Absolute_X(uint8_t page_cross_check)  {
    uint16_t bal, bah;
    
    bal = Fetch_Immediate();
    bah = Fetch_Immediate()<<8;
    effective_address = bah + bal + register_x;
    ea_data = read_byte(effective_address );
    
    if (  (mode<2) && page_cross_check==1 && (  (0xFF00&effective_address) != (0xFF00&bah) ) ) {  
        ea_data = read_byte(effective_address ); 
    }
    return ea_data;
}

inline uint8_t Fetch_Absolute_Y(uint8_t page_cross_check)  {
    uint16_t bal, bah;
    
    bal = Fetch_Immediate();
    bah = Fetch_Immediate()<<8;
    effective_address = bah + bal + register_y;
    ea_data = read_byte(effective_address );
    
    if ( (mode<2)  && page_cross_check==1 && (  (0xFF00&effective_address) != (0xFF00&bah) ) ) {  
        ea_data = read_byte(effective_address ); 
    } 
    return ea_data;
}

inline uint8_t Fetch_Indexed_Indirect_X()  { 
    uint16_t bal;
    uint16_t adl, adh;
    
    bal = Fetch_Immediate() + register_x;
    if (mode<2) read_byte(bal);
    adl = read_byte(0xFF&bal);
    adh = read_byte(0xFF&(bal+1)) << 8;
    effective_address = adh + adl ;
    ea_data = read_byte(effective_address);
    return ea_data;
}

inline uint8_t Fetch_Indexed_Indirect_Y(uint8_t page_cross_check)  {
    uint16_t ial, bah, bal;
    
    ial = Fetch_Immediate();
    bal = read_byte(0xFF&ial);
    bah = read_byte(0xFF&(ial+1)) << 8;
    
    effective_address = bah + bal + register_y;
    ea_data = read_byte(effective_address);
    
    if ( (mode<2) && page_cross_check==1 && ((0xFF00&effective_address) != (0xFF00&bah)) ) {  
        ea_data = read_byte(effective_address); 
    }
    return ea_data;
}


inline void Write_ZeroPage(uint8_t local_data)  {
    effective_address = Fetch_Immediate();
    write_byte(effective_address , local_data);
    return;
}

inline void Write_Absolute(uint8_t local_data)  {
    effective_address = Fetch_Immediate();
    effective_address = (Fetch_Immediate() << 8) + effective_address;
    write_byte(effective_address , local_data );
    return;
}

inline void Write_ZeroPage_X(uint8_t local_data)  {
    effective_address = Fetch_Immediate();
    if (mode<2) read_byte(effective_address);
    write_byte( (0x00FF&(effective_address + register_x)) , local_data );
    return;
}

inline void Write_ZeroPage_Y(uint8_t local_data)  {
    effective_address = Fetch_Immediate();
    if (mode<2) read_byte(effective_address);
    write_byte( (0x00FF&(effective_address + register_y)) , local_data );
    return;
}

inline void Write_Absolute_X(uint8_t local_data)  {
    uint16_t bal,bah;

    bal = Fetch_Immediate();
    bah = Fetch_Immediate()<<8;
    effective_address = bal + bah + register_x; 
    if (mode<2) read_byte(effective_address);
    write_byte(effective_address , local_data );  
    return;
}

inline void Write_Absolute_Y(uint8_t local_data)  {
    uint16_t bal,bah;

    bal = Fetch_Immediate();
    bah = Fetch_Immediate()<<8;
    effective_address = bal + bah + register_y;
    if (mode<2) read_byte(effective_address);
    write_byte(effective_address , local_data );  
    return;
}

inline void Write_Indexed_Indirect_X(uint8_t local_data)  {
    uint16_t bal;
    uint16_t adl, adh;

    bal = Fetch_Immediate();
    if (mode<2) read_byte(bal);
    adl = read_byte(0xFF&(bal+register_x));
    adh = read_byte(0xFF&(bal+register_x+1)) << 8;
    effective_address = adh + adl;
    write_byte(effective_address , local_data );
    return;
}

inline void Write_Indexed_Indirect_Y(uint8_t local_data)  {  
    uint16_t ial;
    uint16_t bal, bah;

    ial = Fetch_Immediate();
    bal = read_byte(ial);
    bah = read_byte(ial+1)<<8;
    effective_address = bah + bal + register_y;
    if (mode<2) read_byte(effective_address);
    write_byte(effective_address , local_data );
    return;
}

inline void Double_WriteBack(uint8_t local_data)  {  
    if (mode<2)  write_byte(effective_address , ea_data);
    write_byte(effective_address , local_data);
    return;
}


// -------------------------------------------------
// Reset sequence for the 6502
// -------------------------------------------------
void reset_sequence() {
    uint16_t temp1, temp2;
       
    while (digitalReadFast(PIN_RESET)!=0) {}                        // Stay here until RESET deasserts
            
                
    digitalWriteFast(PIN_RDWR_n,  0x1);         
    digitalWriteFast(PIN_DATAOUT_OE_n,  0x1 );  

    write_cpu_port(7);

    temp1 = read_byte(register_pc);                                 // Address ??
    temp1 = read_byte(register_pc+1);                               // Address ?? + 1
    temp1 = read_byte(register_sp_fixed);                           // Address SP
    temp1 = read_byte(register_sp_fixed-1);                         // Address SP - 1
    temp1 = read_byte(register_sp_fixed-2);                         // Address SP - 2
                
    temp1 = read_byte(0xFFFC);                                      // Fetch Vector PCL
    temp2 = read_byte(0xFFFD);                                      // Fetch Vector PCH
                
    register_flags = 0x34;                                          // Set the I and B flags
            
    register_pc = (temp2<<8) | temp1;    
    start_read(register_pc);                                        // Fetch first opcode at vector PCH,PCL
    
    
    return;
}


// -------------------------------------------------
// NMI Interrupt Processing
// -------------------------------------------------
void nmi_handler() {
    uint16_t temp1, temp2;
    
    wait_for_CLK_rising_edge();                                     // Begin processing on next CLK edge
    
    register_flags = register_flags | 0x20;                         // Set the flag[5]          
    register_flags = register_flags & 0xEF;                         // Clear the B flag     
    
    if (mode<2) read_byte(register_pc+1);                          // Fetch PC+1 (Discard)
    push(register_pc>>8);                                           // Push PCH
    push(register_pc);                                              // Push PCL
    push(register_flags);                                           // Push P
    temp1 = read_byte(0xFFFA);                                      // Fetch Vector PCL
    temp2 = read_byte(0xFFFB);                                      // Fetch Vector PCH
                
    register_flags = register_flags | 0x34;                         // Set the I flag and restore the B flag

    register_pc = (temp2<<8) | temp1;           
    start_read(register_pc);                                        // Fetch first opcode at vector PCH,PCL
    
    return;
}
    

// -------------------------------------------------
// BRK & IRQ Interrupt Processing
// -------------------------------------------------
void irq_handler(uint8_t opcode_is_brk) {
    uint16_t temp1, temp2;
    
    wait_for_CLK_rising_edge();                                     // Begin processing on next CLK edge
                        
    register_flags = register_flags | 0x20;                         // Set the flag[5]          
    if (opcode_is_brk==1) register_flags = register_flags | 0x10;   // Set the B flag
    else                  register_flags = register_flags & 0xEF;   // Clear the B flag
    
    if (mode<2) read_byte(register_pc+1);                          // Fetch PC+1 (Discard)
    push(register_pc>>8);                                           // Push PCH
    push(register_pc);                                              // Push PCL
    push(register_flags);                                           // Push P
    temp1 = read_byte(0xFFFE);                                      // Fetch Vector PCL
    temp2 = read_byte(0xFFFF);                                      // Fetch Vector PCH
                
    register_flags = register_flags | 0x34;                         // Set the I flag and restore the B flag
                
    register_pc = (temp2<<8) | temp1;           
    start_read(register_pc);                                        // Fetch first opcode at vector PCH,PCL
    
    return;
}
    

// -------------------------------------------------
//
//               6502 Opcodes
//
// -------------------------------------------------

// -------------------------------------------------
// 0x0A - ASL A - Arithmetic Shift Left - Accumulator
// -------------------------------------------------
void opcode_0x0A() {
    
    if (mode<2) read_byte(register_pc);        
    Begin_Fetch_Next_Opcode();
     
    if (0x80&register_a)   register_flags = register_flags | 0x01;              // Set the C flag
    else                   register_flags = register_flags & 0xFE;              // Clear the C flag
    
    register_a = register_a << 1;
    
    Calc_Flags_NEGATIVE_ZERO(register_a);
    return;
}



// -------------------------------------------------
// 0x4A - LSR A - Logical Shift Right - Accumulator
// -------------------------------------------------
void opcode_0x4A() {
    
    if (mode<2) read_byte(register_pc);        
    Begin_Fetch_Next_Opcode();
    
    if (0x01&register_a)   register_flags = register_flags | 0x01;              // Set the C flag
    else                   register_flags = register_flags & 0xFE;              // Clear the C flag
    
    register_a = register_a >> 1;

    Calc_Flags_NEGATIVE_ZERO(register_a);
    return;
}

// -------------------------------------------------
// 0x6A - ROR A - Rotate Right - Accumulator
// -------------------------------------------------
void opcode_0x6A() {

  uint8_t old_carry_flag=0;
    
    if (mode<2) read_byte(register_pc);        
    Begin_Fetch_Next_Opcode();
    
    old_carry_flag = register_flags << 7;                                       // Shift the old carry flag to bit[8] to be rotated in
    
    if (0x01&register_a)   register_flags = register_flags | 0x01;              // Set the C flag
    else                   register_flags = register_flags & 0xFE;              // Clear the C flag
    
    register_a = ( old_carry_flag | (register_a>>1) );

    Calc_Flags_NEGATIVE_ZERO(register_a);
    return;
}


// -------------------------------------------------
// 0x2A - ROL A - Rotate Left - Accumulator
// -------------------------------------------------
void opcode_0x2A() {

  uint8_t old_carry_flag=0;

    if (mode<2) read_byte(register_pc);        
    Begin_Fetch_Next_Opcode();
    
    old_carry_flag = 0x1 & register_flags;                                      // Store the old carry flag to be rotated in
    
    
    if (0x80&register_a)   register_flags = register_flags | 0x01;              // Set the C flag
    else                   register_flags = register_flags & 0xFE;              // Clear the C flag
    
    register_a = (register_a<<1) | old_carry_flag;

    Calc_Flags_NEGATIVE_ZERO(register_a);
    return;
}


// -------------------------------------------------
// ADC 
// -------------------------------------------------
void Calculate_ADC(uint16_t local_data)  { 
    uint16_t total=0;
    uint16_t bcd_low=0;
    uint16_t bcd_high=0;
    uint16_t bcd_total=0;
    uint8_t operand0=0;
    uint8_t operand1=0;
    uint8_t result=0;
    uint8_t low_carry=0;
    uint8_t high_carry=0;
    
    Begin_Fetch_Next_Opcode();
    
    if ((flag_d)==1) {  
        bcd_low = (0x0F&register_a) + (0x0F&local_data) + (flag_c) ;    
        if (bcd_low>0x9) { low_carry=0x10; bcd_low = bcd_low - 0xA ; } 
            
        bcd_high = (0xF0&register_a) + (0xF0&local_data) + low_carry;   
        if (bcd_high>0x90) { high_carry=1;  bcd_high = bcd_high - 0xA0 ; } 
        
        register_flags = register_flags & 0xFE;              // Clear the C flag
        if ((0x00FF&bcd_total) > 0x09) { bcd_total=bcd_total+0x010; bcd_total=bcd_total-0x0A; }    

        if (high_carry==1) { bcd_total=bcd_total-0xA0; register_flags = register_flags | 0x01;  }           // Set the C flag
        else                                           register_flags = register_flags & 0xFE;              // Clear the C flag     
        
        total = (0xFF & (bcd_low + bcd_high));
    }
    
    else {
        total =  register_a + local_data + (flag_c);
    
        if (total>255)    register_flags = register_flags | 0x01;              // Set the C flag
        else              register_flags = register_flags & 0xFE;              // Clear the C flag
    }
    
    
    operand0 = (register_a & 0x80);  
    operand1 = (local_data & 0x80);  
    result   = (total      & 0x80); 

    if      (operand0==0 && operand1==0 && result!=0)  register_flags = register_flags | 0x40;              // Set the V flag
    else if (operand0!=0 && operand1!=0 && result==0)  register_flags = register_flags | 0x40;  
    else                                               register_flags = register_flags & 0xBF;              // Clear the V flag
    
    register_a = (0xFF & total);
    Calc_Flags_NEGATIVE_ZERO(register_a);
    
    return;
}
void opcode_0x69() { Calculate_ADC(Fetch_Immediate());             return;  }  // 0x69 - ADC - Immediate - Binary
void opcode_0x65() { Calculate_ADC(Fetch_ZeroPage());              return;  }  // 0x65 - ADC - ZeroPage
void opcode_0x75() { Calculate_ADC(Fetch_ZeroPage_X());            return;  }  // 0x75 - ADC - ZeroPage , X
void opcode_0x6D() { Calculate_ADC(Fetch_Absolute());              return;  }  // 0x6D - ADC - Absolute
void opcode_0x7D() { Calculate_ADC(Fetch_Absolute_X(1));           return;  }  // 0x7D - ADC - Absolute , X
void opcode_0x79() { Calculate_ADC(Fetch_Absolute_Y(1));           return;  }  // 0x79 - ADC - Absolute , Y
void opcode_0x61() { Calculate_ADC(Fetch_Indexed_Indirect_X());    return;  }  // 0x61 - ADC - Indexed Indirect X
void opcode_0x71() { Calculate_ADC(Fetch_Indexed_Indirect_Y(1));   return;  }  // 0x71 - ADC - Indirect Indexed  Y



// -------------------------------------------------
// SBC 
// -------------------------------------------------
void Calculate_SBC(uint16_t local_data)  { 
    uint16_t total=0;
    uint16_t bcd_low=0;
    uint16_t bcd_high=0;
    uint16_t bcd_total=0;
    int16_t signed_total=0;
    uint8_t operand0=0;
    uint8_t operand1=0;
    uint8_t result=0;
    uint8_t flag_c_invert=0;
    uint8_t low_carry=0;
    uint8_t high_carry=0;
    
    Begin_Fetch_Next_Opcode();
    
    if (flag_c!=0) flag_c_invert=0; else flag_c_invert=1;
    
    if ((flag_d)==1) {  
        bcd_low = (0x0F&register_a) - (0x0F&local_data) - flag_c_invert ;   
        if (bcd_low>0x9) { low_carry=0x10; bcd_low = bcd_low + 0xA ; } 
            
        bcd_high = (0xF0&register_a) - (0xF0&local_data) - low_carry;    
        if (bcd_high>0x90) { high_carry=1;  bcd_high = bcd_high + 0xA0 ; } 
        
        register_flags = register_flags & 0xFE;              // Clear the C flag
        if ((0x00FF&bcd_total) > 0x09) { bcd_total=bcd_total+0x010; bcd_total=bcd_total-0x0A; }    

        if (high_carry==0) { bcd_total=bcd_total-0xA0; register_flags = register_flags | 0x01;  }           // Set the C flag
        else                                           register_flags = register_flags & 0xFE;              // Clear the C flag     
        
        total = (0xFF & (bcd_low + bcd_high));
    }
    
    else {  

    total =  register_a - local_data - flag_c_invert;
    signed_total = (int16_t)register_a - (int16_t)(local_data ) - flag_c_invert;

    
    if (signed_total>=0)    register_flags = register_flags | 0x01;                                         // Set the C flag
    else                    register_flags = register_flags & 0xFE;                                         // Clear the C flag  
    }
    
    
    operand0 = (register_a & 0x80);  
    operand1 = (local_data & 0x80);  
    result   = (total      & 0x80); 

    if      (operand0==0 && operand1!=0 && result!=0)  register_flags = register_flags | 0x40;              // Set the V flag
    else if (operand0!=0 && operand1==0 && result==0)  register_flags = register_flags | 0x40;  
    else                                               register_flags = register_flags & 0xBF;              // Clear the V flag


    register_a = (0xFF & total);
    Calc_Flags_NEGATIVE_ZERO(register_a);
    
    return;
}
void opcode_0xE9() { Calculate_SBC(Fetch_Immediate());             return;  }  // 0xE9 - SBC - Immediate
void opcode_0xE5() { Calculate_SBC(Fetch_ZeroPage());              return;  }  // 0xE5 - SBC - ZeroPage
void opcode_0xF5() { Calculate_SBC(Fetch_ZeroPage_X());            return;  }  // 0xF5 - SBC - ZeroPage , X
void opcode_0xED() { Calculate_SBC(Fetch_Absolute());              return;  }  // 0xED - SBC - Absolute
void opcode_0xFD() { Calculate_SBC(Fetch_Absolute_X(1));           return;  }  // 0xFD - SBC - Absolute , X
void opcode_0xF9() { Calculate_SBC(Fetch_Absolute_Y(1));           return;  }  // 0xF9 - SBC - Absolute , Y
void opcode_0xE1() { Calculate_SBC(Fetch_Indexed_Indirect_X());    return;  }  // 0xE1 - SBC - Indexed Indirect X
void opcode_0xF1() { Calculate_SBC(Fetch_Indexed_Indirect_Y(1));   return;  }  // 0xF1 - SBC - Indirect Indexed  Y


// -------------------------------------------------
// Flag set/resets and NOP
// -------------------------------------------------
void opcode_0xEA() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();                                        return;  }  // 0xEA - NOP   
void opcode_0x18() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_flags=register_flags&0xFE;   return;  }  // 0x18 - CLC - Clear Carry Flag  
void opcode_0xD8() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_flags=register_flags&0xF7;   return;  }  // 0xD8 - CLD - Clear Decimal Mode  
void opcode_0x58() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_flags=register_flags&0xFB;   return;  }  // 0x58 - CLI - Clear Interrupt Flag  
void opcode_0xB8() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_flags=register_flags&0xBF;   return;  }  // 0xB8 - CLV - Clear Overflow Flag  
void opcode_0x38() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_flags=register_flags|0x01;   return;  }  // 0x38 - SEC - Set Carry Flag  
void opcode_0x78() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_flags=register_flags|0x04;   return;  }  // 0x78 - SEI - Set Interrupt Flag  
void opcode_0xF8() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_flags=register_flags|0x08;   return;  }  // 0xF8 - SED - Set Decimal Mode  


// -------------------------------------------------
// Increment/Decrements
// -------------------------------------------------
void opcode_0xCA() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_x=register_x-1;   Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xCA - DEX - Decrement X  
void opcode_0x88() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_y=register_y-1;   Calc_Flags_NEGATIVE_ZERO(register_y); return;  }  // 0x88 - DEY - Decrement Y  
void opcode_0xE8() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_x=register_x+1;   Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xE8 - INX - Increment X  
void opcode_0xC8() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_y=register_y+1;   Calc_Flags_NEGATIVE_ZERO(register_y); return;  }  // 0xC8 - INY - Increment Y  


// -------------------------------------------------
// Transfers
// -------------------------------------------------
void opcode_0xAA() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_x=register_a;   Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xAA - TAX - Transfer Accumulator to X 
void opcode_0xA8() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_y=register_a;   Calc_Flags_NEGATIVE_ZERO(register_y); return;  }  // 0xA8 - TAY - Transfer Accumulator to Y
void opcode_0xBA() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_x=register_sp;  Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xBA - TSX - Transfer Stack Pointer to X
void opcode_0x8A() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_a=register_x;   Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x8A - TXA - Transfer X to Accumulator
void opcode_0x9A() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_sp=register_x;                                        return;  }  // 0x9A - TXS - Transfer X to Stack Pointer
void opcode_0x98() {  if (mode<2) read_byte(register_pc+1);  Begin_Fetch_Next_Opcode();  register_a=register_y;   Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x98 - TYA - Transfer Y to Accumulator


// -------------------------------------------------
// PUSH/POP Flags and Accumulator 
// -------------------------------------------------
void opcode_0x08() {  if (mode<2) read_byte(register_pc+1);  push(register_flags|0x30);                                                                  Begin_Fetch_Next_Opcode();  return;  }  // 0x08 - PHP - Push Flags to Stack
void opcode_0x48() {  if (mode<2) read_byte(register_pc+1);  push(register_a);                                                                           Begin_Fetch_Next_Opcode();  return;  }  // 0x48 - PHA - Push Accumulator to the stack
void opcode_0x28() {  if (mode<2) { read_byte(register_pc+1);  read_byte(register_sp_fixed); }; register_flags=(pop()|0x30);                             Begin_Fetch_Next_Opcode();  return;  }  // 0x28 - PLP - Pop Flags from Stack
void opcode_0x68() {  if (mode<2) { read_byte(register_pc+1);  read_byte(register_sp_fixed); }; register_a=pop();  Calc_Flags_NEGATIVE_ZERO(register_a); Begin_Fetch_Next_Opcode();  return;  }  // 0x68 - PLA - Pop Accumulator from Stack


// -------------------------------------------------
// AND
// -------------------------------------------------
void opcode_0x29() { register_a=register_a&(Fetch_Immediate());           Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x29 - AND - Immediate
void opcode_0x25() { register_a=register_a&(Fetch_ZeroPage());            Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x25 - AND - ZeroPage
void opcode_0x35() { register_a=register_a&(Fetch_ZeroPage_X());          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x35 - AND - ZeroPage , X
void opcode_0x2D() { register_a=register_a&(Fetch_Absolute());            Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x2D - AND - Absolute
void opcode_0x3D() { register_a=register_a&(Fetch_Absolute_X(1));         Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x3D - AND - Absolute , X
void opcode_0x39() { register_a=register_a&(Fetch_Absolute_Y(1));         Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x19 - OR - Absolute , Y
void opcode_0x21() { register_a=register_a&(Fetch_Indexed_Indirect_X());  Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x21 - AND - Indexed Indirect X
void opcode_0x31() { register_a=register_a&(Fetch_Indexed_Indirect_Y(1)); Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x31 - AND - Indirect Indexed  Y


// -------------------------------------------------
// ORA
// -------------------------------------------------
void opcode_0x09() { register_a=register_a|(Fetch_Immediate());            Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x09 - OR - Immediate
void opcode_0x05() { register_a=register_a|(Fetch_ZeroPage());             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x05 - OR - ZeroPage
void opcode_0x15() { register_a=register_a|(Fetch_ZeroPage_X());           Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x15 - OR - ZeroPage , X
void opcode_0x0D() { register_a=register_a|(Fetch_Absolute());             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x0D - OR - Absolute
void opcode_0x1D() { register_a=register_a|(Fetch_Absolute_X(1));          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x1D - OR - Absolute , X
void opcode_0x19() { register_a=register_a|(Fetch_Absolute_Y(1));          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x19 - OR - Absolute , Y
void opcode_0x01() { register_a=register_a|(Fetch_Indexed_Indirect_X());   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x01 - OR - Indexed Indirect X
void opcode_0x11() { register_a=register_a|(Fetch_Indexed_Indirect_Y(1));  Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a);  return;  }  // 0x11 - OR - Indirect Indexed  Y


// -------------------------------------------------
// EOR
// -------------------------------------------------
void opcode_0x49() { register_a=register_a^(Fetch_Immediate());            Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x49 - EOR - Immediate
void opcode_0x45() { register_a=register_a^(Fetch_ZeroPage());             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x45 - EOR - ZeroPage
void opcode_0x55() { register_a=register_a^(Fetch_ZeroPage_X());           Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x55 - EOR - ZeroPage , X
void opcode_0x4D() { register_a=register_a^(Fetch_Absolute());             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x4D - EOR - Absolute
void opcode_0x5D() { register_a=register_a^(Fetch_Absolute_X(1));          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x5D - EOR - Absolute , X
void opcode_0x59() { register_a=register_a^(Fetch_Absolute_Y(1));          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x59 - EOR - Absolute , Y
void opcode_0x41() { register_a=register_a^(Fetch_Indexed_Indirect_X());   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x41 - EOR - Indexed Indirect X
void opcode_0x51() { register_a=register_a^(Fetch_Indexed_Indirect_Y(1));  Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0x51 - EOR - Indirect Indexed  Y


// -------------------------------------------------
// LDA
// -------------------------------------------------
void opcode_0xA9() { register_a=Fetch_Immediate();            Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xA9 - LDA - Immediate
void opcode_0xA5() { register_a=Fetch_ZeroPage();             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xA5 - LDA - ZeroPage
void opcode_0xB5() { register_a=Fetch_ZeroPage_X();           Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xB5 - LDA - ZeroPage , X
void opcode_0xAD() { register_a=Fetch_Absolute();             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xAD - LDA - Absolute
void opcode_0xBD() { register_a=Fetch_Absolute_X(1);          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xBD - LDA - Absolute , X
void opcode_0xB9() { register_a=Fetch_Absolute_Y(1);          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xB9 - LDA - Absolute , Y
void opcode_0xA1() { register_a=Fetch_Indexed_Indirect_X();   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xA1 - LDA - Indexed Indirect X
void opcode_0xB1() { register_a=Fetch_Indexed_Indirect_Y(1);  Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xB1 - LDA - Indirect Indexed  Y


// -------------------------------------------------
// LDX
// -------------------------------------------------
void opcode_0xA2() { register_x=Fetch_Immediate();            Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xA2 - LDX - Immediate
void opcode_0xA6() { register_x=Fetch_ZeroPage();             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xA6 - LDX - ZeroPage
void opcode_0xB6() { register_x=Fetch_ZeroPage_Y();           Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xB6 - LDX - ZeroPage , Y
void opcode_0xAE() { register_x=Fetch_Absolute();             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xAE - LDX - Absolute
void opcode_0xBE() { register_x=Fetch_Absolute_Y(1);          Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_x); return;  }  // 0xBE - LDX - Absolute , Y
                                                              
                                                              
// -------------------------------------------------          
// LDY                                                        
// -------------------------------------------------          
void opcode_0xA0() { register_y=Fetch_Immediate();             Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_y);return;  }  // 0xA0 - LDY - Immediate
void opcode_0xA4() { register_y=Fetch_ZeroPage();              Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_y);return;  }  // 0xA4 - LDY - ZeroPage
void opcode_0xB4() { register_y=Fetch_ZeroPage_X();            Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_y);return;  }  // 0xB4 - LDY - ZeroPage , X
void opcode_0xAC() { register_y=Fetch_Absolute();              Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_y);return;  }  // 0xAC - LDY - Absolute
void opcode_0xBC() { register_y=Fetch_Absolute_X(1);           Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_y);return;  }  // 0xBC - LDY - Absolute , X


// -------------------------------------------------
// BIT 
// -------------------------------------------------
void Calculate_BIT(uint8_t local_data)  {
    uint8_t temp=0;
    
    Begin_Fetch_Next_Opcode();
    
    register_flags = (register_flags & 0x3F) | (local_data & 0xC0);             // Copy fetched memory[7:6] to C,V flags
    
    temp = local_data & register_a;
    if (temp==0)           register_flags = register_flags | 0x02;              // Set the Z flag
    else                   register_flags = register_flags & 0xFD;              // Clear the Z flag 
    
    
    return;
}
void opcode_0x24() { Calculate_BIT(Fetch_ZeroPage());    return;  }             // 0x24 - BIT - ZeroPage
void opcode_0x2C() { Calculate_BIT(Fetch_Absolute());    return;  }             // 0x2C - BIT - Absolute


// -------------------------------------------------
// CMP 
// -------------------------------------------------
void Calculate_CMP(uint8_t local_data)  {
    int16_t temp=0;
    
    Begin_Fetch_Next_Opcode();
    
    temp = register_a - local_data;
    

    if (register_a >= local_data) register_flags = register_flags | 0x01;       // Set the C flag
    else                          register_flags = register_flags & 0xFE;       // Clear the C flag  
    
    Calc_Flags_NEGATIVE_ZERO(temp);
    return;
}
void opcode_0xC9() {  Calculate_CMP(Fetch_Immediate());             return;  }  // 0xC9 - CMP - Immediate
void opcode_0xC5() {  Calculate_CMP(Fetch_ZeroPage());              return;  }  // 0xC5 - CMP - ZeroPage
void opcode_0xD5() {  Calculate_CMP(Fetch_ZeroPage_X());            return;  }  // 0xD5 - CMP - ZeroPage , X
void opcode_0xCD() {  Calculate_CMP(Fetch_Absolute());              return;  }  // 0xCD - CMP - Absolute
void opcode_0xDD() {  Calculate_CMP(Fetch_Absolute_X(1));           return;  }  // 0xDD - CMP - Absolute , X
void opcode_0xD9() {  Calculate_CMP(Fetch_Absolute_Y(1));           return;  }  // 0xD9 - CMP - Absolute , Y
void opcode_0xC1() {  Calculate_CMP(Fetch_Indexed_Indirect_X());    return;  }  // 0xC1 - CMP - Indexed Indirect X
void opcode_0xD1() {  Calculate_CMP(Fetch_Indexed_Indirect_Y(1));   return;  }  // 0xD1 - CMP - Indirect Indexed  Y


// -------------------------------------------------
// CPX 
// -------------------------------------------------
void Calculate_CPX(uint8_t local_data)  {
    uint16_t temp=0;
    
    Begin_Fetch_Next_Opcode();
    
    temp = register_x - local_data;

    if (register_x >= local_data) register_flags = register_flags | 0x01;       // Set the C flag
    else                          register_flags = register_flags & 0xFE;       // Clear the C flag  
    
    Calc_Flags_NEGATIVE_ZERO(temp);
    return;
}
void opcode_0xE0() {  Calculate_CPX(Fetch_Immediate());   return;  }  // 0xE0 - CPX - Immediate
void opcode_0xE4() {  Calculate_CPX(Fetch_ZeroPage());    return;  }  // 0xE4 - CPX - ZeroPage
void opcode_0xEC() {  Calculate_CPX(Fetch_Absolute());    return;  }  // 0xEC - CPX - Absolute


// -------------------------------------------------
// CPY
// -------------------------------------------------
void Calculate_CPY(uint8_t local_data)  {
    uint16_t temp=0;
    
    Begin_Fetch_Next_Opcode();
    
    temp = register_y - local_data; 

    if (register_y >= local_data) register_flags = register_flags | 0x01;       // Set the C flag
    else                          register_flags = register_flags & 0xFE;       // Clear the C flag  
    
    Calc_Flags_NEGATIVE_ZERO(temp);
    return;
}
void opcode_0xC0() {  Calculate_CPY(Fetch_Immediate());    return;  }  // 0xC0 - CPY - Immediate
void opcode_0xC4() {  Calculate_CPY(Fetch_ZeroPage());     return;  }  // 0xC4 - CPY - ZeroPage
void opcode_0xCC() {  Calculate_CPY(Fetch_Absolute());     return;  }  // 0xCC - CPY - Absolute


// -------------------------------------------------
// Store Operations
// -------------------------------------------------
void opcode_0x85() {  Write_ZeroPage(register_a);            Begin_Fetch_Next_Opcode();  return;  }  // 0x85 - STA - ZeroPage
void opcode_0x8D() {  Write_Absolute(register_a);            Begin_Fetch_Next_Opcode();  return;  }  // 0x8D - STA - Absolute
void opcode_0x95() {  Write_ZeroPage_X(register_a);          Begin_Fetch_Next_Opcode();  return;  }  // 0x95 - STA - ZeroPage , X
void opcode_0x9D() {  Write_Absolute_X(register_a);          Begin_Fetch_Next_Opcode();  return;  }  // 0x9D - STA - Absolute , X
void opcode_0x99() {  Write_Absolute_Y(register_a);          Begin_Fetch_Next_Opcode();  return;  }  // 0x99 - STA - Absolute , Y
void opcode_0x81() {  Write_Indexed_Indirect_X(register_a);  Begin_Fetch_Next_Opcode();  return;  }  // 0x81 - STA - Indexed Indirect X
void opcode_0x91() {  Write_Indexed_Indirect_Y(register_a);  Begin_Fetch_Next_Opcode();  return;  }  // 0x91 - STA - Indirect Indexed  Y
void opcode_0x86() {  Write_ZeroPage(register_x);            Begin_Fetch_Next_Opcode();  return;  }  // 0x86 - STX - ZeroPage
void opcode_0x96() {  Write_ZeroPage_Y(register_x);          Begin_Fetch_Next_Opcode();  return;  }  // 0x96 - STX - ZeroPage , Y
void opcode_0x8E() {  Write_Absolute(register_x);            Begin_Fetch_Next_Opcode();  return;  }  // 0x8E - STX - Absolute
void opcode_0x84() {  Write_ZeroPage(register_y);            Begin_Fetch_Next_Opcode();  return;  }  // 0x84 - STY - ZeroPage
void opcode_0x94() {  Write_ZeroPage_X(register_y);          Begin_Fetch_Next_Opcode();  return;  }  // 0x94 - STY - ZeroPage , X
void opcode_0x8C() {  Write_Absolute(register_y);            Begin_Fetch_Next_Opcode();  return;  }  // 0x8C - STY - Absolute


// -------------------------------------------------
// ASL - Arithmetic Shift Left - Memory
// -------------------------------------------------
uint8_t Calculate_ASL(uint8_t local_data) {

    if ((0x80&local_data)==0x80)   register_flags = register_flags | 0x01;              // Set the C flag
    else                           register_flags = register_flags & 0xFE;              // Clear the C flag
    
    local_data = ((local_data << 1) & 0xFE);

    Calc_Flags_NEGATIVE_ZERO(local_data);
    return local_data;
}


// -------------------------------------------------
// ASL - Read-modify-write Operations
// -------------------------------------------------
void opcode_0x06() {  Double_WriteBack(Calculate_ASL(Fetch_ZeroPage()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x06 - ASL  - Arithmetic Shift Left - ZeroPage
void opcode_0x16() {  Double_WriteBack(Calculate_ASL(Fetch_ZeroPage_X()));   Begin_Fetch_Next_Opcode();  return;  }  // 0x16 - ASL  - Arithmetic Shift Left - ZeroPage , X
void opcode_0x0E() {  Double_WriteBack(Calculate_ASL(Fetch_Absolute()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x0E - ASL  - Arithmetic Shift Left - Absolute
void opcode_0x1E() {  Double_WriteBack(Calculate_ASL(Fetch_Absolute_X(0)));  Begin_Fetch_Next_Opcode();  return;  }  // 0x1E - ASL  - Arithmetic Shift Left - Absolute , X


// -------------------------------------------------
// INC - Memory
// -------------------------------------------------
uint8_t Calculate_INC(uint8_t local_data) {

    local_data = local_data + 1;
    global_temp = local_data;
    Calc_Flags_NEGATIVE_ZERO(local_data);
    return local_data;
}

void opcode_0xE6() {  Double_WriteBack(Calculate_INC(Fetch_ZeroPage()));     Begin_Fetch_Next_Opcode();  return;  }  // 0xE6 - INC - ZeroPage
void opcode_0xF6() {  Double_WriteBack(Calculate_INC(Fetch_ZeroPage_X()));   Begin_Fetch_Next_Opcode();  return;  }  // 0xF6 - INC - ZeroPage , X
void opcode_0xEE() {  Double_WriteBack(Calculate_INC(Fetch_Absolute()));     Begin_Fetch_Next_Opcode();  return;  }  // 0xEE - INC - Absolute
void opcode_0xFE() {  Double_WriteBack(Calculate_INC(Fetch_Absolute_X(0)));  Begin_Fetch_Next_Opcode();  return;  }  // 0xFE - INC - Absolute , X


// -------------------------------------------------
// DEC - Memory
// -------------------------------------------------
uint8_t Calculate_DEC(uint8_t local_data) {

    local_data = local_data - 1;
    global_temp = local_data;
    Calc_Flags_NEGATIVE_ZERO(local_data);
    return local_data;
}

void opcode_0xC6() {  Double_WriteBack(Calculate_DEC(Fetch_ZeroPage()));     Begin_Fetch_Next_Opcode();  return;  }  // 0xC6 - DEC - ZeroPage
void opcode_0xD6() {  Double_WriteBack(Calculate_DEC(Fetch_ZeroPage_X()));   Begin_Fetch_Next_Opcode();  return;  }  // 0xD6 - DEC - ZeroPage , X
void opcode_0xCE() {  Double_WriteBack(Calculate_DEC(Fetch_Absolute()));     Begin_Fetch_Next_Opcode();  return;  }  // 0xCE - DEC - Absolute
void opcode_0xDE() {  Double_WriteBack(Calculate_DEC(Fetch_Absolute_X(0)));  Begin_Fetch_Next_Opcode();  return;  }  // 0xDE - DEC - Absolute , X


// -------------------------------------------------
// LSR - Memory
// -------------------------------------------------
uint8_t Calculate_LSR(uint8_t local_data) {

    if ((0x01&local_data)==0x01)   register_flags = register_flags | 0x01;              // Set the C flag
    else                           register_flags = register_flags & 0xFE;              // Clear the C flag
      
    local_data = (0x7F& (local_data >> 1));  


    Calc_Flags_NEGATIVE_ZERO(local_data);
    return local_data;
}
void opcode_0x46() {  Double_WriteBack(Calculate_LSR(Fetch_ZeroPage()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x46 - LSR - Logical Shift Right - ZeroPage
void opcode_0x56() {  Double_WriteBack(Calculate_LSR(Fetch_ZeroPage_X()));   Begin_Fetch_Next_Opcode();  return;  }  // 0x56 - LSR - Logical Shift Right - ZeroPage , X
void opcode_0x4E() {  Double_WriteBack(Calculate_LSR(Fetch_Absolute()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x4E - LSR - Logical Shift Right - Absolute
void opcode_0x5E() {  Double_WriteBack(Calculate_LSR(Fetch_Absolute_X(0)));  Begin_Fetch_Next_Opcode();  return;  }  // 0x5E - LSR - Logical Shift Right - Absolute , X


// -------------------------------------------------
// ROR - Memory
// -------------------------------------------------
uint8_t Calculate_ROR(uint8_t local_data) {

  uint8_t old_carry_flag=0;
  

    old_carry_flag = register_flags << 7;                                       // Shift the old carry flag to bit[8] to be rotated in
    
    if ((0x01&local_data)==0x01)   register_flags = register_flags | 0x01;              // Set the C flag
    else                           register_flags = register_flags & 0xFE;              // Clear the C flag
    
    local_data = ( old_carry_flag | (local_data>>1) );
    

    Calc_Flags_NEGATIVE_ZERO(local_data);
    return local_data;
}
void opcode_0x66() {  Double_WriteBack(Calculate_ROR(Fetch_ZeroPage()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x66 - ROR - Rotate Right - ZeroPage
void opcode_0x76() {  Double_WriteBack(Calculate_ROR(Fetch_ZeroPage_X()));   Begin_Fetch_Next_Opcode();  return;  }  // 0x76 - ROR - Rotate Right - ZeroPage , X
void opcode_0x6E() {  Double_WriteBack(Calculate_ROR(Fetch_Absolute()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x6E - ROR - Rotate Right - Absolute
void opcode_0x7E() {  Double_WriteBack(Calculate_ROR(Fetch_Absolute_X(0)));  Begin_Fetch_Next_Opcode();  return;  }  // 0x7E - ROR - Rotate Right - Absolute , X


// -------------------------------------------------
// ROL - Memory
// -------------------------------------------------
uint8_t Calculate_ROL(uint8_t local_data) {

  uint8_t old_carry_flag=0;
  
    old_carry_flag = 0x1 & register_flags;                                      // Store the old carry flag to be rotated in
    
    
    if (0x80&local_data)   register_flags = register_flags | 0x01;              // Set the C flag
    else                   register_flags = register_flags & 0xFE;              // Clear the C flag
    
    local_data = (local_data<<1) | old_carry_flag;

    Calc_Flags_NEGATIVE_ZERO(local_data);
    return local_data;
}
void opcode_0x26() {  Double_WriteBack(Calculate_ROL(Fetch_ZeroPage()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x26 - ROL - Rotate Left - ZeroPage
void opcode_0x36() {  Double_WriteBack(Calculate_ROL(Fetch_ZeroPage_X()));   Begin_Fetch_Next_Opcode();  return;  }  // 0x36 - ROL - Rotate Left - ZeroPage , X
void opcode_0x2E() {  Double_WriteBack(Calculate_ROL(Fetch_Absolute()));     Begin_Fetch_Next_Opcode();  return;  }  // 0x2E - ROL - Rotate Left - Absolute
void opcode_0x3E() {  Double_WriteBack(Calculate_ROL(Fetch_Absolute_X(0)));  Begin_Fetch_Next_Opcode();  return;  }  // 0x3E - ROL - Rotate Left - Absolute , X


// -------------------------------------------------
// Branches
// -------------------------------------------------
void Branch_Taken()  {
    
    effective_address = Sign_Extend16(Fetch_Immediate()); 
    effective_address = (register_pc+1) + effective_address;

    if (mode<2) {
    if ( (0xFF00&register_pc) == (0xFF00&effective_address) )  {  Fetch_Immediate();                     }  // Page boundary not crossed
    else                                                       {  Fetch_Immediate(); Fetch_Immediate();  }  // Page boundary crossed
    }
    register_pc = effective_address;
    start_read(register_pc);
    return;
}
void opcode_0xB0() {  if ((flag_c)==1) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0xB0 - BCS - Branch on Carry Set
void opcode_0x90() {  if ((flag_c)==0) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0x90 - BCC - Branch on Carry Clear
void opcode_0xF0() {  if ((flag_z)==1) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0xF0 - BEQ - Branch on Zero Set
void opcode_0xD0() {  if ((flag_z)==0) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0xD0 - BNE - Branch on Zero Clear
void opcode_0x70() {  if ((flag_v)==1) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0x70 - BVS - Branch on Overflow Set
void opcode_0x50() {  if ((flag_v)==0) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0x50 - BVC - Branch on Overflow Clear
void opcode_0x30() {  if ((flag_n)==1) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0x30 - BMI - Branch on Minus (N Flag Set)
void opcode_0x10() {  if ((flag_n)==0) Branch_Taken();  else { if (mode<2) { Fetch_Immediate(); } else { register_pc++; }; Begin_Fetch_Next_Opcode();}  return;  }  // 0x10 - BPL - Branch on Plus  (N Flag Clear)


// -------------------------------------------------
// Jumps and Returns
// -------------------------------------------------
void opcode_0x4C() { register_pc=Calculate_Absolute();  start_read(register_pc);  return;  }  // 0x4C - JMP - Jump Absolute


// -------------------------------------------------
// 0x6C - JMP - Jump Indirect
// -------------------------------------------------
void opcode_0x6C()  {
    uint16_t lal, lah;
    uint16_t adl, adh;
    
    lal = Fetch_Immediate();        
    lah = Fetch_Immediate()<<8;     
    adl = read_byte(lah + lal);     
    adh = read_byte(lah + lal + 1)<<8;
    effective_address = adh+adl;  
    register_pc = (0xFF00&adh) + (0x00FF&effective_address) ;  // 6502 page wrapping bug 
    start_read(register_pc);
    return ;
}

// -------------------------------------------------
// 0x20 - JSR - Jump to Subroutine
// -------------------------------------------------
void opcode_0x20()  {
    uint16_t adl, adh; 
    
    adl = Fetch_Immediate();
    adh = Fetch_Immediate()<<8;
    if (mode<2) read_byte(register_sp_fixed);
    push((0xFF00&register_pc)>>8);  

    push(0x00FF&register_pc);
    register_pc = adh+adl;  
    start_read(register_pc);
    return ;
}

// -------------------------------------------------
// 0x40 - RTI - Return from Interrupt
// -------------------------------------------------
void opcode_0x40()  {
    uint16_t pcl, pch; 
    
    Fetch_Immediate();
    if (mode<2) read_byte(register_sp_fixed);
    register_flags = pop();
    pcl = pop();
    pch = pop()<<8;
    register_pc = pch+pcl;  
    start_read(register_pc);
    return ;
}

// -------------------------------------------------
// 0x60 - RTS - Return from Subroutine
// -------------------------------------------------
void opcode_0x60()  {
    uint16_t pcl, pch; 
    
    Fetch_Immediate();
    if (mode<2) read_byte(register_sp_fixed);
    pcl = pop();
    pch = pop()<<8;
    register_pc = pch+pcl+1;  
    if (mode<2) read_byte(register_pc);
    start_read(register_pc);
    return ;
}


// -------------------------------------------------
//
// *** Undocumented 6502 Opcodes ***
//
// -------------------------------------------------

// --------------------------------------------------------------------------------------------------
// SLO - Shift left one bit in memory, then OR accumulator with memory.
// --------------------------------------------------------------------------------------------------
uint8_t Calculate_SLO(uint8_t local_data) {

    if ((0x80&local_data)==0x80)   register_flags = register_flags | 0x01;              // Set the C flag
    else                           register_flags = register_flags & 0xFE;              // Clear the C flag
    
    local_data = ((local_data << 1) & 0xFE);
    
    register_a = register_a | local_data;

    Calc_Flags_NEGATIVE_ZERO(register_a);
    return local_data;
}
void opcode_0x07() {  Double_WriteBack(Calculate_SLO(Fetch_ZeroPage()));              Begin_Fetch_Next_Opcode();  return;  }  // 0x07 - SLO - ZeroPage
void opcode_0x17() {  Double_WriteBack(Calculate_SLO(Fetch_ZeroPage_X()));            Begin_Fetch_Next_Opcode();  return;  }  // 0x17 - SLO - ZeroPage , X
void opcode_0x03() {  Double_WriteBack(Calculate_SLO(Fetch_Indexed_Indirect_X()));    Begin_Fetch_Next_Opcode();  return;  }  // 0x03 - SLO - Indexed Indirect X
void opcode_0x13() {  Double_WriteBack(Calculate_SLO(Fetch_Indexed_Indirect_Y(1)));   Begin_Fetch_Next_Opcode();  return;  }  // 0x13 - SLO - Indirect Indexed  Y
void opcode_0x0F() {  Double_WriteBack(Calculate_SLO(Fetch_Absolute()));              Begin_Fetch_Next_Opcode();  return;  }  // 0x0F - SLO - Absolute
void opcode_0x1F() {  Double_WriteBack(Calculate_SLO(Fetch_Absolute_X(1)));           Begin_Fetch_Next_Opcode();  return;  }  // 0x1F - SLO - Absolute , X
void opcode_0x1B() {  Double_WriteBack(Calculate_SLO(Fetch_Absolute_Y(1)));           Begin_Fetch_Next_Opcode();  return;  }  // 0x1B - SLO - Absolute , Y


// --------------------------------------------------------------------------------------------------
// RLA - Rotate one bit left in memory, then AND accumulator with memory.
// --------------------------------------------------------------------------------------------------
uint8_t Calculate_RLA(uint8_t local_data) {
    uint8_t old_carry_flag=0;
  
    old_carry_flag = 0x1 & register_flags;                                      // Store the old carry flag to be rotated in
    
    
    if (0x80&local_data)   register_flags = register_flags | 0x01;              // Set the C flag
    else                   register_flags = register_flags & 0xFE;              // Clear the C flag
    
    local_data = (local_data<<1) | old_carry_flag;
    
    register_a = register_a & local_data;

    Calc_Flags_NEGATIVE_ZERO(register_a);
    return local_data;
}
void opcode_0x27() {  Double_WriteBack(Calculate_RLA(Fetch_ZeroPage()));              Begin_Fetch_Next_Opcode();  return;  }  // 0x27 - RLA - ZeroPage
void opcode_0x37() {  Double_WriteBack(Calculate_RLA(Fetch_ZeroPage_X()));            Begin_Fetch_Next_Opcode();  return;  }  // 0x37 - RLA - ZeroPage , X
void opcode_0x23() {  Double_WriteBack(Calculate_RLA(Fetch_Indexed_Indirect_X()));    Begin_Fetch_Next_Opcode();  return;  }  // 0x23 - RLA - Indexed Indirect X
void opcode_0x33() {  Double_WriteBack(Calculate_RLA(Fetch_Indexed_Indirect_Y(1)));   Begin_Fetch_Next_Opcode();  return;  }  // 0x33 - RLA - Indirect Indexed  Y
void opcode_0x2F() {  Double_WriteBack(Calculate_RLA(Fetch_Absolute()));              Begin_Fetch_Next_Opcode();  return;  }  // 0x2F - RLA - Absolute
void opcode_0x3F() {  Double_WriteBack(Calculate_RLA(Fetch_Absolute_X(1)));           Begin_Fetch_Next_Opcode();  return;  }  // 0x3F - RLA - Absolute , X
void opcode_0x3B() {  Double_WriteBack(Calculate_RLA(Fetch_Absolute_Y(1)));           Begin_Fetch_Next_Opcode();  return;  }  // 0x3B - RLA - Absolute , Y


// --------------------------------------------------------------------------------------------------
// SRE - Shift right one bit in memory, then EOR accumulator with memory.
// --------------------------------------------------------------------------------------------------
uint8_t Calculate_SRE(uint8_t local_data) {
    
    if ((0x01&local_data)==0x01)   register_flags = register_flags | 0x01;      // Set the C flag
    else                           register_flags = register_flags & 0xFE;      // Clear the C flag
      
    local_data = (0x7F& (local_data >> 1));  
    
    register_a = register_a ^ local_data;

    Calc_Flags_NEGATIVE_ZERO(register_a);
    return local_data;
}
void opcode_0x47() {  Double_WriteBack(Calculate_SRE(Fetch_ZeroPage()));              Begin_Fetch_Next_Opcode();  return;  }  // 0x47 - SRE - ZeroPage
void opcode_0x57() {  Double_WriteBack(Calculate_SRE(Fetch_ZeroPage_X()));            Begin_Fetch_Next_Opcode();  return;  }  // 0x57 - SRE - ZeroPage , X
void opcode_0x43() {  Double_WriteBack(Calculate_SRE(Fetch_Indexed_Indirect_X()));    Begin_Fetch_Next_Opcode();  return;  }  // 0x43 - SRE - Indexed Indirect X
void opcode_0x53() {  Double_WriteBack(Calculate_SRE(Fetch_Indexed_Indirect_Y(1)));   Begin_Fetch_Next_Opcode();  return;  }  // 0x53 - SRE - Indirect Indexed  Y
void opcode_0x4F() {  Double_WriteBack(Calculate_SRE(Fetch_Absolute()));              Begin_Fetch_Next_Opcode();  return;  }  // 0x4F - SRE - Absolute
void opcode_0x5F() {  Double_WriteBack(Calculate_SRE(Fetch_Absolute_X(1)));           Begin_Fetch_Next_Opcode();  return;  }  // 0x5F - SRE - Absolute , X
void opcode_0x5B() {  Double_WriteBack(Calculate_SRE(Fetch_Absolute_Y(1)));           Begin_Fetch_Next_Opcode();  return;  }  // 0x5B - SRE - Absolute , Y


// --------------------------------------------------------------------------------------------------
// RRA - Rotate one bit right in memory, then add memory to accumulator (with carry).
// --------------------------------------------------------------------------------------------------
uint8_t Calculate_RRA(uint8_t local_data) {
  uint8_t local_old_C;

    local_old_C = (0x1&register_flags) << 7;
    
    if ((0x01&local_data)==0x01)   register_flags = register_flags | 0x01;      // Set the C flag
    else                           register_flags = register_flags & 0xFE;      // Clear the C flag
      
    local_data = local_old_C | (0x7F& (local_data >> 1));  
    
    global_temp = local_data;
    
    return local_data;
}

void opcode_0x67() {  Double_WriteBack(Calculate_RRA(Fetch_ZeroPage()));              Calculate_ADC(global_temp);  return;  }  // 0x67 - RRA - ZeroPage
void opcode_0x77() {  Double_WriteBack(Calculate_RRA(Fetch_ZeroPage_X()));            Calculate_ADC(global_temp);  return;  }  // 0x77 - RRA - ZeroPage , X
void opcode_0x63() {  Double_WriteBack(Calculate_RRA(Fetch_Indexed_Indirect_X()));    Calculate_ADC(global_temp);  return;  }  // 0x63 - RRA - Indexed Indirect X
void opcode_0x73() {  Double_WriteBack(Calculate_RRA(Fetch_Indexed_Indirect_Y(1)));   Calculate_ADC(global_temp);  return;  }  // 0x73 - RRA - Indirect Indexed  Y
void opcode_0x6F() {  Double_WriteBack(Calculate_RRA(Fetch_Absolute()));              Calculate_ADC(global_temp);  return;  }  // 0x6F - RRA - Absolute
void opcode_0x7F() {  Double_WriteBack(Calculate_RRA(Fetch_Absolute_X(1)));           Calculate_ADC(global_temp);  return;  }  // 0x7F - RRA - Absolute , X
void opcode_0x7B() {  Double_WriteBack(Calculate_RRA(Fetch_Absolute_Y(1)));           Calculate_ADC(global_temp);  return;  }  // 0x7B - RRA - Absolute , Y


// --------------------------------------------------------------------------------------------------
// AND the contents of the A and X registers (without changing the contents of either register) and 
// stores the result in memory.
// --------------------------------------------------------------------------------------------------
void opcode_0x87() {  Write_ZeroPage(register_a&register_x);            Begin_Fetch_Next_Opcode();  return;  }  // 0x87 - SAX - ZeroPage
void opcode_0x97() {  Write_ZeroPage_Y(register_a&register_x);          Begin_Fetch_Next_Opcode();  return;  }  // 0x97 - SAX - ZeroPage , Y
void opcode_0x83() {  Write_Indexed_Indirect_X(register_a&register_x);  Begin_Fetch_Next_Opcode();  return;  }  // 0x83 - SAX - Indexed Indirect X
void opcode_0x8F() {  Write_Absolute(register_a&register_x);            Begin_Fetch_Next_Opcode();  return;  }  // 0x8F - SAX - Absolute


// --------------------------------------------------------------------------------------------------
// Load both the accumulator and the X register with the contents of a memory location.
// --------------------------------------------------------------------------------------------------
void opcode_0xA7() { register_a=Fetch_ZeroPage();             register_x=register_a;   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xA7 - LAX - ZeroPage
void opcode_0xB7() { register_a=Fetch_ZeroPage_Y();           register_x=register_a;   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xB7 - LAX - ZeroPage , Y
void opcode_0xA3() { register_a=Fetch_Indexed_Indirect_X();   register_x=register_a;   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xA3 - LAX - Indexed Indirect X
void opcode_0xB3() { register_a=Fetch_Indexed_Indirect_Y(1);  register_x=register_a;   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xB3 - LAX - Indirect Indexed  Y
void opcode_0xAF() { register_a=Fetch_Absolute();             register_x=register_a;   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xAF - LAX - Absolute
void opcode_0xBF() { register_a=Fetch_Absolute_Y(1);          register_x=register_a;   Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xBF - LAX - Absolute , Y


// --------------------------------------------------------------------------------------------------
// Decrement the contents of a memory location and then compare the result with the A register.
// --------------------------------------------------------------------------------------------------
void opcode_0xC7() {  Double_WriteBack(Calculate_DEC(Fetch_ZeroPage()));             Calculate_CMP(global_temp);  return;  }  // 0xC7 - DCP - ZeroPage
void opcode_0xD7() {  Double_WriteBack(Calculate_DEC(Fetch_ZeroPage_X()));           Calculate_CMP(global_temp);  return;  }  // 0xD7 - DCP - ZeroPage , X
void opcode_0xC3() {  Double_WriteBack(Calculate_DEC(Fetch_Indexed_Indirect_X()));   Calculate_CMP(global_temp);  return;  }  // 0xC3 - DCP - Indexed Indirect X
void opcode_0xD3() {  Double_WriteBack(Calculate_DEC(Fetch_Indexed_Indirect_Y(0)));  Calculate_CMP(global_temp);  return;  }  // 0xD3 - DCP - Indirect Indexed  Y
void opcode_0xCF() {  Double_WriteBack(Calculate_DEC(Fetch_Absolute()));             Calculate_CMP(global_temp);  return;  }  // 0xCF - DCP - Absolute
void opcode_0xDF() {  Double_WriteBack(Calculate_DEC(Fetch_Absolute_X(0)));          Calculate_CMP(global_temp);  return;  }  // 0xDF - DCP - Absolute , X
void opcode_0xDB() {  Double_WriteBack(Calculate_DEC(Fetch_Absolute_Y(0)));          Calculate_CMP(global_temp);  return;  }  // 0xDB - DCP - Absolute , Y



// --------------------------------------------------------------------------------------------------
// ISC - Increase memory by one, then subtract memory from accumulator (with borrow).
// --------------------------------------------------------------------------------------------------
void opcode_0xE7() {  Double_WriteBack(Calculate_INC(Fetch_ZeroPage()));             Calculate_SBC(global_temp);  return;  }  // 0xE7 - ISC - ZeroPage
void opcode_0xF7() {  Double_WriteBack(Calculate_INC(Fetch_ZeroPage_X()));           Calculate_SBC(global_temp);  return;  }  // 0xF7 - ISC - ZeroPage , X
void opcode_0xE3() {  Double_WriteBack(Calculate_INC(Fetch_Indexed_Indirect_X()));   Calculate_SBC(global_temp);  return;  }  // 0xE3 - ISC - Indexed Indirect X
void opcode_0xF3() {  Double_WriteBack(Calculate_INC(Fetch_Indexed_Indirect_Y(0)));  Calculate_SBC(global_temp);  return;  }  // 0xF3 - ISC - Indirect Indexed  Y
void opcode_0xEF() {  Double_WriteBack(Calculate_INC(Fetch_Absolute()));             Calculate_SBC(global_temp);  return;  }  // 0xEF - ISC - Absolute
void opcode_0xFF() {  Double_WriteBack(Calculate_INC(Fetch_Absolute_X(0)));          Calculate_SBC(global_temp);  return;  }  // 0xFF - ISC - Absolute , X
void opcode_0xFB() {  Double_WriteBack(Calculate_INC(Fetch_Absolute_Y(0)));          Calculate_SBC(global_temp);  return;  }  // 0xFB - ISC - Absolute , Y


// --------------------------------------------------------------------------------------------------
// ANC - ANDs the contents of the A register with an immediate value and then moves bit 7 of A
// into the Carry flag.
// --------------------------------------------------------------------------------------------------
void Calculate_ANC(uint8_t local_data) {
    
    Begin_Fetch_Next_Opcode();

    register_a = register_a & local_data;

    if ((0x80&register_a)==0x80)   register_flags = register_flags | 0x01;              // Set the C flag
    else                           register_flags = register_flags & 0xFE;              // Clear the C flag
        
    Calc_Flags_NEGATIVE_ZERO(register_a);
    return;
}
void opcode_0x0B() {  Calculate_ANC(Fetch_Immediate());  return;  }  // 0x0B - ANC - Immediate
void opcode_0x2B() {  Calculate_ANC(Fetch_Immediate());  return;  }  // 0x2B - ANC - Immediate


// --------------------------------------------------------------------------------------------------
// ALR - AND the contents of the A register with an immediate value and then LSRs the result.
// --------------------------------------------------------------------------------------------------
void Calculate_ALR(uint8_t local_data) {
    
    Begin_Fetch_Next_Opcode();

    register_a = register_a & local_data;
    
    if ((0x01&register_a)==0x01)   register_flags = register_flags | 0x01;              // Set the C flag
    else                           register_flags = register_flags & 0xFE;              // Clear the C flag
      
    register_a = (0x7F& (register_a >> 1));  
    
    Calc_Flags_NEGATIVE_ZERO(register_a);
    return;
}
void opcode_0x4B() {  Calculate_ALR(Fetch_Immediate());  return;  }  // 0x4B - ALR - Immediate


// --------------------------------------------------------------------------------------------------
// ARR - ANDs the accumulator with an immediate value and then rotates the content right.
// --------------------------------------------------------------------------------------------------
void Calculate_ARR(uint8_t local_data) {
  uint8_t local_old_C;

    local_old_C = (0x1&register_flags) << 7;
    
    Begin_Fetch_Next_Opcode();

    register_a = register_a & local_data;
    
    register_a = local_old_C | (0x7F& (register_a >> 1));  

    register_flags = register_flags & 0xBE;                                            // Pre-clear the C and V flags   
    if ( (0xC0 & register_a) == 0x40) {  register_flags = register_flags | 0x40;  }    // Set the V flag 
    if ( (0xC0 & register_a) == 0x80) {  register_flags = register_flags | 0x41;  }    // Set the C and V flags 
    if ( (0xC0 & register_a) == 0xC0) {  register_flags = register_flags | 0x01;  }    // Set the C flag 
    
    Calc_Flags_NEGATIVE_ZERO(register_a);
    return;
}
void opcode_0x6B() {  Calculate_ARR(Fetch_Immediate());  return;  }  // 0x6B - ARR - Immediate


// --------------------------------------------------------------------------------------------------
// SBX - ANDs the contents of the A and X registers (leaving the contents of A intact),
// subtracts an immediate value, and then stores the result in X.
// --------------------------------------------------------------------------------------------------
void Calculate_SBX(uint16_t local_data)  { 
    int16_t signed_total=0;

 
    Begin_Fetch_Next_Opcode();
    
    register_x = register_a & register_x;


    register_x =  register_x - local_data;
    signed_total = (int16_t)register_x - (int16_t)(local_data );

    
    if (signed_total>=0)    register_flags = register_flags | 0x01;              // Set the C flag
    else                    register_flags = register_flags & 0xFE;              // Clear the C flag  

    register_x = (0xFF & register_x);
    Calc_Flags_NEGATIVE_ZERO(register_x);
    
    return;
    }
void opcode_0xCB() {  Calculate_SBX(Fetch_Immediate());  return;  }  // 0xCB - SBX - Immediate
    
    
// --------------------------------------------------------------------------------------------------
// LAS - AND memory with stack pointer, transfer result to accumulator, X register and stack pointer.
// --------------------------------------------------------------------------------------------------
void opcode_0xBB() { register_sp=(register_sp&Fetch_Absolute_Y(1));  register_a=register_sp;  register_x=register_sp;  Begin_Fetch_Next_Opcode();  Calc_Flags_NEGATIVE_ZERO(register_a); return;  }  // 0xBB - LAS - Absolute , Y
    

// --------------------------------------------------------------------------------------------------
// NOP - Fetch Immediate
// --------------------------------------------------------------------------------------------------
void opcode_0x80() {  Fetch_Immediate();  Begin_Fetch_Next_Opcode(); return;  }  // 0x80 - NOP - Immediate
void opcode_0x82() {  Fetch_Immediate();  Begin_Fetch_Next_Opcode(); return;  }  // 0x82 - NOP - Immediate
void opcode_0xC2() {  Fetch_Immediate();  Begin_Fetch_Next_Opcode(); return;  }  // 0xC2 - NOP - Immediate
void opcode_0xE2() {  Fetch_Immediate();  Begin_Fetch_Next_Opcode(); return;  }  // 0xE2 - NOP - Immediate
void opcode_0x89() {  Fetch_Immediate();  Begin_Fetch_Next_Opcode(); return;  }  // 0x89 - NOP - Immediate


// --------------------------------------------------------------------------------------------------
// NOP - Fetch ZeroPage
// --------------------------------------------------------------------------------------------------
void opcode_0x04() {  Fetch_ZeroPage();  Begin_Fetch_Next_Opcode(); return;  }  // 0x04 - NOP - ZeroPage
void opcode_0x44() {  Fetch_ZeroPage();  Begin_Fetch_Next_Opcode(); return;  }  // 0x44 - NOP - ZeroPage
void opcode_0x64() {  Fetch_ZeroPage();  Begin_Fetch_Next_Opcode(); return;  }  // 0x64 - NOP - ZeroPage


// --------------------------------------------------------------------------------------------------
// NOP - Fetch ZeroPage , X
// --------------------------------------------------------------------------------------------------
void opcode_0x14() {  Fetch_ZeroPage_X();  Begin_Fetch_Next_Opcode(); return;  }  // 0x14 - NOP - ZeroPage , X
void opcode_0x34() {  Fetch_ZeroPage_X();  Begin_Fetch_Next_Opcode(); return;  }  // 0x34 - NOP - ZeroPage , X
void opcode_0x54() {  Fetch_ZeroPage_X();  Begin_Fetch_Next_Opcode(); return;  }  // 0x54 - NOP - ZeroPage , X
void opcode_0x74() {  Fetch_ZeroPage_X();  Begin_Fetch_Next_Opcode(); return;  }  // 0x74 - NOP - ZeroPage , X
void opcode_0xD4() {  Fetch_ZeroPage_X();  Begin_Fetch_Next_Opcode(); return;  }  // 0xD4 - NOP - ZeroPage , X
void opcode_0xF4() {  Fetch_ZeroPage_X();  Begin_Fetch_Next_Opcode(); return;  }  // 0xF4 - NOP - ZeroPage , X


// --------------------------------------------------------------------------------------------------
// NOP - Fetch Absolute
// --------------------------------------------------------------------------------------------------
void opcode_0x0C() {  Fetch_Absolute();  Begin_Fetch_Next_Opcode(); return;  }  // 0x0C - NOP - Absolute


// --------------------------------------------------------------------------------------------------
// NOP - Fetch Absolute , X
// --------------------------------------------------------------------------------------------------
void opcode_0x1C() {  Fetch_Absolute_X(1);  Begin_Fetch_Next_Opcode(); return;  }  // 0x1C - NOP - Absolute , X
void opcode_0x3C() {  Fetch_Absolute_X(1);  Begin_Fetch_Next_Opcode(); return;  }  // 0x3C - NOP - Absolute , X
void opcode_0x5C() {  Fetch_Absolute_X(1);  Begin_Fetch_Next_Opcode(); return;  }  // 0x5C - NOP - Absolute , X
void opcode_0x7C() {  Fetch_Absolute_X(1);  Begin_Fetch_Next_Opcode(); return;  }  // 0x7C - NOP - Absolute , X
void opcode_0xDC() {  Fetch_Absolute_X(1);  Begin_Fetch_Next_Opcode(); return;  }  // 0xDC - NOP - Absolute , X
void opcode_0xFC() {  Fetch_Absolute_X(1);  Begin_Fetch_Next_Opcode(); return;  }  // 0xFC - NOP - Absolute , X


// --------------------------------------------------------------------------------------------------
// JAM - Lock up the processor
// --------------------------------------------------------------------------------------------------
void opcode_0x02() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x02 - JAM
void opcode_0x12() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x12 - JAM
void opcode_0x22() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x22 - JAM
void opcode_0x32() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x32 - JAM
void opcode_0x42() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x42 - JAM
void opcode_0x52() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x52 - JAM
void opcode_0x62() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x62 - JAM
void opcode_0x72() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x72 - JAM
void opcode_0x92() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0x92 - JAM
void opcode_0xB2() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0xB2 - JAM
void opcode_0xD2() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0xD2 - JAM
void opcode_0xF2() {  Fetch_Immediate();   handle_JAM();    return;  }  // 0xF2 - JAM


// --------------------------------------------------------------------------------------------------
// Unstable 6502 opcodes
// --------------------------------------------------------------------------------------------------

// 0x93 - SHA - ZeroPage , Y 
void opcode_0x93() {
    uint16_t initial_ea;
    
    initial_ea = Fetch_Immediate(); 
    effective_address = (0x00FF&(initial_ea + register_y)); 
    if ( (0xFF00&initial_ea) != (0xFF00&effective_address) ) effective_address = effective_address & (0x00FF | ((register_a & register_x)<<8));
    //read_byte(effective_address); 
    write_byte( effective_address , (register_a & register_x & ((effective_address>>8)+1)) );   
    Begin_Fetch_Next_Opcode(); 
    return;  
    }   


// 0x9F - SHA - Absolute , Y
void opcode_0x9F() {
    uint16_t xbal,xbah;
    uint16_t initial_ea;
    
    xbal = Fetch_Immediate(); 
    xbah = Fetch_Immediate()<<8; 
    initial_ea = xbal + xbah;     
    effective_address = initial_ea + register_y;     
    if ( (0xFF00&initial_ea) != (0xFF00&effective_address) ) effective_address = effective_address & (0x00FF | ((register_a & register_x)<<8));
    //read_byte(effective_address); 
    write_byte( effective_address , (register_a & register_x & ((effective_address>>8)+1)) );   
    Begin_Fetch_Next_Opcode(); 
    return;  
    }   
    
    

// 0x9E - SHX - Absolute , Y
void opcode_0x9E() {
    uint16_t xbal,xbah;
    uint16_t initial_ea;
    
    xbal = Fetch_Immediate(); 
    xbah = Fetch_Immediate()<<8; 
    initial_ea = xbal + xbah;     
    effective_address = initial_ea + register_y;     
    if ( (0xFF00&initial_ea) != (0xFF00&effective_address) ) effective_address = effective_address & (0x00FF | (register_x<<8));
    //read_byte(effective_address); 
    write_byte( effective_address , (register_x & ((effective_address>>8)+1)) );   
    Begin_Fetch_Next_Opcode(); 
    return;  
    }   


// 0x9C - SHY - Absolute , X
void opcode_0x9C() {
    uint16_t xbal,xbah;
    uint16_t initial_ea;
    
    xbal = Fetch_Immediate(); 
    xbah = Fetch_Immediate()<<8; 
    initial_ea = xbal + xbah;     
    effective_address = initial_ea + register_x;     
    if ( (0xFF00&initial_ea) != (0xFF00&effective_address) ) effective_address = effective_address & (0x00FF | (register_y<<8));
    //read_byte(effective_address); 
    write_byte( effective_address , (register_y & ((effective_address>>8)+1)) );   
    Begin_Fetch_Next_Opcode(); 
    return;  
    }   
    


// 0x9B - TAS - Absolute , Y
void opcode_0x9B() {
    uint16_t xbal,xbah;
    uint16_t initial_ea;
    
    register_sp = register_a & register_x;
    
    xbal = Fetch_Immediate(); 
    xbah = Fetch_Immediate()<<8; 
    initial_ea = xbal + xbah;     
    effective_address = initial_ea + register_y;     
    if ( (0xFF00&initial_ea) != (0xFF00&effective_address) ) effective_address = effective_address & (0x00FF | ((register_a & register_x)<<8));
    //read_byte(effective_address); 
    write_byte( effective_address , (register_a & register_x & ((effective_address>>8)+1)) );   
    Begin_Fetch_Next_Opcode(); 
    return;  
    }   
    
    
// 0x8B - ANE - Immediate
void opcode_0x8B() {
    
    Calc_Flags_NEGATIVE_ZERO(register_a);
    
    register_a = (register_a | 0xEE) & register_x & Fetch_Immediate();
    
    Begin_Fetch_Next_Opcode(); 
    return;  
    }   
    
    
// 0xAB - LAX - Immediate 
void opcode_0xAB() {
    
    Calc_Flags_NEGATIVE_ZERO(register_a);
    
    register_a = (register_a | 0xEE) & Fetch_Immediate();
    register_x = register_a;
    
    Begin_Fetch_Next_Opcode(); 
    return;  
    }   

// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------

void handle_JAM() {
  Serial.print("JAM! at $"); Serial.println(register_pc, HEX);

  // wait for RESET to assert and return control to the loop
  while (digitalReadFast(PIN_RESET)==0) {}                        // Stay here until RESET asserts
}

// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------

void test_sequence() {
  uint8_t tmp_mode;
  tmp_mode = mode;
  mode = 1;
  for (uint8_t i=0;i<255;i++) {
    write_byte(0x400+i,i);
  }
  write_byte(0xc000, 0x78);
  write_byte(0xc001, 0xee);
  write_byte(0xc002, 0x20);
  write_byte(0xc003, 0xd0);
  write_byte(0xc004, 0x4c);
  write_byte(0xc005, 0x01);
  write_byte(0xc006, 0xc0);
  mode = tmp_mode;
  start_read(register_pc);
}

// --------------------------------------------------------------------------------------------------
// --------------------------------------------------------------------------------------------------

void monitor_reg() {
  char buf[35];
  Serial.println("  ADDR A  X  Y  SP 00 01 NV-BDIZC");
  sprintf(buf,";%04X %02X %02X %02X %02X %02X %02X ",register_pc,register_a,register_x,register_y,register_sp,internal_RAM[0],internal_RAM[1]);
  Serial.print(buf);
  uint8_t f = register_flags;
  for (uint8_t i=0;i<7;i++) {
    if (f & 0x80) { Serial.print('1'); } else { Serial.print('0'); };
    f = f << 1;
  }
  Serial.println();
}

void monitor_mem() {
  char buf[255];
  uint8_t i=0;
  char b='\0';
  uint16_t addr;
  static uint16_t lastaddr = 0;
  while (i<sizeof(buf) && b!='\n') {
    while(!Serial.available());
    b = Serial.read();
    buf[i++] = b;
  }
  buf[i] = 0;
  Serial.print("["); Serial.print(buf); Serial.println("]");
  if (strlen(buf)==0) {
    addr = lastaddr;
  } else {
    addr = strtol(buf,0,16);
  }
  Serial.print("addr=");Serial.println(addr,HEX);
  for (uint8_t i=0;i<5;i++) {
    sprintf(buf, "%04X", addr);
    Serial.print(">"); Serial.print(buf);
    for (uint8_t j=0;j<16;i++) {
      sprintf(buf, " %02X", internal_RAM[addr++]);
      Serial.print(buf);
    }
    Serial.println();
  }
  lastaddr = addr;
}

// -------------------------------------------------
//
// Main loop
//
// -------------------------------------------------
 void loop() {

  uint16_t local_counter=0;
  uint8_t  nmi_n_old=1;
  uint8_t  next_instruction;
  uint32_t restore_taptime=0;
  const uint32_t restore_tapthreshold_max = 250;  // tap within 1/4s
  const uint32_t restore_tapthreshold_min = 50;   // but no faster than 50Hz (will that interfere with User Port serial I/O?
  uint8_t  restore_state=0;
  int incomingByte;

  // Give Teensy 4.1 a moment
  delay (50);
  wait_for_CLK_rising_edge();
  wait_for_CLK_rising_edge();
  wait_for_CLK_rising_edge();

  reset_sequence();


  while (1) {
      
      if (direct_reset==1) reset_sequence();
      
      
      // Set Acceleration using UART receive characters
      // Send the numbers 0,1,2,3 from the host through a serial terminal to the MCL65+
      // for acceleration modes 0,1,2,3
      //
      local_counter++;
      if (local_counter==8000) {
         if (Serial.available() ) {
          incomingByte = Serial.read();   
          switch (incomingByte){
            case '0': mode=0;  Serial.println("M0"); break;
            case '1': mode=1;  Serial.println("M1"); break;
            case '2': mode=2;  Serial.println("M2"); break;
            case '3': mode=3;  Serial.println("M3"); break;
            case 'R': reset_sequence(); Serial.println("RESET"); break;
            case 'e': EXROM=0; reset_sequence(); Serial.println("EXROM=0"); break;
            case 'E': EXROM=1; reset_sequence(); Serial.println("EXROM=1"); break;
            case 'g': GAME=0; reset_sequence(); Serial.println("GAME=0"); break;
            case 'G': GAME=1; reset_sequence(); Serial.println("GAME=1"); break;
            case 't': Serial.println("TEST"); test_sequence(); break;
            case '?': Serial.print("M"); Serial.print(mode); Serial.print(" EXROM"); Serial.print(EXROM); Serial.print(" GAME"); Serial.println(GAME); 
                Serial.print("Setup: mode="); Serial.print(mode); Serial.print(" LOAD:"); Serial.print(load_trap_enabled); Serial.print(" REU:"); Serial.println(reu_emulation_enabled);
                break;
            case 'm': monitor_mem();
            case 'r': monitor_reg();
            default: break;
          }
        }
      }
    
      // Poll for NMI and IRQ
      //
      if (nmi_n_old==0 && direct_nmi==1) {
        // check if we should intercept RESTORE taps
        uint32_t taptime = millis();
        if (((taptime-restore_taptime) > restore_tapthreshold_min) && ((taptime-restore_taptime) < restore_tapthreshold_max)) {
          restore_state++;
          if (restore_state==3) {
            restore_state = 0;
            // yes - do something like a freezer cartridge menu
            test_sequence();
          }
        } else {
          restore_state = 0;
          nmi_handler();
        }
        restore_taptime = taptime;
      }
      if (direct_irq==0x1  && (flag_i)==0x0)    irq_handler(0x0);   
      nmi_n_old = direct_nmi;                                        

      next_instruction = finish_read_byte();  

      // trap I/O
      if (load_trap_enabled && register_pc==0xffd5) {
        // check if Kernal is enabled (skip cart_high_rom check?)
        if ((Page_224_255) && (bank_mode & HIRAM_BIT)) {
           // is it tape?
           if (read_byte(0xba)==1) {
             String filename;
             uint8_t fnamelen = read_byte(0xb7);
             uint16_t fname = (read_byte(0xbc) << 8) + read_byte(0xbb);
             uint8_t lfn = read_byte(0xb8);
             uint8_t sa  = read_byte(0xb9);
             bool loadmode = (register_a==0);
             uint16_t loadaddr = (register_y << 8) + register_x;
             uint16_t basicaddr = (read_byte(0x2c) << 8) + read_byte(0x2b);
             size_t bytes = 0;
             Serial.print("reading filename from "); Serial.print(fname, HEX); Serial.print(" - "); Serial.print(fnamelen); Serial.print(" chars");
             for (uint8_t i=0; i<fnamelen; i++, fname++) {
               filename += String((char)read_byte(fname));
             }
             Serial.print("["); Serial.print(filename); Serial.println("]");
             Serial.print("load trap with filename=["); Serial.print(filename); Serial.print("], lfn="); Serial.print(lfn); Serial.print(" sa="); Serial.print(sa); Serial.print(" loadmode="); Serial.print(loadmode); Serial.print(" loadaddr="); Serial.println(loadaddr, HEX);
             if (filename.length()==0) {
               Serial.println("empty fname - load internal browser");
               // SHIFT+RUN/STOP - load the browser
               loadaddr = basicaddr;
               memcpy(&internal_RAM[basicaddr], &sdbrowser_prg[2], sdbrowser_prg_len-2);
               bytes = sdbrowser_prg_len-2;
               register_flags=register_flags&0xFE; // CLC
             } else {
               // if name was set load the specified file
               // pass all parameters: A (load/verify), X/Y (loadaddr); setlfs, setnam, whole internalRAM; loadaddr will be updated according to file or unchanged
               bytes = sd_load(filename, internal_RAM, lfn, sa, loadmode, &loadaddr);
               register_flags=register_flags&0xFE; // CLC
             }
             // update KERNAL pointers
             write_byte(0xac, loadaddr & 0xff);
             write_byte(0xad, loadaddr << 8);
             if (loadaddr!=basicaddr) {
              write_byte(0xaa, loadaddr & 0xff);  // update calladdr for sd_browser
              write_byte(0xab, loadaddr << 8);
             }
             // data is only in internal_RAM, writeback to host RAM if needed
             if (mode<3) {
              uint8_t tmp = read_cpu_port();
              write_cpu_port(0x00); // 64K RAM
              for (size_t i=0; i<bytes; i++, loadaddr++) {
                write_byte(loadaddr, internal_RAM[loadaddr]);
              }
              write_cpu_port(tmp); // restore config
             } else {
              loadaddr += bytes;
             }
             // set x/y to end address
             register_x = loadaddr & 0xff;
             register_y = loadaddr >> 8;
             next_instruction = 0x60; // RTS
           }
        }
      }

      switch (next_instruction){
          
        case 0x00:   irq_handler(0x1); break;  // BRK - Break
        case 0x01:   opcode_0x01();    break;  // OR - Indexed Indirect X
        case 0x02:   opcode_0x02();    break;  // JAM
        case 0x03:   opcode_0x03();    break;  // SLO - Indexed Indirect X
        case 0x04:   opcode_0x04();    break;  // NOP - ZeroPage
        case 0x05:   opcode_0x05();    break;  // OR ZeroPage
        case 0x06:   opcode_0x06();    break;  // ASL A - Arithmetic Shift Left - ZeroPage
        case 0x07:   opcode_0x07();    break;  // SLO - ZeroPage
        case 0x08:   opcode_0x08();    break;  // PHP - Push processor status to the stack
        case 0x09:   opcode_0x09();    break;  // OR - Immediate
        case 0x0A:   opcode_0x0A();    break;  // ASL A
        case 0x0B:   opcode_0x0B();    break;  // ANC - Immediate
        case 0x0C:   opcode_0x0C();    break;  // NOP - Absolute
        case 0x0D:   opcode_0x0D();    break;  // OR - Absolute
        case 0x0E:   opcode_0x0E();    break;  // ASL A - Arithmetic Shift Left - Absolute
        case 0x0F:   opcode_0x0F();    break;  // SLO - Absolute
        case 0x10:   opcode_0x10();    break;  // BNE - Branch on Zero Clear
        case 0x11:   opcode_0x11();    break;  // OR Indirect Indexed  Y
        case 0x12:   opcode_0x12();    break;  // JAM
        case 0x13:   opcode_0x13();    break;  // Indirect Indexed  Y
        case 0x14:   opcode_0x14();    break;  // NOP - ZeroPage , X
        case 0x15:   opcode_0x15();    break;  // OR - ZeroPage,X
        case 0x16:   opcode_0x16();    break;  // ASL A - Arithmetic Shift Left - ZeroPage , X
        case 0x17:   opcode_0x17();    break;  // SLO - ZeroPage , X
        case 0x18:   opcode_0x18();    break;  // CLC
        case 0x19:   opcode_0x19();    break;  // OR - Absolute,Y
        case 0x1A:   opcode_0xEA();    break;  // NOP
        case 0x1B:   opcode_0x1B();    break;  // SLO - Absolute , Y
        case 0x1C:   opcode_0x1C();    break;  // NOP - Absolute , X
        case 0x1D:   opcode_0x1D();    break;  // OR - Absolute,X
        case 0x1E:   opcode_0x1E();    break;  // ASL A - Arithmetic Shift Left - Absolute , X
        case 0x1F:   opcode_0x1F();    break;  // SLO - Absolute , X
        case 0x20:   opcode_0x20();    break;  // JSR - Jump to Subroutine
        case 0x21:   opcode_0x21();    break;  // AND - Indexed Indirect
        case 0x22:   opcode_0x22();    break;  // JAM
        case 0x23:   opcode_0x23();    break;  // RLA - Indexed Indirect X
        case 0x24:   opcode_0x24();    break;  // BIT - ZeroPage
        case 0x25:   opcode_0x25();    break;  // AND - ZeroPage
        case 0x26:   opcode_0x26();    break;  // ROL - Rotate Left - ZeroPage
        case 0x27:   opcode_0x27();    break;  // RLA - ZeroPage
        case 0x28:   opcode_0x28();    break;  // PLP - Pop processor status from the stack
        case 0x29:   opcode_0x29();    break;  // AND - Immediate
        case 0x2A:   opcode_0x2A();    break;  // ROL A
        case 0x2B:   opcode_0x2B();    break;  // ANC - Immediate
        case 0x2C:   opcode_0x2C();    break;  // BIT - Absolute
        case 0x2D:   opcode_0x2D();    break;  // AND - Absolute
        case 0x2E:   opcode_0x2E();    break;  // ROL - Rotate Left - Absolute
        case 0x2F:   opcode_0x2F();    break;  // RLA - Absolute
        case 0x30:   opcode_0x30();    break;  // BMI - Branch on Minus (N Flag Set)
        case 0x31:   opcode_0x31();    break;  // AND - Indirect Indexed
        case 0x32:   opcode_0x32();    break;  // JAM
        case 0x33:   opcode_0x33();    break;  // RLA - Indirect Indexed  Y
        case 0x34:   opcode_0x34();    break;  // NOP - ZeroPage , X
        case 0x35:   opcode_0x35();    break;  // AND - ZeroPage,X
        case 0x36:   opcode_0x36();    break;  // ROL - Rotate Left - ZeroPage , X
        case 0x37:   opcode_0x37();    break;  // RLA - ZeroPage , X
        case 0x38:   opcode_0x38();    break;  // SEC
        case 0x39:   opcode_0x39();    break;  // AND - Absolute,Y
        case 0x3A:   opcode_0xEA();    break;  // NOP
        case 0x3B:   opcode_0x3B();    break;  // RLA - Absolute , Y
        case 0x3C:   opcode_0x3C();    break;  // NOP - Absolute , X
        case 0x3D:   opcode_0x3D();    break;  // AND - Absolute,X
        case 0x3E:   opcode_0x3E();    break;  // ROL - Rotate Left - Absolute , X
        case 0x3F:   opcode_0x3F();    break;  // RLA - Absolute , X
        case 0x40:   opcode_0x40();    break;  // RTI - Return from Interrupt
        case 0x41:   opcode_0x41();    break;  // EOR - Indexed Indirect X
        case 0x42:   opcode_0x42();    break;  // JAM
        case 0x43:   opcode_0x43();    break;  // SRE - Indexed Indirect X
        case 0x44:   opcode_0x44();    break;  // NOP - ZeroPage
        case 0x45:   opcode_0x45();    break;  // EOR - ZeroPage
        case 0x46:   opcode_0x46();    break;  // LSR - Logical Shift Right - ZeroPage
        case 0x47:   opcode_0x47();    break;  // SRE - ZeroPage
        case 0x48:   opcode_0x48();    break;  // PHA - Push Accumulator to the stack
        case 0x49:   opcode_0x49();    break;  // EOR - Immediate
        case 0x4A:   opcode_0x4A();    break;  // LSR A
        case 0x4B:   opcode_0x4B();    break;  // ALR - Immediate
        case 0x4C:   opcode_0x4C();    break;  // JMP - Jump Absolute
        case 0x4D:   opcode_0x4D();    break;  // EOR - Absolute
        case 0x4E:   opcode_0x4E();    break;  // LSR - Logical Shift Right - Absolute
        case 0x4F:   opcode_0x4F();    break;  // SRE - Absolute
        case 0x50:   opcode_0x50();    break;  // BVC - Branch on Overflow Clear
        case 0x51:   opcode_0x51();    break;  // EOR - Indirect Indexed  Y
        case 0x52:   opcode_0x52();    break;  // JAM
        case 0x53:   opcode_0x53();    break;  // SRE - Indirect Indexed  Y
        case 0x54:   opcode_0x54();    break;  // NOP - ZeroPage , X
        case 0x55:   opcode_0x55();    break;  // EOR - ZeroPage,X
        case 0x56:   opcode_0x56();    break;  // LSR - Logical Shift Right - ZeroPage , X
        case 0x57:   opcode_0x57();    break;  // SRE - ZeroPage , X
        case 0x58:   opcode_0x58();    break;  // CLI
        case 0x59:   opcode_0x59();    break;  // EOR - Absolute,Y
        case 0x5A:   opcode_0xEA();    break;  // NOP
        case 0x5B:   opcode_0x5B();    break;  // RE - Absolute , Y
        case 0x5C:   opcode_0x5C();    break;  // NOP - Absolute , X
        case 0x5D:   opcode_0x5D();    break;  // EOR - Absolute,X
        case 0x5E:   opcode_0x5E();    break;  // LSR - Logical Shift Right - Absolute , X
        case 0x5F:   opcode_0x5F();    break;  // SRE - Absolute , X
        case 0x60:   opcode_0x60();    break;  // RTS - Return from Subroutine
        case 0x61:   opcode_0x61();    break;  // ADC - Indexed Indirect X
        case 0x62:   opcode_0x62();    break;  // JAM
        case 0x63:   opcode_0x63();    break;  // RRA - Indexed Indirect X
        case 0x64:   opcode_0x64();    break;  // NOP - ZeroPage
        case 0x65:   opcode_0x65();    break;  // ADC - ZeroPage
        case 0x66:   opcode_0x66();    break;  // ROR - Rotate Right - ZeroPage
        case 0x67:   opcode_0x67();    break;  // RRA - ZeroPage
        case 0x68:   opcode_0x68();    break;  // PLA - Pop Accumulator from the stack
        case 0x69:   opcode_0x69();    break;  // ADC - Immediate
        case 0x6A:   opcode_0x6A();    break;  // ROR A
        case 0x6B:   opcode_0x6B();    break;  // ARR - Immediate
        case 0x6C:   opcode_0x6C();    break;  // JMP - Jump Indirect
        case 0x6D:   opcode_0x6D();    break;  // ADC - Absolute
        case 0x6E:   opcode_0x6E();    break;  // ROR - Rotate Right - Absolute
        case 0x6F:   opcode_0x6F();    break;  // RRA - Absolute
        case 0x70:   opcode_0x70();    break;  // BVS - Branch on Overflow Set
        case 0x71:   opcode_0x71();    break;  // ADC - Indirect Indexed  Y
        case 0x72:   opcode_0x72();    break;  // JAM
        case 0x73:   opcode_0x73();    break;  // RRA - Indirect Indexed  Y
        case 0x74:   opcode_0x74();    break;  // NOP - ZeroPage , X
        case 0x75:   opcode_0x75();    break;  // ADC - ZeroPage , X
        case 0x76:   opcode_0x76();    break;  // ROR - Rotate Right - ZeroPage , X
        case 0x77:   opcode_0x77();    break;  // RRA - ZeroPage , X
        case 0x78:   opcode_0x78();    break;  // SEI
        case 0x79:   opcode_0x79();    break;  // ADC - Absolute , Y
        case 0x7A:   opcode_0xEA();    break;  // NOP
        case 0x7B:   opcode_0x7B();    break;  // RRA - Absolute , Y
        case 0x7C:   opcode_0x7C();    break;  // NOP - Absolute , X
        case 0x7D:   opcode_0x7D();    break;  // ADC - Absolute , X
        case 0x7E:   opcode_0x7E();    break;  // ROR - Rotate Right - Absolute , X
        case 0x7F:   opcode_0x7F();    break;  // RRA - Absolute , X
        case 0x80:   opcode_0x80();    break;  // NOP - Immediate
        case 0x81:   opcode_0x81();    break;  // STA - Indexed Indirect X
        case 0x82:   opcode_0x82();    break;  // NOP - Immediate
        case 0x83:   opcode_0x83();    break;  // SAX - Indexed Indirect X
        case 0x84:   opcode_0x84();    break;  // STY - ZeroPage
        case 0x85:   opcode_0x85();    break;  // STA - ZeroPage
        case 0x86:   opcode_0x86();    break;  // STX - ZeroPage
        case 0x87:   opcode_0x87();    break;  // SAX - ZeroPage
        case 0x88:   opcode_0x88();    break;  // DEY
        case 0x89:   opcode_0x89();    break;  // NOP - Immediate
        case 0x8A:   opcode_0x8A();    break;  // TXA
        case 0x8B:   opcode_0x8B();    break;  // ANE - Immediate
        case 0x8C:   opcode_0x8C();    break;  // STY - Absolute
        case 0x8D:   opcode_0x8D();    break;  // STA - Absolute
        case 0x8E:   opcode_0x8E();    break;  // STX - Absolute
        case 0x8F:   opcode_0x8F();    break;  // SAX - Absolute
        case 0x90:   opcode_0x90();    break;  // BCC - Branch on Carry Clear
        case 0x91:   opcode_0x91();    break;  // STA - Indirect Indexed  Y
        case 0x92:   opcode_0x92();    break;  // JAM
        case 0x93:   opcode_0x93();    break;  // SHA - ZeroPage , Y
        case 0x94:   opcode_0x94();    break;  // STY - ZeroPage , X
        case 0x95:   opcode_0x95();    break;  // STA - ZeroPage , X
        case 0x96:   opcode_0x96();    break;  // STX - ZeroPage , Y
        case 0x97:   opcode_0x97();    break;  // SAX - ZeroPage , Y
        case 0x98:   opcode_0x98();    break;  // TYA
        case 0x99:   opcode_0x99();    break;  // STA - Absolute , Y
        case 0x9A:   opcode_0x9A();    break;  // TXS
        case 0x9B:   opcode_0x9B();    break;  // TAS - Absolute , Y 
        case 0x9C:   opcode_0x9C();    break;  // SHY - Absolute , X
        case 0x9D:   opcode_0x9D();    break;  // STA - Absolute , X
        case 0x9E:   opcode_0x9E();    break;  // SHX - Absolute , Y
        case 0x9F:   opcode_0x9F();    break;  // SHA - Absolute , Y
        case 0xA0:   opcode_0xA0();    break;  // LDY - Immediate
        case 0xA1:   opcode_0xA1();    break;  // LDA - Indexed Indirect X
        case 0xA2:   opcode_0xA2();    break;  // LDX - Immediate
        case 0xA3:   opcode_0xA3();    break;  // LAX - Indexed Indirect X
        case 0xA4:   opcode_0xA4();    break;  // LDY - ZeroPage
        case 0xA5:   opcode_0xA5();    break;  // LDA - ZeroPage
        case 0xA6:   opcode_0xA6();    break;  // LDX - ZeroPage
        case 0xA7:   opcode_0xA7();    break;  // LAX - ZeroPage
        case 0xA8:   opcode_0xA8();    break;  // TAY
        case 0xA9:   opcode_0xA9();    break;  // LDA - Immediate
        case 0xAA:   opcode_0xAA();    break;  // TAX
        case 0xAB:   opcode_0xAB();    break;  // LAX - Immediate
        case 0xAC:   opcode_0xAC();    break;  // LDY - Absolute
        case 0xAD:   opcode_0xAD();    break;  // LDA - Absolute
        case 0xAE:   opcode_0xAE();    break;  // LDX - Absolute
        case 0xAF:   opcode_0xAF();    break;  // LAX - Absolute
        case 0xB0:   opcode_0xB0();    break;  // BCS - Branch on Carry Set
        case 0xB1:   opcode_0xB1();    break;  // LDA - Indirect Indexed  Y
        case 0xB2:   opcode_0xB2();    break;  // JAM
        case 0xB3:   opcode_0xB3();    break;  // LAX - Indirect Indexed  Y
        case 0xB4:   opcode_0xB4();    break;  // LDY - ZeroPage , X
        case 0xB5:   opcode_0xB5();    break;  // LDA - ZeroPage , X
        case 0xB6:   opcode_0xB6();    break;  // LDX - ZeroPage , Y
        case 0xB7:   opcode_0xB7();    break;  // LAX - ZeroPage , Y
        case 0xB8:   opcode_0xB8();    break;  // CLV
        case 0xB9:   opcode_0xB9();    break;  // LDA - Absolute , Y
        case 0xBA:   opcode_0xBA();    break;  // TSX
        case 0xBB:   opcode_0xBB();    break;  // LAS - Absolute , Y
        case 0xBC:   opcode_0xBC();    break;  // LDY - Absolute , X
        case 0xBD:   opcode_0xBD();    break;  // LDA - Absolute , X
        case 0xBE:   opcode_0xBE();    break;  // LDX - Absolute , Y
        case 0xBF:   opcode_0xBF();    break;  // LAX - Absolute , Y
        case 0xC0:   opcode_0xC0();    break;  // CPY - Immediate
        case 0xC1:   opcode_0xC1();    break;  // CMP - Indexed Indirect X
        case 0xC2:   opcode_0xC2();    break;  // NOP - Immediate
        case 0xC3:   opcode_0xC3();    break;  // DCP - Indexed Indirect X
        case 0xC4:   opcode_0xC4();    break;  // CPY - ZeroPage
        case 0xC5:   opcode_0xC5();    break;  // CMP - ZeroPage
        case 0xC6:   opcode_0xC6();    break;  // DEC - ZeroPage
        case 0xC7:   opcode_0xC7();    break;  // DCP - ZeroPage
        case 0xC8:   opcode_0xC8();    break;  // INY
        case 0xC9:   opcode_0xC9();    break;  // CMP - Immediate
        case 0xCA:   opcode_0xCA();    break;  // DEX
        case 0xCB:   opcode_0xCB();    break;  // SBX - Immediate
        case 0xCC:   opcode_0xCC();    break;  // CPY - Absolute
        case 0xCD:   opcode_0xCD();    break;  // CMP - Absolute
        case 0xCE:   opcode_0xCE();    break;  // DEC - Absolute
        case 0xCF:   opcode_0xCF();    break;  // DCP - Absolute
        case 0xD0:   opcode_0xD0();    break;  // BNE - Branch on Zero Clear
        case 0xD1:   opcode_0xD1();    break;  // CMP - Indirect Indexed  Y
        case 0xD2:   opcode_0xD2();    break;  // JAM
        case 0xD3:   opcode_0xD3();    break;  // DCP - Indirect Indexed  Y
        case 0xD4:   opcode_0xD4();    break;  // NOP - ZeroPage , X
        case 0xD5:   opcode_0xD5();    break;  // CMP - ZeroPage , X
        case 0xD6:   opcode_0xD6();    break;  // DEC - ZeroPage , X
        case 0xD7:   opcode_0xD7();    break;  // DCP - ZeroPage , X
        case 0xD8:   opcode_0xD8();    break;  // CLD
        case 0xD9:   opcode_0xD9();    break;  // CMP - Absolute , Y
        case 0xDA:   opcode_0xEA();    break;  // NOP
        case 0xDB:   opcode_0xDB();    break;  // DCP - Absolute , Y
        case 0xDC:   opcode_0xDC();    break;  // NOP - Absolute , X
        case 0xDD:   opcode_0xDD();    break;  // CMP - Absolute , X
        case 0xDE:   opcode_0xDE();    break;  // DEC - Absolute , X
        case 0xDF:   opcode_0xDF();    break;  // DCP - Absolute , X
        case 0xE0:   opcode_0xE0();    break;  // CPX - Immediate
        case 0xE1:   opcode_0xE1();    break;  // SBC - Indexed Indirect X
        case 0xE2:   opcode_0xE2();    break;  // NOP - Immediate
        case 0xE3:   opcode_0xE3();    break;  // ISC - Indexed Indirect X
        case 0xE4:   opcode_0xE4();    break;  // CPX - ZeroPage
        case 0xE5:   opcode_0xE5();    break;  // SBC - ZeroPage
        case 0xE6:   opcode_0xE6();    break;  // INC - ZeroPage
        case 0xE7:   opcode_0xE7();    break;  // ISC - ZeroPage
        case 0xE8:   opcode_0xE8();    break;  // INX
        case 0xE9:   opcode_0xE9();    break;  // SBC - Immediate
        case 0xEA:   opcode_0xEA();    break;  // NOP
        case 0xEB:   opcode_0xE9();    break;  // SBC - Immediate
        case 0xEC:   opcode_0xEC();    break;  // CPX - Absolute
        case 0xED:   opcode_0xED();    break;  // SBC - Absolute
        case 0xEE:   opcode_0xEE();    break;  // INC - Absolute
        case 0xEF:   opcode_0xEF();    break;  // ISC - Absolute
        case 0xF0:   opcode_0xF0();    break;  // BEQ - Branch on Zero Set
        case 0xF1:   opcode_0xF1();    break;  // SBC - Indirect Indexed  Y
        case 0xF2:   opcode_0xF2();    break;  // JAM
        case 0xF3:   opcode_0xF3();    break;  // ISC - Indirect Indexed  Y
        case 0xF4:   opcode_0xF4();    break;  // NOP - ZeroPage , X
        case 0xF5:   opcode_0xF5();    break;  // SBC - ZeroPage , X
        case 0xF6:   opcode_0xF6();    break;  // INC - ZeroPage , X
        case 0xF7:   opcode_0xF7();    break;  // ISC - ZeroPage , X
        case 0xF8:   opcode_0xF8();    break;  // SED
        case 0xF9:   opcode_0xF9();    break;  // SBC - Absolute , Y
        case 0xFA:   opcode_0xEA();    break;  // NOP
        case 0xFB:   opcode_0xFB();    break;  // ISC - Absolute , Y
        case 0xFC:   opcode_0xFC();    break;  // NOP - Absolute , X
        case 0xFD:   opcode_0xFD();    break;  // SBC - Absolute , X
        case 0xFE:   opcode_0xFE();    break;  // INC - Absolute , X
        case 0xFF:   opcode_0xFF();    break;  // ISC - Absolute , X
      }

    } 
}
