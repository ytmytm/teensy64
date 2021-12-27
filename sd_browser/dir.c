/*
 * dir.c
 *
 *  Created on: 10.01.2009
 *      Author: bader
 *
 * DraCopy (dc*) is a simple copy program.
 * DraBrowse (db*) is a simple file browser.
 *
 * Since both programs make use of kernal routines they shall
 * be able to work with most file oriented IEC devices.
 *
 * Created 2009 by Sascha Bader
 *
 * The code can be used freely as long as you retain
 * a notice describing original source and author.
 *
 * THE PROGRAMS ARE DISTRIBUTED IN THE HOPE THAT THEY WILL BE USEFUL,
 * BUT WITHOUT ANY WARRANTY. USE THEM AT YOUR OWN RISK!
 *
 * Newer versions might be available here: http://www.sascha-bader.de/html/code.html
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <conio.h>
#include <peekpoke.h>
#include "dir.h"
#include "defines.h"

/*
 * read a devices directory
 */
void readDir(Directory *dir, char *path)
{
	DirElement *current;

    // initialize directory
    dir->selected = 0;
	dir->no_of_elements = 0;
	strncpy(dir->name, path, FILENAME_LENGTH);

    // Teensy64 directory protocol - LOAD "path"
	// fill in dir structure of DirElements up to MAX_DIR_ELEMENTS
	cbm_k_setnam(dir->name); // change dir to provided name
	cbm_k_setlfs(15,1,0); // 'load' dir on LFN=15 with secondary address = 0 ->  change dir to path and load into dir structure with provided address
	cbm_k_load(0, (unsigned)dir->elements);

	// update dir->no_of_elements
	while (dir->no_of_elements < MAX_DIR_ELEMENTS) { // TODO: Test limit (off by one?)
		current = dir->elements + dir->no_of_elements;
	    if (current->type == FILE_NONE) {
    	    // end of dir
	    	break;
		} else {
			dir->no_of_elements++;
		}
	}
}
