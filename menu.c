/*
Copyright 2005, 2006, 2007 Dennis van Weeren
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// 2009-11-14   - OSD labels changed
// 2009-12-15   - added display of directory name extensions
// 2010-01-09   - support for variable number of tracks
// 2016-06-01   - improvements to 8-bit menu

//#include "AT91SAM7S256.h"
//#include "stdbool.h"
#include <stdlib.h>
#include <inttypes.h>
#include "stdio.h"
#include "string.h"
#include "errors.h"
#include "mmc.h"
#include "fat.h"
#include "osd.h"
#include "fpga.h"
#include "fdd.h"
#include "hdd.h"
#include "hardware.h"
#include "firmware.h"
#include "config.h"
#include "menu.h"
#include "user_io.h"
#include "tos.h"
#include "cdc_control.h"
#include "debug.h"
#include "boot.h"
#include "archie.h"
#include "usb/joymapping.h"

// test features (not used right now)
// #define ALLOW_TEST_MENU 0 //remove to disable in prod version


// other constants
#define DIRSIZE 8 // number of items in directory display window

// TODO!
#define SPIN() asm volatile ( "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0");

unsigned char menustate = MENU_NONE1;
unsigned char parentstate;
unsigned char menusub = 0;
unsigned char menusub_last = 0; //for when we allocate it dynamically and need to know last row
unsigned int menumask = 0; // Used to determine which rows are selectable...
unsigned long menu_timer = 0;

extern unsigned char drives;
extern adfTYPE df[4];

extern configTYPE config;
extern fileTYPE file;
extern char s[40];

extern unsigned char fat32;

extern DIRENTRY DirEntry[MAXDIRENTRIES];
extern unsigned char sort_table[MAXDIRENTRIES];
extern unsigned char nDirEntries;
extern unsigned char iSelectedEntry;
extern unsigned long iCurrentDirectory;
extern char DirEntryLFN[MAXDIRENTRIES][261];
char DirEntryInfo[MAXDIRENTRIES][5]; // disk number info of dir entries
char DiskInfo[5]; // disk number info of selected entry

extern const char version[];

const char *config_tos_mem[] =  {"512 kB", "1 MB", "2 MB", "4 MB", "8 MB", "14 MB", "--", "--" };
const char *config_tos_wrprot[] =  {"none", "A:", "B:", "A: and B:"};
const char *config_tos_usb[] =  {"none", "control", "debug", "serial", "parallel", "midi"};

const char *config_filter_msg[] =  {"none", "HORIZONTAL", "VERTICAL", "H+V"};
const char *config_memory_chip_msg[] = {"0.5 MB", "1.0 MB", "1.5 MB", "2.0 MB"};
const char *config_memory_slow_msg[] = {"none  ", "0.5 MB", "1.0 MB", "1.5 MB"};
const char *config_scanlines_msg[] = {"off", "dim", "black"};
const char *config_dither_msg[] = {"off", "SPT", "RND", "S+R"};
const char *config_memory_fast_msg[] = {"none  ", "2.0 MB", "4.0 MB","24.0 MB","24.0 MB"};
const char *config_cpu_msg[] = {"68000 ", "68010", "-----","68020"};
const char *config_hdf_msg[] = {"Disabled", "Hardfile (disk img)", "MMC/SD card", "MMC/SD partition 1", "MMC/SD partition 2", "MMC/SD partition 3", "MMC/SD partition 4"};
const char *config_chipset_msg[] = {"OCS-A500", "OCS-A1000", "ECS", "---", "---", "---", "AGA", "---"};
const char *config_turbo_msg[] = {"none", "CHIPRAM", "KICK", "BOTH"};
char *config_autofire_msg[] = {"        AUTOFIRE OFF", "        AUTOFIRE FAST", "        AUTOFIRE MEDIUM", "        AUTOFIRE SLOW"};
const char *config_cd32pad_msg[] =  {"OFF", "ON"};

enum HelpText_Message {HELPTEXT_NONE,HELPTEXT_MAIN,HELPTEXT_HARDFILE,HELPTEXT_CHIPSET,HELPTEXT_MEMORY,HELPTEXT_VIDEO};
const char *helptexts[]={
	0,
	"                                Welcome to MiST!  Use the cursor keys to navigate the menus.  Use space bar or enter to select an item.  Press Esc or F12 to exit the menus.  Joystick emulation on the numeric keypad can be toggled with the numlock key, while pressing Ctrl-Alt-0 (numeric keypad) toggles autofire mode.",
	"                                Minimig can emulate an A600 IDE harddisk interface.  The emulation can make use of Minimig-style hardfiles (complete disk images) or UAE-style hardfiles (filesystem images with no partition table).  It is also possible to use either the entire SD card or an individual partition as an emulated harddisk.",
	"                                Minimig's processor core can emulate a 68000 or 68020 processor (though the 68020 mode is still experimental.)  If you're running software built for 68000, there's no advantage to using the 68020 mode, since the 68000 emulation runs just as fast.",
	"                                Minimig can make use of up to 2 megabytes of Chip RAM, up to 1.5 megabytes of Slow RAM (A500 Trapdoor RAM), and up to 24 megabytes of true Fast RAM.  To use the HRTmon feature you will need a file on the SD card named hrtmon.rom.",
	"                                Minimig's video features include a blur filter, to simulate the poorer picture quality on older monitors, and also scanline generation to simulate the appearance of a screen with low vertical resolution.",
	0
};

// one screen width
const char* HELPTEXT_SPACER= "                                ";
char helptext_custom[320];

const char* scanlines[]={"Off","25%","50%","75%"};
const char* stereo[]={"Mono","Stereo"};
const char* atari_chipset[]={"ST","STE","MegaSTE","STEroids"};

unsigned char config_autofire = 0;

// file selection menu variables
char fs_pFileExt[4] = "xxx";
unsigned char fs_Options;
unsigned char fs_MenuSelect;
unsigned char fs_MenuCancel;

void SelectFile(char* pFileExt, unsigned char Options, unsigned char MenuSelect, unsigned char MenuCancel, char chdir)
{
  // this function displays file selection menu

  iprintf("%s - %s\n", pFileExt, fs_pFileExt);
  
  if (strncmp(pFileExt, fs_pFileExt, 3) != 0) // check desired file extension
  { // if different from the current one go to the root directory and init entry buffer
    ChangeDirectory(DIRECTORY_ROOT);

    // for 8 bit cores try to 
    if((user_io_core_type() == CORE_TYPE_8BIT) && chdir) {
      user_io_create_config_name(s);
      // try to change into subdir named after the core
      strcpy(s+8, "   ");
      iprintf("Trying to open work dir \"%s\"\n", s);
      
      ScanDirectory(SCAN_INIT, "",  SCAN_DIR | FIND_DIR);

      // no return flag :(, so scan 10 times blindly...
      for(;;) {
	int i;
	char res = 0;
	unsigned short last_StartCluster = DirEntry[0].StartCluster;

	for(i=0;i<nDirEntries;i++) {
	  //iprintf("cmp %11s %11s\n", DirEntry[i].Name, s);
	  if(strncasecmp(DirEntry[i].Name, s, 11) == 0) {
	    ChangeDirectory(DirEntry[i].StartCluster + (fat32 ? (DirEntry[i].HighCluster & 0x0FFF) << 16 : 0));
	    res = 1;
            break;
          }
        }
	// found directory: stop searching
        if(res) break;

	// last search returned less than 8 entries: Stop searching since 
	// there sure aren't more
	if(nDirEntries != 8) 
	  break;

	// get next 8 directory entries
	iSelectedEntry = MAXDIRENTRIES -1;
	ScanDirectory(SCAN_NEXT_PAGE, "",  SCAN_DIR | FIND_DIR);

	// if 8 entries are returned check if the start cluster of the first entry
	// is the same as the first one in the previous list. If it is, then this
	// is the same list and we are done
	if((nDirEntries == 8) && (DirEntry[0].StartCluster == last_StartCluster)) 
	  break;
      }
    }
    
    ScanDirectory(SCAN_INIT, pFileExt, Options);
  }

  iprintf("pFileExt = %3s\n", pFileExt);
  strcpy(fs_pFileExt, pFileExt);
  //  fs_pFileExt = pFileExt;
  fs_Options = Options;
  fs_MenuSelect = MenuSelect;
  fs_MenuCancel = MenuCancel;
  
  menustate = MENU_FILE_SELECT1;
}


static void substrcpy(char *d, char *s, char idx) {
  char p = 0;

  while(*s) {
    if((p == idx) && *s && (*s != ','))
      *d++ = *s;

    if(*s == ',')
      p++;

    s++;
  }

  *d = 0;
}

#define STD_EXIT       "            exit"
#define STD_SPACE_EXIT "        SPACE to exit"
#define STD_COMBO_EXIT " Hold ESC then SPACE to exit"

#define JOY_VID 		 "VID:"
#define JOY_PID 	   "PID:"

#define HELPTEXT_DELAY 10000
#define FRAME_DELAY 150

//assumes big endian
void siprintbinary(char* buffer, size_t const size, void const * const ptr)
{
    unsigned char *b = (unsigned char*) ptr;
    unsigned char byte;
    int i, j;
		siprintf(buffer, "%*s", size-1, "");
    for (i=size-1;i>=0;i--)
    {
        for (j=0;j<8;j++)
        {
            byte = (b[i] >> j) & 1;
            if(byte)
							strcat(buffer, "1");
						else
							strcat(buffer, "0");
        }
    }
		return;
}

void get_joystick_state( char *joy_string, char *joy_string2, unsigned int joy_num ) {	
	// helper to get joystick status (both USB or DB9)
	uint16_t vjoy;
	memset(joy_string, '\0', sizeof(joy_string));
	memset(joy_string2, '\0', sizeof(joy_string2));
	if (joy_num==0) {
		vjoy = (uint16_t)OsdJoyGet();
		vjoy |= OsdJoyGetExtra() << 8;
	} else {
		vjoy = (uint16_t)OsdJoyGet2();
		vjoy |= OsdJoyGetExtra2() << 8;
	}
	if (vjoy==0) {
		memset(joy_string2, ' ', 8);
		memset(joy_string2+8, '\x14', 1);
		return;		
	}
	strcpy(joy_string,  "        \x12   X Y L R L2 R2 L3");
	strcpy(joy_string2, "      < \x13 > A B Sel Sta R3");
	if(!(vjoy & JOY_UP)) memset(joy_string+8, ' ', 1);
	if(!(vjoy & JOY_X))  memset(joy_string+12, ' ', 1);
	if(!(vjoy & JOY_Y))  memset(joy_string+14, ' ', 1);
	if(!(vjoy & JOY_L))  memset(joy_string+16, ' ', 1);
	if(!(vjoy & JOY_R))  memset(joy_string+18, ' ', 1);
	if(!(vjoy & JOY_L2))  memset(joy_string+20, ' ', 2);
	if(!(vjoy & JOY_R2))  memset(joy_string+23, ' ', 2);
	if(!(vjoy & JOY_L3))  memset(joy_string+26, ' ', 2);
	if(!(vjoy & JOY_LEFT)) 	memset(joy_string2+6, ' ', 1);
	if(!(vjoy & JOY_DOWN))  memset(joy_string2+8, '\x14', 1);
	if(!(vjoy & JOY_RIGHT)) memset(joy_string2+10, ' ', 1);
	if(!(vjoy & JOY_A))  		memset(joy_string2+12, ' ', 1);
	if(!(vjoy & JOY_B))  		memset(joy_string2+14, ' ', 1);
	if(!(vjoy & JOY_SELECT))memset(joy_string2+16, ' ', 3);
	if(!(vjoy & JOY_START)) memset(joy_string2+20, ' ', 3);
	if(!(vjoy & JOY_R3))  	memset(joy_string2+24, ' ', 2);
	
	return;
}

void get_joystick_state_usb( char *s, unsigned char joy_num ) {	
	/* USB specific - current "raw" state 
	  (in reverse binary format to correspont to MIST.INI mapping entries)
	*/
	char buffer[5];
	if (OsdNumJoysticks()==0 || (joy_num==1 && OsdNumJoysticks()<2)) 
	{
		strcpy( s, " ");
		return;
	}
	char binary_string[9]="00000000";
	unsigned char joy = 0;
	if(joy_num==0)
		joy = OsdUsbJoyGet();
	else
		joy = OsdUsbJoyGetB();
	siprintf(s, "    USB: ");
	siprintbinary(binary_string, sizeof(joy), &joy);
	binary_string[0]=binary_string[0]=='1'?'>':'-';
	binary_string[1]=binary_string[1]=='1'?'<':'-';
	binary_string[2]=binary_string[2]=='1'?'\x13':'-';
	binary_string[3]=binary_string[3]=='1'?'\x12':'-';
	
	memcpy( buffer, &binary_string[0], 4 );
	buffer[4]='\0';
	strcat(s, buffer );
	strcat(s, " ");
	memcpy( buffer, &binary_string[4], 4 );
	buffer[4]='\0';
	strcat(s, buffer );
	if(joy_num==0)
		joy = OsdUsbJoyGetExtra();
	else
		joy = OsdUsbJoyGetExtraB();
	siprintbinary(binary_string, sizeof(joy), &joy);
	strcat(s, binary_string);
	return;
}
			
void append_joystick_usbid ( char *usb_id, unsigned int usb_vid, unsigned int usb_pid ) {
	/*
	appends a string with VID and PID numbers. 
	Make sure to provide long enough string in usb_id
	*/
	unsigned short i;
	char vid[5] = "    ";
	char pid[5] = "    ";
	itoa(usb_vid, vid, 16);
	itoa(usb_pid, pid, 16);
	if(strlen(vid)<4) {
		for(i=5;i>0;i--) {
			vid[i]=vid[i-1];
		}
		vid[0]='0';
	}
	if(strlen(pid)<4) {
		for(i=5;i>0;i--) {
			pid[i]=pid[i-1];
		}
		pid[0]='0';
	}
	strcat( usb_id, JOY_VID);
	strcat( usb_id, vid);
	strcat( usb_id, " ");
	strcat( usb_id, JOY_PID);
	strcat( usb_id, pid);
	return;
}		
		
void get_joystick_id ( char *usb_id, unsigned char joy_num, short raw_id ) {
	/*
	Builds a string containing the USB VID/PID information of a joystick
	*/
	unsigned int usb_vid; 
	unsigned int usb_pid; 
	char buffer[32]="";
	if (raw_id==0) {
		if (OsdNumJoysticks()==0 || (joy_num==1 && OsdNumJoysticks()<2)) 
		{
			strcpy( usb_id, "      ");
			strcat( usb_id, "Atari DB9 Joystick");
			return;
		}
	}
	if (joy_num==1) {
		usb_vid = OsdUsbVidGetB();
		usb_pid = OsdUsbPidGetB();
	} else {
		usb_vid = OsdUsbVidGet();
		usb_pid = OsdUsbPidGet();
	}
	memset(usb_id, '\0', sizeof(usb_id));
	if (usb_vid>0) {
		if (raw_id == 0) {
			strcpy(buffer, get_joystick_alias( usb_vid, usb_pid ));
		}
		if(strlen(buffer)==0) {
			append_joystick_usbid( buffer, usb_vid, usb_pid );
		}		
	} else {
		strcpy(buffer, "Atari DB9 Joystick");
	}	
	if(raw_id == 0)
		siprintf(usb_id, "%*s", (28-strlen(buffer))/2, " ");
	else
		strcpy(usb_id, "");
	strcat(usb_id, buffer);
	return;
}

/* translates a USB modifier bit into a PS2 scancode */
void assign_ps2_modifier ( unsigned char mod, unsigned int* keys_ps2, unsigned char offset, unsigned int ps2_value) {
	unsigned int i;
	if(mod&offset) {
		for(i=0; i<4; i++) {
			if(keys_ps2[i]==0) {
				keys_ps2[i] = ps2_value;
				return;
			}
		}
	}
}
		
			
void HandleUI(void)
{
	char *p;
	unsigned char i, c, m, up, down, select, menu, right, left, plus, minus;
	unsigned long len;
	static hardfileTYPE t_hardfile[2]; // temporary copy of former hardfile configuration
	static unsigned char ctrl = false;
	static unsigned char lalt = false;
	char enable;
	static long helptext_timer;
	static const char *helptext;
	static char helpstate=0;
	unsigned char keys[6] = {0,0,0,0,0,0};
	unsigned int keys_ps2[6] = {0,0,0,0,0,0};
	
	/* check joystick status */
	char joy_string[32];
	char joy_string2[32];
	char usb_id[64];
		
	// get user control codes
	c = OsdGetCtrl();
	
	// decode and set events
	menu = false;
	select = false;
	up = false;
	down = false;
	left = false;
	right = false;
	plus=false;
	minus=false;
	
	switch (c)
	{
		case KEY_CTRL :
			ctrl = true;
			break;
		case KEY_CTRL | KEY_UPSTROKE :
			ctrl = false;
			break;
		case KEY_LALT :
			lalt = true;
			break;
		case KEY_LALT | KEY_UPSTROKE :
			lalt = false;
			break;
		case KEY_KP0 :
			if (ctrl && lalt)
			{
				if (menustate == MENU_NONE2 || menustate == MENU_INFO)
				{
					config_autofire++;
					config_autofire &= 3;
					ConfigAutofire(config_autofire);
					if (menustate == MENU_NONE2 || menustate == MENU_INFO)
							InfoMessage(config_autofire_msg[config_autofire]);
				}
			}
			break;
			
		case KEY_MENU:
			menu = true;
			break;

		// Within the menu the esc key acts as the menu key. problem:
		// if the menu is left with a press of ESC, then the follwing
		// break code for the ESC key when the key is released will 
		// reach the core which never saw the make code. Simple solution:
		// react on break code instead of make code
		case KEY_ESC | KEY_UPSTROKE :
			if (menustate != MENU_NONE2)
				menu = true;
			break;
		case KEY_ENTER :
		case KEY_SPACE :
				select = true;
				break;
		case KEY_UP:
				up = true;
				break;
		case KEY_DOWN:
				down = true;
				break;
		case KEY_LEFT :
				left = true;
				break;
		case KEY_RIGHT :
				right = true;
				break;
		case KEY_KPPLUS :
		plus=true;
				break;
		case KEY_KPMINUS :
		minus=true;
				break;
	}

	if(menu || select || up || down || left || right )
	{
		if(helpstate)
			OsdWrite(7,STD_EXIT,(menumask-((1<<(menusub+1))-1))<=0,0); // Redraw the Exit line...
		helpstate=0;
		helptext_timer=GetTimer(HELPTEXT_DELAY);
	}

	if(helptext)
	{
		if(helpstate<9)
		{
			if(CheckTimer(helptext_timer))
			{
				helptext_timer=GetTimer(FRAME_DELAY);
				OsdWriteOffset(7,STD_EXIT,0,0,helpstate);
				++helpstate;
			}
		}
		else if(helpstate==9)
		{
			ScrollReset();
			++helpstate;
		}
		else
			ScrollText(7,helptext,0,0,0);
	}

	// Standardised menu up/down.
	// The screen should set menumask, bit 0 to make the top line selectable, bit 1 for the 2nd line, etc.
	// (Lines in this context don't have to correspond to rows on the OSD.)
	// Also set parentstate to the appropriate menustate.
	if(menumask)
	{
		if (down && (menumask>=(1<<(menusub+1))))	// Any active entries left?
		{
			do
				menusub++;
			while((menumask & (1<<menusub)) == 0);
            menustate = parentstate;
    }

		if (up && menusub > 0 && (menumask<<(8-menusub)))
		{
			do
				--menusub;
			while((menumask & (1<<menusub)) == 0);
            menustate = parentstate;
		}
	}

	// Switch to current menu screen
  switch (menustate)
	{
		/******************************************************************/
		/* no menu selected                                               */
		/******************************************************************/
		case MENU_NONE1 :
			helptext=helptexts[HELPTEXT_NONE];
			menumask=0;
			OsdDisable();
			menustate = MENU_NONE2;
			break;

		case MENU_NONE2 :
			if (menu)
			{
				if((user_io_core_type() == CORE_TYPE_MINIMIG) ||
					 (user_io_core_type() == CORE_TYPE_MINIMIG2))
					menustate = MENU_MAIN1;
				else if(user_io_core_type() == CORE_TYPE_MIST)
					menustate = MENU_MIST_MAIN1;
				else if(user_io_core_type() == CORE_TYPE_ARCHIE)
					menustate = MENU_ARCHIE_MAIN1;
				else {
					// the "menu" core is special in jumps directly to the core selection menu
					if(!strcmp(user_io_get_core_name(), "MENU"))
						SelectFile("RBF", SCAN_LFN, MENU_FIRMWARE_CORE_FILE_SELECTED, MENU_NONE1, 0);
					else
						menustate = MENU_8BIT_MAIN1;
				}
				menusub = 0;
				OsdClear();
				OsdEnable(DISABLE_KEYBOARD);
			}
			break;

			/******************************************************************/
			/* archimedes main menu                                           */
			/******************************************************************/

		case MENU_ARCHIE_MAIN1: {
			menumask=0x3f;
			OsdSetTitle("ARCHIE", 0);

			strcpy(s, " Floppy 0: ");
			strcat(s, archie_get_floppy_name(0));
			OsdWrite(0, s, menusub == 0, 0);

			strcpy(s, " Floppy 1: ");
			strcat(s, archie_get_floppy_name(1));
			OsdWrite(1, s, menusub == 1, 0);
			
			strcpy(s, " OS ROM: ");
			strcat(s, archie_get_rom_name());
			OsdWrite(2, s, menusub == 2, 0);
			
			OsdWrite(3, "", 0,0);
			
			// the following is exactly like the atatri st core
			OsdWrite(4, " Firmware & Core           \x16", menusub == 3,0);
			OsdWrite(5, " Save config                ", menusub == 4,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 5,0);
			menustate = MENU_ARCHIE_MAIN2;
			parentstate=MENU_ARCHIE_MAIN1;
		} break;

		case MENU_ARCHIE_MAIN2 :
			// menu key closes menu
			if (menu)
			menustate = MENU_NONE1;
			if(select) {
				switch(menusub) {
					case 0:  // Floppy 0
					case 1:  // Floppy 1
						if(archie_floppy_is_inserted(menusub)) {
							archie_set_floppy(menusub, NULL);
							menustate = MENU_ARCHIE_MAIN1;
						} else
							SelectFile("ADF", SCAN_DIR | SCAN_LFN, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1, 1);
						break;
						
					case 2:  // Load ROM
						SelectFile("ROM", SCAN_LFN, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1, 1);
						break;

					case 3:  // Firmware submenu
						menustate = MENU_FIRMWARE1;
						menusub = 1;
						break;

					case 4:  // Save config
						menustate = MENU_NONE1;
						archie_save_config();
						break;

					case 5:  // Exit
						menustate = MENU_NONE1;
						break;
				}
			}
			break;

		case MENU_ARCHIE_MAIN_FILE_SELECTED : // file successfully selected
			if(menusub == 0) archie_set_floppy(0, &file);
			if(menusub == 1) archie_set_floppy(1, &file);
			if(menusub == 2) archie_set_rom(&file);
			menustate = MENU_ARCHIE_MAIN1;
			break;
			
		/******************************************************************/
		/* 8 bit main menu                                                */
		/******************************************************************/
			
		case MENU_8BIT_MAIN1: {
			char entry=0;
						
			menumask=0;
			p = user_io_get_core_name();
			if(!p[0]) OsdSetTitle("8BIT", OSD_ARROW_RIGHT);
			else      OsdSetTitle(p, OSD_ARROW_RIGHT);

			if(!p[0]) OsdCoreNameSet("8BIT");
			else      OsdCoreNameSet(p);

			// check if there's a file type supported
			p = user_io_8bit_get_string(1);
			if(p && strlen(p)) {
				entry = 1;
				menumask = 3; //allow to choose "exit" at tht end
				strcpy(s, " Load *.");
				strcat(s, p);
				OsdWrite(0, s, menusub==0, 0);
			}

			// add options as requested by core
			i = 2;
			do {
				unsigned char status = user_io_8bit_set_status(0,0);  // 0,0 gets status
							
				p = user_io_8bit_get_string(i);
				//	  iprintf("Option %d: %s\n", i-1, p);

				// check for 'F'ile or 'S'D image strings
				if(p && ((p[0] == 'F') || (p[0] == 'S'))) {
					substrcpy(s, p, 2);
					if(strlen(s)) {
						strcpy(s, " ");
						substrcpy(s+1, p, 2);
						strcat(s, " *.");
					} else {
						if(p[0] == 'F') strcpy(s, " Load *.");
						else            strcpy(s, " Mount *.");
					}
					substrcpy(s+strlen(s), p, 1);
					OsdWrite(entry, s, menusub==entry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
				}

				// check for 'T'oggle strings
				if(p && (p[0] == 'T')) {
					// p[1] is the digit after the O, so O1 is status bit 1
					char x = (status & (1<<(p[1]-'0')))?1:0;

					s[0] = ' ';
					substrcpy(s+1, p, 1);
					OsdWrite(entry, s, menusub == entry,0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
				}

				// check for 'O'ption strings
				if(p && (p[0] == 'O')) {
					// p[1] is the digit after the O, so O1 is status bit 1
					char x = (status & (1<<(p[1]-'0')))?1:0;

					// get currently active option
					substrcpy(s, p, 2+x);
					char l = strlen(s);
					
					s[0] = ' ';
					substrcpy(s+1, p, 1);
					strcat(s, ":");
					l = 26-l-strlen(s); 
					while(l--) strcat(s, " ");

					substrcpy(s+strlen(s), p, 2+x);

					OsdWrite(entry, s, menusub == entry,0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					entry++;
				}
				
				// check for 'V'ersion strings
				if(p && (p[0] == 'V')) {
					
					// p[1] is not used but kept for future use
					char x = p[1];
					
					// get version string
					strcpy(s, OsdCoreName());
					strcat(s," ");
					substrcpy(s+strlen(s), p, 1);
					OsdCoreNameSet(s);
				}
				i++;
			} while(p);

			// exit row
			OsdWrite(7, STD_EXIT, menusub == entry, 0);
			menusub_last=entry; //remember final row
			
			// clear rest of OSD
			for(;entry<7;entry++) 
				OsdWrite(entry, "", 0,0);
			
			menustate = MENU_8BIT_MAIN2;
			parentstate=MENU_8BIT_MAIN1;
			
			// set helptext with core display on top of basic info
			siprintf(helptext_custom, HELPTEXT_SPACER);
			strcat(helptext_custom, OsdCoreName());
			strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
			helptext=helptext_custom;
			
		} break; // end MENU_8BIT_MAIN1

		case MENU_8BIT_MAIN2 :
			// menu key closes menu
			if (menu)
				menustate = MENU_NONE1;
			if(select) {
				
				if (menusub==menusub_last) {
					menustate = MENU_NONE1;
				} else {
				
					char fs_present;
					p = user_io_8bit_get_string(1);
					fs_present = p && strlen(p);

					// entry 0 = file selector
					if(!menusub && fs_present) {
						p = user_io_8bit_get_string(1);

					// use a local copy of "p" since SelectFile will destroy the buffer behind it
					static char ext[4];
					strncpy(ext, p, 4);
					while(strlen(ext) < 3) strcat(ext, " ");
						SelectFile(ext, SCAN_DIR | SCAN_LFN, MENU_8BIT_MAIN_FILE_SELECTED, MENU_8BIT_MAIN1, 1);
					} else {
						p = user_io_8bit_get_string(menusub + (fs_present?1:2));

						if((p[0] == 'F')||(p[0] == 'S')) {
							static char ext[4];
							substrcpy(ext, p, 1);
							while(strlen(ext) < 3) strcat(ext, " ");
							SelectFile(ext, SCAN_DIR | SCAN_LFN, 
							(p[0] == 'F')?MENU_8BIT_MAIN_FILE_SELECTED:MENU_8BIT_MAIN_IMAGE_SELECTED, 
							MENU_8BIT_MAIN1, 1);
						} else {
							// determine which status bit is affected
							unsigned char mask = 1<<(p[1]-'0');
							unsigned char status = user_io_8bit_set_status(0,0);  // 0,0 gets status

							//	    iprintf("Option %s %x\n", p, status ^ mask);

							// change bit
							user_io_8bit_set_status(status ^ mask, mask);

							// ... and change it again in case of a toggle bit
							if(p[0] == 'T')
								user_io_8bit_set_status(status, mask);

							menustate = MENU_8BIT_MAIN1;
						}
					}
				}
			}
			else if (right) {
				menustate = MENU_8BIT_SYSTEM1;
				menusub = 0;
			}
			break;

		case MENU_8BIT_MAIN_FILE_SELECTED : // file successfully selected
			// this assumes that further file entries only exist if the first one also exists
			user_io_file_tx(&file, menusub+1);
			// close menu afterwards
			menustate = MENU_NONE1;
			break;

		case MENU_8BIT_MAIN_IMAGE_SELECTED :
			iprintf("Image selected: %s\n", file.name);
			user_io_file_mount(&file);
			// select image for SD card
			menustate = MENU_NONE1;
			break;

		case MENU_8BIT_SYSTEM1:
			helptext=helptexts[HELPTEXT_MAIN];
			m = 0;
			if (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2)
				m = 1;
			menumask=0x1f; // 5 selections + Exit
			OsdSetTitle("System", OSD_ARROW_LEFT); 
			menustate = MENU_8BIT_SYSTEM2;
			parentstate = MENU_8BIT_SYSTEM1;
			OsdWrite(0, "", 0, 0);
			OsdWrite(1, " Firmware & Core           \x16", menusub == 0,0);
			OsdWrite(2, " Input Devices             \x16", menusub == 1,0);
			if (m)
				OsdWrite(3, " Reset", menusub == 2,0); // reset only works for minimig
			else
				OsdWrite(3, " Save settings", menusub == 2,0); // Minimig saves settings elsewhere
			OsdWrite(4, "", 0,0);
			OsdWrite(5, " About", menusub  == 3,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 4,0);
			break;

		case MENU_8BIT_SYSTEM2 :
			m = 0;
			if (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2)
				m = 1;
			// menu key closes menu
			if (menu)
				menustate = MENU_NONE1;
			if(select) {
				switch (menusub) {
					case 0:
						// Firmware submenu
						menustate = MENU_FIRMWARE1;
						menusub = 1;
						break;
					case 1:
						// Joystick1 test
						menustate = MENU_8BIT_CONTROLLERS1;
						menusub = 0;
						break;
					case 2:
						if (m) {
								// reset for minimig
							menustate = MENU_RESET1; 
							menusub = 1;
						} else {
							// Save settings
							user_io_create_config_name(s);
							iprintf("Saving config to %s\n", s);
							if(FileNew(&file, s, 1)) {
							 // finally write data
							 sector_buffer[0] = user_io_8bit_set_status(0,0);
							 FileWrite(&file, sector_buffer); 
							 iprintf("Settings for %s written\n", s);
							}
						}
						break;
					case 3:
						// About logo
						menustate = MENU_8BIT_ABOUT1; 
						menusub = 0;
						break;
					case 4:
						// Exit
						menustate=MENU_NONE1;
						menusub = 0;
						break;
					
				}
			} else { 
				if (left) {
					// go back to core requesting this menu
					switch(user_io_core_type()) {
						case CORE_TYPE_MINIMIG:
						case CORE_TYPE_MINIMIG2:
							menusub = 1;
							menustate = MENU_MAIN2_1;
							break;
						case CORE_TYPE_MIST:
							menusub = 5;
							menustate = MENU_MIST_MAIN1;
							break;
						case CORE_TYPE_ARCHIE:
							menusub = 3;
							menustate = MENU_ARCHIE_MAIN1;
							break;
						case CORE_TYPE_8BIT:
							menusub = 0;
							menustate = MENU_8BIT_MAIN1;
							break;
					}
				}
			}
			break;
		
		case MENU_8BIT_ABOUT1:
			menumask=0;
			helptext = helptexts[HELPTEXT_NONE];
			OsdSetTitle("About", 0); 
			menustate = MENU_8BIT_ABOUT2;
			parentstate=MENU_8BIT_ABOUT1;
			OsdDrawLogo(0,0,1);
			OsdDrawLogo(1,1,1);
			OsdDrawLogo(2,2,1);
			OsdDrawLogo(3,3,1);
			OsdDrawLogo(4,4,1);
			OsdDrawLogo(6,6,1);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_EXIT, menusub==0, 0);
			StarsInit();
			ScrollReset();
			break;
			
		case MENU_8BIT_ABOUT2:
			StarsUpdate();
			OsdDrawLogo(0,0,1);
			OsdDrawLogo(1,1,1);
			OsdDrawLogo(2,2,1);
			OsdDrawLogo(3,3,1);
			OsdDrawLogo(4,4,1);
			OsdDrawLogo(6,6,1);
			ScrollText(5,"                                 MiST by Till Harbaum, based on Minimig by Dennis van Weeren and other projects. MiST hardware and software is distributed under the terms of the GNU General Public License version 3. MiST FPGA cores are the work of their respective authors under individual licensing.", 0, 0, 0);			
			// menu key closes menu
			if (menu) {
				menustate = MENU_8BIT_SYSTEM1;
				menusub = 3;
			}
			if(select) {
				//iprintf("Selected", 0);
				if (menusub==0) {
					menustate = MENU_8BIT_SYSTEM1;
					menusub = 3;
				}
			}
			else { 
				if (left)
				{
					menustate = MENU_8BIT_SYSTEM1;
					menusub = 3;
				} 
			}
			break;
		
		case MENU_8BIT_CONTROLLERS1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=0x1f;
			OsdSetTitle("Inputs", 0);
			menustate = MENU_8BIT_CONTROLLERS2;
			parentstate=MENU_8BIT_CONTROLLERS1;
			OsdWrite(0, "", 0, 0);
			OsdWrite(1, " Joystick 1 Test           \x16", menusub==0, 0);	
			OsdWrite(2, " Joystick 2 Test           \x16", menusub==1, 0);
			OsdWrite(3, " Keyboard Test             \x16", menusub==2, 0);
			OsdWrite(4, " USB status                \x16", menusub==3, 0);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_EXIT, menusub==4, 0);
			break;
		
		case MENU_8BIT_CONTROLLERS2:
			// menu key goes back to previous menu
			if (menu)
				menustate = MENU_8BIT_SYSTEM1;
			if(select) {
				switch (menusub) {
					case 0:
						// Joystick1 Test
						menustate = MENU_8BIT_JOYTEST_A1;
						menusub = 0;
						break;
					case 1:
						// Joystick2 test
						menustate = MENU_8BIT_JOYTEST_B1;
						menusub = 0;
						break;
					case 2:
						// Keyboard test
						menustate = MENU_8BIT_KEYTEST1;
						menusub = 0;
						break;
					case 3:
						// USB status
						menustate=MENU_8BIT_USB1;
						menusub = 0;
						break;
					case 4:
						// Exit to system menu
						menustate=MENU_8BIT_SYSTEM1;
						menusub = 1;
						break;
				}
			}
			break;
		
		case MENU_8BIT_KEYTEST1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=1;
			OsdSetTitle("Keyboard", 0);
			menustate = MENU_8BIT_KEYTEST2;
			parentstate=MENU_8BIT_KEYTEST1;
			OsdKeyboardPressed(keys);
			OsdWrite(0, "       USB scancodes", 0,0);
			siprintf(s, "     %2x   %2x   %2x   %2x", keys[0], keys[1], keys[2], keys[3]); // keys[4], keys[5]); - no need to show all, save some space...
			OsdWrite(2, s, 0,0);
			OsdWrite(3, "", 0, 0);
			OsdWrite(4, "       PS/2 scancodes", 0, 0);
			OsdKeyboardPressedPS2(keys_ps2);
			siprintf(s, "   %4x %4x %4x %4x", keys_ps2[0], keys_ps2[1], keys_ps2[2], keys_ps2[3]); // keys_ps2[4], keys_ps2[5]);
			OsdWrite(5, s, 0, 0);			
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_COMBO_EXIT, menusub==0, 0);
			break;
			
		case MENU_8BIT_KEYTEST2:
			OsdKeyboardPressed(keys);
			OsdWrite(0, "       USB scancodes", 0,0);
			siprintf(s, "     %2x   %2x   %2x   %2x", keys[0], keys[1], keys[2], keys[3]); // keys[4], keys[5]);
			OsdWrite(1, s, 0,0);
			m = OsdKeyboardModifiers();
			siprintbinary(usb_id, sizeof(m), &m);
			siprintf(s, "    mod keys - %s", usb_id);
			OsdWrite(2, s, 0,0);
			OsdKeyboardPressedPS2(keys_ps2);
			assign_ps2_modifier( m, keys_ps2, 0x1,  0x14);   // LCTRL
			assign_ps2_modifier( m, keys_ps2, 0x2,  0x12);   // LSHIFT
			assign_ps2_modifier( m, keys_ps2, 0x4,  0x11);   // LALT
			assign_ps2_modifier( m, keys_ps2, 0x8,  0xE01F); // LGUI
			assign_ps2_modifier( m, keys_ps2, 0x10, 0xE014); // RCTRL
			assign_ps2_modifier( m, keys_ps2, 0x20, 0x59);   // RSHIFT
			assign_ps2_modifier( m, keys_ps2, 0x40, 0xE011); // RALT
			assign_ps2_modifier( m, keys_ps2, 0x80, 0xE027); // RGUI
			siprintf(s, "   %4x %4x %4x %4x", keys_ps2[0], keys_ps2[1], keys_ps2[2], keys_ps2[3]);// keys_ps2[4], keys_ps2[5]);
			OsdWrite(5, s, 0, 0);
									
			// allow allow exit when hitting space and ESC
			for(i=0; i<6; i++) {
				if(keys[i]==0x29) { //ESC
					if(c==KEY_SPACE) {
						menustate = MENU_8BIT_CONTROLLERS1;
						menusub = 2;
					}
				}
			}
			break;
		
		case MENU_8BIT_USB1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=1;
			OsdSetTitle("USB", 0);
			menustate = MENU_8BIT_USB2;
			parentstate=MENU_8BIT_USB1;
			strcpy(usb_id, " ");
			get_joystick_id( usb_id, 0, 1);
			siprintf(s, " Joy1 - %s", usb_id);
			OsdWrite(0, "", 0, 0);
			OsdWrite(1, s, 0, 0);
			strcpy(usb_id, " ");
			get_joystick_id( usb_id, 1, 1);
			siprintf(s, " Joy2 - %s", usb_id);
			OsdWrite(2, "", 0, 0);
			OsdWrite(3, s, 0, 0);
			OsdWrite(4, "", 0, 0);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_EXIT, menusub==0, 0);
			break;
		
		case MENU_8BIT_USB2:
			menumask=1;
			OsdSetTitle("USB", 0);
			strcpy(usb_id, " ");
			get_joystick_id( usb_id, 0, 1);
			siprintf(s, " Joy1 - %s", usb_id);
			OsdWrite(0, "", 0, 0);
			OsdWrite(1, s, 0, 0);
			strcpy(usb_id, " ");
			get_joystick_id( usb_id, 1, 1);
			siprintf(s, " Joy2 - %s", usb_id);
			OsdWrite(2, "", 0, 0);
			OsdWrite(3, s, 0, 0);
			OsdWrite(7, STD_EXIT, menusub==0, 0);
			// menu key goes back to previous menu
			if (menu) {
					menustate = MENU_8BIT_CONTROLLERS1;
					menusub = 3;
			}	
			if(select) {
				if(menusub==0) {
					menustate = MENU_8BIT_CONTROLLERS1;
					menusub = 3;
				}
			}
			break;
		
		case MENU_8BIT_JOYTEST_A1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=1;
			get_joystick_id( usb_id, 0, 0);
			OsdSetTitle("Joy1", 0);
			menustate = MENU_8BIT_JOYTEST_A2;
			parentstate=MENU_8BIT_JOYTEST_A1;
			OsdWrite(0, "       Test Joystick 1", 0, 0);
			OsdWrite(1, usb_id, 0, 0);
			OsdWrite(2, "", 0, 0);
			OsdWrite(3, "", 0, 0);
			OsdWrite(4, "", 0, 0);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_SPACE_EXIT, menusub==0, 0);
			break;
		
		case MENU_8BIT_JOYTEST_A2:
			get_joystick_state( joy_string, joy_string2, 0 ); //grab state of joy 0
			get_joystick_id( usb_id, 0, 0 );
			OsdWrite(1, usb_id, 0, 0);
			OsdWrite(3, joy_string, 0, 0);
			OsdWrite(4, joy_string2, 0, 0);
			OsdWrite(5, " ", 0, 0);
			// display raw USB input
			get_joystick_state_usb ( s, 0 );
			OsdWrite(6, s, 0,0);
			// allow allow exit when hitting space
			if(c==KEY_SPACE) {
				menustate = MENU_8BIT_CONTROLLERS1;
				menusub = 0;
			}
			break;
		
		case MENU_8BIT_JOYTEST_B1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=1;
			get_joystick_id( usb_id, 1, 0);
			OsdSetTitle("Joy2", 0);
			menustate = MENU_8BIT_JOYTEST_B2;
			parentstate=MENU_8BIT_JOYTEST_B1;
			OsdWrite(0, "       Test Joystick 2", 0, 0);
			OsdWrite(1, usb_id, 0, 0);
			OsdWrite(2, "", 0, 0);
			OsdWrite(3, "", 0, 0);
			OsdWrite(4, "", 0, 0);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_SPACE_EXIT, menusub==0, 0);
			break;
			
		case MENU_8BIT_JOYTEST_B2:
			get_joystick_state( joy_string, joy_string2, 1 );
			get_joystick_id( usb_id, 1, 0);
			OsdWrite(1, usb_id, 0, 0);
			OsdWrite(3, joy_string, 0, 0);
			OsdWrite(4, joy_string2, 0, 0);
			OsdWrite(5, " ", 0, 0);
			// display raw USB input
			get_joystick_state_usb ( s, 1 );
			OsdWrite(6, s, 0,0);
			// allow allow exit when hitting space
			if(c==KEY_SPACE) {
				menustate = MENU_8BIT_CONTROLLERS1;
				menusub = 1;
			}
			break;
			
		/******************************************************************/
		/* mist main menu                                                 */
		/******************************************************************/

		case MENU_MIST_MAIN1 :
			menumask=0xff;
			OsdSetTitle("Mist", 0);

			// most important: main page has setup for floppy A: and screen
			strcpy(s, " A: ");
			strcat(s, tos_get_disk_name(0));
			if(tos_system_ctrl() & TOS_CONTROL_FDC_WR_PROT_A) strcat(s, " \x17");
			OsdWrite(0, s, menusub == 0,0);

			strcpy(s, " Screen: ");
			if(tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
			else                                          strcat(s, "Mono");
				OsdWrite(1, s, menusub == 1,0);

			/* everything else is in submenus */
			OsdWrite(2, " Storage                   \x16", menusub == 2,0);
			OsdWrite(3, " System                    \x16", menusub == 3,0);
			OsdWrite(4, " Audio / Video             \x16", menusub == 4,0);
			OsdWrite(5, " Firmware & Core           \x16", menusub == 5,0);

			OsdWrite(6, " Save config                ", menusub == 6,0);

			OsdWrite(7, STD_EXIT, menusub == 7,0);

			menustate = MENU_MIST_MAIN2;
			parentstate=MENU_MIST_MAIN1;
			break;

		case MENU_MIST_MAIN2 :
			// menu key closes menu
			if (menu)
					menustate = MENU_NONE1;
			if(select) {
				switch(menusub) {
					case 0:
						if(tos_disk_is_inserted(0)) {
							tos_insert_disk(0, NULL);
							menustate = MENU_MIST_MAIN1;
						} else
							SelectFile("ST ", SCAN_DIR | SCAN_LFN, MENU_MIST_MAIN_FILE_SELECTED, MENU_MIST_MAIN1, 0);
						break;
						
					case 1:
						tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
									menustate = MENU_MIST_MAIN1;
						break;

					case 2:  // Storage submenu
						menustate = MENU_MIST_STORAGE1;
						menusub = 0;
						break;

					case 3:  // System submenu
						menustate = MENU_MIST_SYSTEM1;
						menusub = 0;
						break;

					case 4:  // Video submenu
						menustate = MENU_MIST_VIDEO1;
						menusub = 0;
						break;

					case 5:  // Firmware submenu
						menustate = MENU_FIRMWARE1;
						menusub = 1;
						break;

					case 6:  // Save config
						menustate = MENU_NONE1;
						tos_config_save();
						break;

					case 7:  // Exit
						menustate = MENU_NONE1;
						break;
				}
			}
			break;

		case MENU_MIST_MAIN_FILE_SELECTED : // file successfully selected
			tos_insert_disk(0, &file);
			menustate = MENU_MIST_MAIN1;
			break;

		case MENU_MIST_STORAGE1 :
			menumask = tos_get_direct_hdd()?0x3f:0x7f;
			OsdSetTitle("Storage", 0);
			// entries for both floppies
			for(i=0;i<2;i++) {
				strcpy(s, " A: ");
				strcat(s, tos_get_disk_name(i));
				s[1] = 'A'+i;
				if(tos_system_ctrl() & (TOS_CONTROL_FDC_WR_PROT_A << i))
					strcat(s, " \x17");
				OsdWrite(i, s, menusub == i,0);
			}
			strcpy(s, " Write protect: ");
			strcat(s, config_tos_wrprot[(tos_system_ctrl() >> 6)&3]);
			OsdWrite(2, s, menusub == 2,0);
			OsdWrite(3, "", 0, 0);
			strcpy(s, " ACSI0 direct SD: ");
			strcat(s, tos_get_direct_hdd()?"on":"off");
			OsdWrite(4, s, menusub == 3, 0);
			for(i=0;i<2;i++) {
				strcpy(s, " ACSI0: ");
				s[5] = '0'+i;
				
				strcat(s, tos_get_disk_name(2+i));
				OsdWrite(5+i, s, ((i==1) || !tos_get_direct_hdd())?(menusub == (!tos_get_direct_hdd()?4:3)+i):0, 
					 (i==0) && tos_get_direct_hdd());
			}
			OsdWrite(7, STD_EXIT, !tos_get_direct_hdd()?(menusub == 6):(menusub == 5),0);
			parentstate = menustate;
			menustate = MENU_MIST_STORAGE2;
			break;


		case MENU_MIST_STORAGE2 :
			if (menu) {
				menustate = MENU_MIST_MAIN1;
				menusub = 2;
			}
			if(select) {
				if(menusub <= 1) {
					if(tos_disk_is_inserted(menusub)) {
						tos_insert_disk(menusub, NULL);
						menustate = MENU_MIST_STORAGE1;
					} else
						SelectFile("ST ", SCAN_DIR | SCAN_LFN, MENU_MIST_STORAGE_FILE_SELECTED, MENU_MIST_STORAGE1, 0);
				}
				else if(menusub == 2) {
					// remove current write protect bits and increase by one
					tos_update_sysctrl((tos_system_ctrl() & ~(TOS_CONTROL_FDC_WR_PROT_A | TOS_CONTROL_FDC_WR_PROT_B)) 
								 | (((((tos_system_ctrl() >> 6)&3) + 1)&3)<<6) );
					menustate = MENU_MIST_STORAGE1;

				} else if(menusub == 3) {
					tos_set_direct_hdd(!tos_get_direct_hdd());
					menustate = MENU_MIST_STORAGE1;

					// no direct hhd emulation: Both ACSI entries are enabled
					// or direct hhd emulation for ACSI0: Only second ACSI entry is enabled
				} else if((menusub == 4) || (!tos_get_direct_hdd() && (menusub == 5))) {
					char disk_idx = menusub - (tos_get_direct_hdd()?1:2);
					iprintf("Select image for disk %d\n", disk_idx);

					if(tos_disk_is_inserted(disk_idx)) {
						tos_insert_disk(disk_idx, NULL);
						menustate = MENU_MIST_STORAGE1;
					} else
						SelectFile("HD ", SCAN_LFN, MENU_MIST_STORAGE_FILE_SELECTED, MENU_MIST_STORAGE1, 0);

				} else if (tos_get_direct_hdd()?(menusub == 5):(menusub == 6)) {
					menustate = MENU_MIST_MAIN1;
					menusub = 2;
				}
			}
			break;

		case MENU_MIST_STORAGE_FILE_SELECTED : // file successfully selected
			// floppy/hdd      
			if(menusub < 2)
				tos_insert_disk(menusub, &file);
			else {
				char disk_idx = menusub - (tos_get_direct_hdd()?1:2);
				iprintf("Insert image for disk %d\n", disk_idx);
				tos_insert_disk(disk_idx, &file);
			}
			menustate = MENU_MIST_STORAGE1;
			break;

		case MENU_MIST_SYSTEM1 :
			menumask=0xff;
			OsdSetTitle("System", 0);

			strcpy(s, " Memory:    ");
			strcat(s, config_tos_mem[(tos_system_ctrl() >> 1)&7]);
						OsdWrite(0, s, menusub == 0,0);

			strcpy(s, " CPU:       ");
			strcat(s, config_cpu_msg[(tos_system_ctrl() >> 4)&3]);
						OsdWrite(1, s, menusub == 1, 0);

			strcpy(s, " TOS:       ");
			strcat(s, tos_get_image_name());
			OsdWrite(2, s, menusub == 2, 0);

			strcpy(s, " Cartridge: ");
			strcat(s, tos_get_cartridge_name());
						OsdWrite(3, s, menusub == 3, 0);

			strcpy(s, " USB I/O:   ");
			strcat(s, config_tos_usb[tos_get_cdc_control_redirect()]);
			OsdWrite(4, s, menusub == 4, 0);

			OsdWrite(5, " Reset",     menusub == 5, 0);
			OsdWrite(6, " Cold boot", menusub == 6, 0);

			OsdWrite(7, STD_EXIT, menusub == 7,0);

			parentstate = menustate;
			menustate = MENU_MIST_SYSTEM2;
			break;

		case MENU_MIST_SYSTEM2 :
			if (menu) {
				menustate = MENU_MIST_MAIN1;
				menusub = 3;
			}
			if(select) {
				switch(menusub) {
					case 0: { // RAM
						int mem = (tos_system_ctrl() >> 1)&7;   // current memory config
						mem++;
						if(mem > 5) mem = 3;                 // cycle 4MB/8MB/14MB
						tos_update_sysctrl((tos_system_ctrl() & ~0x0e) | (mem<<1) );
						tos_reset(1);
						menustate = MENU_MIST_SYSTEM1;
					} break;

					case 1: { // CPU
						int cpu = (tos_system_ctrl() >> 4)&3;   // current cpu config
						cpu = (cpu+1)&3;
						if(cpu == 2) cpu = 3;                 // skip unused config
						tos_update_sysctrl((tos_system_ctrl() & ~0x30) | (cpu<<4) );
						tos_reset(0);
						menustate = MENU_MIST_SYSTEM1;
					} break;

					case 2:  // TOS
						SelectFile("IMG", SCAN_LFN, MENU_MIST_SYSTEM_FILE_SELECTED, MENU_MIST_SYSTEM1, 0);
						break;

					case 3:  // Cart
						// if a cart name is set, then remove it
						if(tos_cartridge_is_inserted()) {
							tos_load_cartridge("");
							menustate = MENU_MIST_SYSTEM1;
						} else
							SelectFile("IMG", SCAN_LFN, MENU_MIST_SYSTEM_FILE_SELECTED, MENU_MIST_SYSTEM1, 0);
						break;

					case 4:
						if(tos_get_cdc_control_redirect() == CDC_REDIRECT_MIDI) 
							tos_set_cdc_control_redirect(CDC_REDIRECT_NONE);
						else 
							tos_set_cdc_control_redirect(tos_get_cdc_control_redirect()+1);
							menustate = MENU_MIST_SYSTEM1;
							break;

					case 5:  // Reset
						tos_reset(0);
						menustate = MENU_NONE1;
						break;

					case 6:  // Cold Boot
						tos_reset(1);
						menustate = MENU_NONE1;
						break;

					case 7:
						menustate = MENU_MIST_MAIN1;
						menusub = 3;
						break;
				}
			}
			break;
	
		case MENU_MIST_SYSTEM_FILE_SELECTED : // file successfully selected
			if(menusub == 2) {
				tos_upload(file.name);
				menustate = MENU_MIST_SYSTEM1;
			}
			if(menusub == 3) {
				tos_load_cartridge(file.name);
				menustate = MENU_MIST_SYSTEM1;
			}
			break;


		case MENU_MIST_VIDEO1 :

			menumask=0x7f;
			OsdSetTitle("A/V", 0);

			strcpy(s, " Screen:        ");
			if(tos_system_ctrl() & TOS_CONTROL_VIDEO_COLOR) strcat(s, "Color");
			else                                            strcat(s, "Mono");
			OsdWrite(0, s, menusub == 0,0);

			// Viking card can only be enabled with max 8MB RAM
			enable = (tos_system_ctrl()&0xe) <= TOS_MEMCONFIG_8M;
			strcpy(s, " Viking/SM194:  ");
			strcat(s, ((tos_system_ctrl() & TOS_CONTROL_VIKING) && enable)?"on":"off");
			OsdWrite(1, s, menusub == 1, enable?0:1);

			// Blitter is always present in >= STE
			enable = (tos_system_ctrl() & (TOS_CONTROL_STE | TOS_CONTROL_MSTE))?1:0;
			strcpy(s, " Blitter:       ");
			strcat(s, ((tos_system_ctrl() & TOS_CONTROL_BLITTER) || enable)?"on":"off");
			OsdWrite(2, s, menusub == 2, enable);

			strcpy(s, " Chipset:       ");
			// extract  TOS_CONTROL_STE and  TOS_CONTROL_MSTE bits
			strcat(s, atari_chipset[(tos_system_ctrl()>>23)&3]);
			OsdWrite(3, s, menusub == 3, 0);

			OsdWrite(4, " Video adjust              \x16", menusub == 4, 0);

			strcpy(s, " YM-Audio:      ");
			strcat(s, stereo[(tos_system_ctrl() & TOS_CONTROL_STEREO)?1:0]);
			OsdWrite(5, s, menusub == 5,0);
			OsdWrite(6, "", 0, 0);

			OsdWrite(7, STD_EXIT, menusub == 6,0);

			parentstate = menustate;
						menustate = MENU_MIST_VIDEO2;
			break;

		case MENU_MIST_VIDEO2 :
			if (menu) {
				menustate = MENU_MIST_MAIN1;
				menusub = 4;
			}

			if(select) {
				switch(menusub) {
				case 0:
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIDEO_COLOR);
					menustate = MENU_MIST_VIDEO1;
					break;
					
				case 1:
					// viking/sm194
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_VIKING);
					menustate = MENU_MIST_VIDEO1;
					break;

				case 2:
					if(!(tos_system_ctrl() & TOS_CONTROL_STE)) {
						tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_BLITTER );
						menustate = MENU_MIST_VIDEO1;
					}
					break;

				case 3: {
					unsigned long chipset = (tos_system_ctrl() >> 23)+1;
					if(chipset == 4) chipset = 0;
					tos_update_sysctrl(tos_system_ctrl() & ~(TOS_CONTROL_STE | TOS_CONTROL_MSTE) | 
								 (chipset << 23));
					menustate = MENU_MIST_VIDEO1;
				} break;
					
				case 4:
					menustate = MENU_MIST_VIDEO_ADJUST1;
					menusub = 0;
					break;

				case 5:
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_STEREO);
					menustate = MENU_MIST_VIDEO1;
					break;
					
				case 6:
					menustate = MENU_MIST_MAIN1;
					menusub = 4;
					break;
				}
			}
			break;

		case MENU_MIST_VIDEO_ADJUST1 :

			menumask=0x1f;
			OsdSetTitle("V-adjust", 0);

			OsdWrite(0, "", 0,0);

			strcpy(s, " PAL mode:    ");
			if(tos_system_ctrl() & TOS_CONTROL_PAL50HZ) strcat(s, "50Hz");
			else                                      strcat(s, "56Hz");
			OsdWrite(1, s, menusub == 0,0);

			strcpy(s, " Scanlines:   ");
			strcat(s,scanlines[(tos_system_ctrl()>>20)&3]);
			OsdWrite(2, s, menusub == 1,0);

			OsdWrite(3, "", 0,0);

						siprintf(s, " Horizontal:  %d", tos_get_video_adjust(0));
			OsdWrite(4, s, menusub == 2,0);

			siprintf(s, " Vertical:    %d", tos_get_video_adjust(1));
			OsdWrite(5, s, menusub == 3,0);

			OsdWrite(6, "", 0,0);

			OsdWrite(7, STD_EXIT, menusub == 4,0);

			parentstate = menustate;
						menustate = MENU_MIST_VIDEO_ADJUST2;
			break;

				case MENU_MIST_VIDEO_ADJUST2 :
					if (menu) {
			menustate = MENU_MIST_VIDEO1;
			menusub = 4;
					}

					// use left/right to adjust video position
					if(left || right) {
			if((menusub == 2)||(menusub == 3)) {
				if(left && (tos_get_video_adjust(menusub - 2) > -100))
					tos_set_video_adjust(menusub - 2, -1);
				
				if(right && (tos_get_video_adjust(menusub - 2) < 100))
					tos_set_video_adjust(menusub - 2, +1);
				
				menustate = MENU_MIST_VIDEO_ADJUST1;
			}
					}

					if(select) {
			switch(menusub) {
			case 0:
				tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_PAL50HZ);
				menustate = MENU_MIST_VIDEO_ADJUST1;
				break;

			case 1: {
				// next scanline state
				int scan = ((tos_system_ctrl() >> 20)+1)&3;
				tos_update_sysctrl((tos_system_ctrl() & ~TOS_CONTROL_SCANLINES) | (scan << 20));
				menustate=MENU_MIST_VIDEO_ADJUST1;
			} break;

				// entries 2 and 3 use left/right
				
			case 4:
				menustate = MENU_MIST_VIDEO1;
				menusub = 4;
				break;
			}
			}
			break;

		/******************************************************************/
		/* minimig main menu                                              */
		/******************************************************************/
		case MENU_MAIN1 :
		menumask=0x70;	// b01110000 Floppy turbo, Harddisk options & Exit.
		OsdSetTitle("Minimig",OSD_ARROW_RIGHT);
		helptext=helptexts[HELPTEXT_MAIN];

				// floppy drive info
		// We display a line for each drive that's active
		// in the config file, but grey out any that the FPGA doesn't think are active.
		// We also print a help text in place of the last drive if it's inactive.
				for (i = 0; i < 4; i++)
				{
			if(i==config.floppy.drives+1)
				OsdWrite(i," KP +/- to add/remove drives",0,1);
			else
			{
						strcpy(s, " dfx: ");
						s[3] = i + '0';
				if(i<=drives)
				{
					menumask|=(1<<i);	// Make enabled drives selectable

						if (df[i].status & DSK_INSERTED) // floppy disk is inserted
						{
								strncpy(&s[6], df[i].name, sizeof(df[0].name));
						if(!(df[i].status & DSK_WRITABLE))
									strcpy(&s[6 + sizeof(df[i].name)-1], " \x17"); // padlock icon for write-protected disks
						else
									strcpy(&s[6 + sizeof(df[i].name)-1], "  "); // clear padlock icon for write-enabled disks
						}
						else // no floppy disk
					{
								strcat(s, "* no disk *");
					}
				}
				else if(i<=config.floppy.drives)
				{
					strcat(s,"* active after reset *");
				}
				else
					strcpy(s,"");
						OsdWrite(i, s, menusub == i,(i>drives)||(i>config.floppy.drives));
			}
				}
		siprintf(s," Floppy disk turbo : %s",config.floppy.speed ? "on" : "off");
				OsdWrite(4, s, menusub==4,0);
				OsdWrite(5, " Hard disk settings \x16", menusub == 5,0);
				OsdWrite(6, "", 0,0);
				OsdWrite(7, STD_EXIT, menusub == 6,0);

				menustate = MENU_MAIN2;
		parentstate=MENU_MAIN1;
				break;

		case MENU_MAIN2 :
				if (menu)
						menustate = MENU_NONE1;
		else if(plus && (config.floppy.drives<3))
		{
			config.floppy.drives++;
			ConfigFloppy(config.floppy.drives,config.floppy.speed);
					menustate = MENU_MAIN1;
		}
		else if(minus && (config.floppy.drives>0))
		{
			config.floppy.drives--;
			ConfigFloppy(config.floppy.drives,config.floppy.speed);
					menustate = MENU_MAIN1;
		}
				else if (select)
				{
						if (menusub < 4)
						{
								if (df[menusub].status & DSK_INSERTED) // eject selected floppy
								{
										df[menusub].status = 0;
										menustate = MENU_MAIN1;
								}
								else
								{
										df[menusub].status = 0;
										SelectFile("ADF", SCAN_DIR | SCAN_LFN, MENU_FILE_SELECTED, MENU_MAIN1, 0);
								}
						}
						else if (menusub == 4)	// Toggle floppy turbo
			{
								config.floppy.speed^=1;
				ConfigFloppy(config.floppy.drives,config.floppy.speed);
								menustate = MENU_MAIN1;
			}
						else if (menusub == 5)	// Go to harddrives page.
			{
								 t_hardfile[0] = config.hardfile[0];
								 t_hardfile[1] = config.hardfile[1];
								 menustate = MENU_SETTINGS_HARDFILE1;
				 menusub=0;
			}
						else if (menusub == 6)
								menustate = MENU_NONE1;
				}
				else if (c == KEY_BACK) // eject all floppies
				{
						for (i = 0; i <= drives; i++)
								df[i].status = 0;

						menustate = MENU_MAIN1;
				}
				else if (right)
				{
						menustate = MENU_MAIN2_1;
						menusub = 0;
				}
				break;

		case MENU_FILE_SELECTED : // file successfully selected

				 InsertFloppy(&df[menusub]);
				 menustate = MENU_MAIN1;
				 menusub++;
				 if (menusub > drives)
						 menusub = 6;

				 break;

		/******************************************************************/
		/* second part of the main menu                                   */
		/******************************************************************/
		case MENU_MAIN2_1 :
			helptext=helptexts[HELPTEXT_MAIN];
			menumask=0x3f;
			OsdSetTitle("Settings",OSD_ARROW_LEFT|OSD_ARROW_RIGHT);
			OsdWrite(0, "    load configuration", menusub == 0,0);
			OsdWrite(1, "    save configuration", menusub == 1,0);
			OsdWrite(2, "", 0,0);
			OsdWrite(3, "    chipset settings \x16", menusub == 2,0);
			OsdWrite(4, "     memory settings \x16", menusub == 3,0);
			OsdWrite(5, "      video settings \x16", menusub == 4,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 5,0);
			parentstate = menustate;
			menustate = MENU_MAIN2_2;
			break;

		case MENU_MAIN2_2 :

			if (menu)
					menustate = MENU_NONE1;
			else if (select)
			{
					if (menusub == 0)
					{
							menusub = 0;
							menustate = MENU_LOADCONFIG_1;
					}
					else if (menusub == 1)
					{
							menusub = 0;
							menustate = MENU_SAVECONFIG_1;
					}
					else if (menusub == 2)
					{
							menustate = MENU_SETTINGS_CHIPSET1;
							menusub = 0;
					}
					else if (menusub == 3)
					{
							menustate = MENU_SETTINGS_MEMORY1;
							menusub = 0;
					}
					else if (menusub == 4)
					{
							menustate = MENU_SETTINGS_VIDEO1;
							menusub = 0;
					}
					else if (menusub == 5)
							menustate = MENU_NONE1;
			}
			else if (left)
			{
					menustate = MENU_MAIN1;
					menusub = 0;
			}
			else if (right)
			{
					menustate = MENU_8BIT_SYSTEM1; 
					menusub = 0;
			}
			break;
		
	
		case MENU_LOADCONFIG_1 :
			helptext=helptexts[HELPTEXT_NONE];
			if(parentstate!=menustate)	// First run?
			{
				menumask=0x20;
				SetConfigurationFilename(0); if(ConfigurationExists(0)) menumask|=0x01;
				SetConfigurationFilename(1); if(ConfigurationExists(0)) menumask|=0x02;
				SetConfigurationFilename(2); if(ConfigurationExists(0)) menumask|=0x04;
				SetConfigurationFilename(3); if(ConfigurationExists(0)) menumask|=0x08;
				SetConfigurationFilename(4); if(ConfigurationExists(0)) menumask|=0x10;
			}
			parentstate=menustate;
			OsdSetTitle("Load",0);

			OsdWrite(0, "", 0,0);
			OsdWrite(1, "          Default", menusub == 0,(menumask & 1)==0);
			OsdWrite(2, "          1", menusub == 1,(menumask & 2)==0);
			OsdWrite(3, "          2", menusub == 2,(menumask & 4)==0);
			OsdWrite(4, "          3", menusub == 3,(menumask & 8)==0);
			OsdWrite(5, "          4", menusub == 4,(menumask & 0x10)==0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 5,0);

			menustate = MENU_LOADCONFIG_2;
			break;

		case MENU_LOADCONFIG_2 :

			if (down)
			{
			//            if (menusub < 3)
					if (menusub < 5)
							menusub++;
					menustate = MENU_LOADCONFIG_1;
			}
			else if (select)
			{
				if(menusub<5)
				{
					OsdDisable();
					SetConfigurationFilename(menusub);
					LoadConfiguration(NULL);
					//				OsdReset(RESET_NORMAL);
								menustate = MENU_NONE1;
				}
				else
				{
					menustate = MENU_MAIN2_1;
					menusub = 0;
				}
			}
			if (menu) // exit menu
			{
					menustate = MENU_MAIN2_1;
					menusub = 0;
			}
			break;

		/******************************************************************/
		/* file selection menu                                            */
		/******************************************************************/
		case MENU_FILE_SELECT1 :
			helptext=helptexts[HELPTEXT_NONE];
			OsdSetTitle("Select",0);
			PrintDirectory();
			menustate = MENU_FILE_SELECT2;
			break;

		case MENU_FILE_SELECT2 :
			menumask=0;

			ScrollLongName(); // scrolls file name if longer than display line

			if (c == KEY_HOME)
			{
					ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
					menustate = MENU_FILE_SELECT1;
			}

			if (c == KEY_BACK)
			{
					if (iCurrentDirectory) // if not root directory
					{
							ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
							ChangeDirectory(DirEntry[sort_table[iSelectedEntry]].StartCluster + (fat32 ? (DirEntry[sort_table[iSelectedEntry]].HighCluster & 0x0FFF) << 16 : 0));
							if (ScanDirectory(SCAN_INIT_FIRST, fs_pFileExt, fs_Options))
									ScanDirectory(SCAN_INIT_NEXT, fs_pFileExt, fs_Options);

							menustate = MENU_FILE_SELECT1;
					}
			}

			if ((c == KEY_PGUP) || (c == KEY_LEFT))
			{
					ScanDirectory(SCAN_PREV_PAGE, fs_pFileExt, fs_Options);
					menustate = MENU_FILE_SELECT1;        }

			if ((c == KEY_PGDN) || (c == KEY_RIGHT))
			{
					ScanDirectory(SCAN_NEXT_PAGE, fs_pFileExt, fs_Options);
					menustate = MENU_FILE_SELECT1;
			}

			if (down) // scroll down one entry
			{
					ScanDirectory(SCAN_NEXT, fs_pFileExt, fs_Options);
					menustate = MENU_FILE_SELECT1;
			}

			if (up) // scroll up one entry
			{
					ScanDirectory(SCAN_PREV, fs_pFileExt, fs_Options);
					menustate = MENU_FILE_SELECT1;
			}

			if ((i = GetASCIIKey(c)))
			{ // find an entry beginning with given character
					if (nDirEntries)
					{
							if (DirEntry[sort_table[iSelectedEntry]].Attributes & ATTR_DIRECTORY)
							{ // it's a directory
									if (i < DirEntry[sort_table[iSelectedEntry]].Name[0])
									{
											if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE))
													ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR);
									}
									else if (i > DirEntry[sort_table[iSelectedEntry]].Name[0])
									{
											if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR))
													ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE);
									}
									else
									{
											if (!ScanDirectory(i, fs_pFileExt, fs_Options)) // find nexr
													if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE))
															ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR);
									}
							}
							else
							{ // it's a file
									if (i < DirEntry[sort_table[iSelectedEntry]].Name[0])
									{
											if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR))
													ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE);
									}
									else if (i > DirEntry[sort_table[iSelectedEntry]].Name[0])
									{
											if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE))
													ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR);
									}
									else
									{
											if (!ScanDirectory(i, fs_pFileExt, fs_Options)) // find next
													if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR))
															ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE);
									}
							}
					}
					menustate = MENU_FILE_SELECT1;
			}

			if (select)
			{
					if (DirEntry[sort_table[iSelectedEntry]].Attributes & ATTR_DIRECTORY)
					{
							ChangeDirectory(DirEntry[sort_table[iSelectedEntry]].StartCluster + (fat32 ? (DirEntry[sort_table[iSelectedEntry]].HighCluster & 0x0FFF) << 16 : 0));
							{
									if (strncmp((char*)DirEntry[sort_table[iSelectedEntry]].Name, "..", 2) == 0)
									{ // parent dir selected
											 if (ScanDirectory(SCAN_INIT_FIRST, fs_pFileExt, fs_Options))
													 ScanDirectory(SCAN_INIT_NEXT, fs_pFileExt, fs_Options);
											 else
													 ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);
									}
									else
											ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);

									menustate = MENU_FILE_SELECT1;
							}
					}
					else
					{
							if (nDirEntries)
							{
									file.long_name[0] = 0;
									len = strlen(DirEntryLFN[sort_table[iSelectedEntry]]);
									if (len > 4)
											if (DirEntryLFN[sort_table[iSelectedEntry]][len-4] == '.')
													len -= 4; // remove extension

									if (len > sizeof(file.long_name))
											len = sizeof(file.long_name);

									strncpy(file.name, (const char*)DirEntry[sort_table[iSelectedEntry]].Name, sizeof(file.name));
									memset(file.long_name, 0, sizeof(file.long_name));
									strncpy(file.long_name, DirEntryLFN[sort_table[iSelectedEntry]], len);
									strncpy(DiskInfo, DirEntryInfo[iSelectedEntry], sizeof(DiskInfo));

									file.size = DirEntry[sort_table[iSelectedEntry]].FileSize;
									file.attributes = DirEntry[sort_table[iSelectedEntry]].Attributes;
									file.start_cluster = DirEntry[sort_table[iSelectedEntry]].StartCluster + (fat32 ? (DirEntry[sort_table[iSelectedEntry]].HighCluster & 0x0FFF) << 16 : 0);
									file.cluster = file.start_cluster;
									file.sector = 0;

									menustate = fs_MenuSelect;
							}
					}
			}

			if (menu)
			{
					menustate = fs_MenuCancel;
			}

			break;

		/******************************************************************/
		/* reset menu                                                     */
		/******************************************************************/
		case MENU_RESET1 :
			helptext=helptexts[HELPTEXT_NONE];
			OsdSetTitle("Reset",0);
			menumask=0x03;	// Yes / No
			parentstate=menustate;

			OsdWrite(0, "", 0,0);
			OsdWrite(1, "         Reset MiST?", 0,0);
			OsdWrite(2, "", 0,0);
			OsdWrite(3, "               yes", menusub == 0,0);
			OsdWrite(4, "               no", menusub == 1,0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, "", 0,0);

			menustate = MENU_RESET2;
			break;

	case MENU_RESET2 :

			if (select && menusub == 0)
			{
					menustate = MENU_NONE1;
					OsdReset(RESET_NORMAL);
			}

			if (menu || (select && (menusub == 1))) // exit menu
			{
					menustate = MENU_8BIT_SYSTEM1;
					menusub = 0;
			}
			break;
	
		case MENU_SAVECONFIG_1 :
			helptext=helptexts[HELPTEXT_NONE];
			menumask=0x3f;
			parentstate=menustate;
			OsdSetTitle("Save",0);

			OsdWrite(0, "", 0, 0);
			OsdWrite(1, "        Default", menusub == 0,0);
			OsdWrite(2, "        1", menusub == 1,0);
			OsdWrite(3, "        2", menusub == 2,0);
			OsdWrite(4, "        3", menusub == 3,0);
			OsdWrite(5, "        4", menusub == 4,0);
			OsdWrite(6, "", 0,0);
//        OsdWrite(7, "              exit", menusub == 3);
			OsdWrite(7, STD_EXIT, menusub == 5,0);

			menustate = MENU_SAVECONFIG_2;
			break;

		case MENU_SAVECONFIG_2 :

			if (menu)
			{
					menustate = MENU_MAIN2_1;
					menusub = 5;
			}

			else if (up)
			{
					if (menusub > 0)
							menusub--;
					menustate = MENU_SAVECONFIG_1;
			}
			else if (down)
			{
//            if (menusub < 3)
					if (menusub < 5)
							menusub++;
					menustate = MENU_SAVECONFIG_1;
			}
			else if (select)
			{
				if(menusub<5)
				{
					SetConfigurationFilename(menusub);
					SaveConfiguration(NULL);
							menustate = MENU_NONE1;
				}
				else
				{
					menustate = MENU_MAIN2_1;
					menusub = 1;
				}
			}
			if (menu) // exit menu
			{
					menustate = MENU_MAIN2_1;
					menusub = 1;
			}
			break;



		/******************************************************************/
		/* chipset settings menu                                          */
		/******************************************************************/
		case MENU_SETTINGS_CHIPSET1 :
			helptext=helptexts[HELPTEXT_CHIPSET];
			menumask=0;
			OsdSetTitle("Chipset",OSD_ARROW_LEFT|OSD_ARROW_RIGHT);

			OsdWrite(0, "", 0,0);
			strcpy(s, "         CPU : ");
			strcat(s, config_cpu_msg[config.cpu & 0x03]);
			OsdWrite(1, s, menusub == 0,0);
			strcpy(s, "       Turbo : ");
			strcat(s, config_turbo_msg[(config.cpu >> 2) & 0x03]);
			OsdWrite(2, s, menusub == 1,0);
			strcpy(s, "       Video : ");
			strcat(s, config.chipset & CONFIG_NTSC ? "NTSC" : "PAL");
			OsdWrite(3, s, menusub == 2,0);
			strcpy(s, "     Chipset : ");
			strcat(s, config_chipset_msg[(config.chipset >> 2) & (minimig_v1()?3:7)]);
			OsdWrite(4, s, menusub == 3,0);
			strcpy(s, "     CD32Pad : ");
			strcat(s, config_cd32pad_msg[(config.autofire >> 2) & 1]);
			OsdWrite(5, s, menusub == 4,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 5,0);

			menustate = MENU_SETTINGS_CHIPSET2;
			break;

		case MENU_SETTINGS_CHIPSET2 :

			if (down && menusub < 5)
			{
					menusub++;
					menustate = MENU_SETTINGS_CHIPSET1;
			}

			if (up && menusub > 0)
			{
					menusub--;
					menustate = MENU_SETTINGS_CHIPSET1;
			}

			if (select)
			{
					if (menusub == 0)
					{
							menustate = MENU_SETTINGS_CHIPSET1;
							int _config_cpu = config.cpu & 0x3;
							_config_cpu += 1; 
							if (_config_cpu==0x02) _config_cpu += 1;
							config.cpu = (config.cpu & 0xfc) | (_config_cpu & 0x3);
							ConfigCPU(config.cpu);
					}
					else if (menusub == 1)
					{
							menustate = MENU_SETTINGS_CHIPSET1;
							int _config_turbo = (config.cpu >> 2) & 0x3;
							_config_turbo += 1;
							config.cpu = (config.cpu & 0x3) | ((_config_turbo & 0x3) << 2);
							ConfigCPU(config.cpu);
					}
					else if (menusub == 2)
					{
							config.chipset ^= CONFIG_NTSC;
							menustate = MENU_SETTINGS_CHIPSET1;
							ConfigChipset(config.chipset);
					}
					else if (menusub == 3)
					{
						if(minimig_v1()) 
						{
							if (config.chipset & CONFIG_ECS)
								config.chipset &= ~(CONFIG_ECS|CONFIG_A1000);
							else
								config.chipset += CONFIG_A1000;
						} 
						else
						{
							switch(config.chipset&0x1c) {
								case 0:
									config.chipset = (config.chipset&3) | CONFIG_A1000;
									break;
								case CONFIG_A1000:
									config.chipset = (config.chipset&3) | CONFIG_ECS;
									break;
								case CONFIG_ECS:
									config.chipset = (config.chipset&3) | CONFIG_AGA | CONFIG_ECS;
									break;
								case (CONFIG_AGA|CONFIG_ECS):
									config.chipset = (config.chipset&3) | 0;
									break;
								}
						}
							menustate = MENU_SETTINGS_CHIPSET1;
							ConfigChipset(config.chipset);
					}
					else if (menusub == 4)
					{
						//config.autofire = ((((config.autofire >> 2) + 1) & 1) << 2) || (config.autofire & 3);
						config.autofire  = (config.autofire + 4) & 0x7;
						menustate = MENU_SETTINGS_CHIPSET1;
						ConfigAutofire(config.autofire);
					}
					else if (menusub == 5)
					{
							menustate = MENU_MAIN2_1;
							menusub = 2;
					}
			}

			if (menu)
			{
					menustate = MENU_MAIN2_1;
					menusub = 2;
			}
			else if (right)
			{
					menustate = MENU_SETTINGS_MEMORY1;
					menusub = 0;
			}
			else if (left)
			{
					menustate = MENU_SETTINGS_VIDEO1;
					menusub = 0;
			}
			break;

		/******************************************************************/
		/* memory settings menu                                           */
		/******************************************************************/
		case MENU_SETTINGS_MEMORY1 :
			helptext=helptexts[HELPTEXT_MEMORY];
			menumask=0x3f;
			parentstate=menustate;

			OsdSetTitle("Memory",OSD_ARROW_LEFT|OSD_ARROW_RIGHT);

			OsdWrite(0, "", 0,0);
			strcpy(s, "      CHIP  : ");
			strcat(s, config_memory_chip_msg[config.memory & 0x03]);
			OsdWrite(1, s, menusub == 0,0);
			strcpy(s, "      SLOW  : ");
			strcat(s, config_memory_slow_msg[config.memory >> 2 & 0x03]);
			OsdWrite(2, s, menusub == 1,0);
			strcpy(s, "      FAST  : ");
			strcat(s, config_memory_fast_msg[config.memory >> 4 & 0x03]);
			OsdWrite(3, s, menusub == 2,0);

			OsdWrite(4, "", 0,0);

			strcpy(s, "      ROM   : ");
			if (config.kickstart.long_name[0])
					strncat(s, config.kickstart.long_name, sizeof(config.kickstart.long_name));
			else
					strncat(s, config.kickstart.name, sizeof(config.kickstart.name));
			OsdWrite(5, s, menusub == 3,0);

			strcpy(s, "      HRTmon: ");
			strcat(s, (config.memory&0x40) ? "enabled " : "disabled");
			OsdWrite(6, s, menusub == 4,0);

			OsdWrite(7, STD_EXIT, menusub == 5,0);

			menustate = MENU_SETTINGS_MEMORY2;
			break;

		case MENU_SETTINGS_MEMORY2 :
			if (select)
			{
					if (menusub == 0)
					{
							config.memory = ((config.memory + 1) & 0x03) | (config.memory & ~0x03);
							menustate = MENU_SETTINGS_MEMORY1;
							ConfigMemory(config.memory);
					}
					else if (menusub == 1)
					{
							config.memory = ((config.memory + 4) & 0x0C) | (config.memory & ~0x0C);
							menustate = MENU_SETTINGS_MEMORY1;
							ConfigMemory(config.memory);
					}
					else if (menusub == 2)
					{
							config.memory = ((config.memory + 0x10) & 0x30) | (config.memory & ~0x30);
				//                if ((config.memory & 0x30) == 0x30)
				//					config.memory -= 0x30;
				//				if (!(config.disable_ar3 & 0x01)&&(config.memory & 0x20))
				//                    config.memory &= ~0x30;
							menustate = MENU_SETTINGS_MEMORY1;
							ConfigMemory(config.memory);
					}
					else if (menusub == 3)
					{
						SelectFile("ROM", SCAN_LFN, MENU_ROMFILE_SELECTED, MENU_SETTINGS_MEMORY1, 0);
					}
					else if (menusub == 4)
					{
						config.memory ^= 0x40;
						ConfigMemory(config.memory);
						//if (!(config.disable_ar3 & 0x01)||(config.memory & 0x20))
						//  config.disable_ar3 |= 0x01;
						//else
						//  config.disable_ar3 &= 0xFE;
						menustate = MENU_SETTINGS_MEMORY1;
					}
					else if (menusub == 5)
					{
							menustate = MENU_MAIN2_1;
							menusub = 3;
					}
			}

			if (menu)
			{
					menustate = MENU_MAIN2_1;
					menusub = 3;
			}
			else if (right)
			{
					menustate = MENU_SETTINGS_VIDEO1;
					menusub = 0;
			}
			else if (left)
			{
					menustate = MENU_SETTINGS_CHIPSET1;
					menusub = 0;
			}
			break;

		/******************************************************************/
		/* hardfile settings menu                                         */
		/******************************************************************/

		// FIXME!  Nasty race condition here.  Changing HDF type has immediate effect
		// which could be disastrous if the user's writing to the drive at the time!
		// Make the menu work on the copy, not the original, and copy on acceptance,
		// not on rejection.
		case MENU_SETTINGS_HARDFILE1 :
			helptext=helptexts[HELPTEXT_HARDFILE];
			OsdSetTitle("Harddisks",0);

			parentstate = menustate;
			menumask=0x21;	// b00100001 - On/off & exit enabled by default...
			if(config.enable_ide)
				menumask|=0x0a;  // b00001010 - HD0 and HD1 type
			strcpy(s, "   A600 IDE : ");
			strcat(s, config.enable_ide ? "on " : "off");
			OsdWrite(0, s, menusub == 0,0);
			OsdWrite(1, "", 0,0);

			strcpy(s, " Master : ");
			if(config.hardfile[0].enabled==(HDF_FILE|HDF_SYNTHRDB))
				strcat(s,"Hardfile (filesys)");
			else
				strcat(s, config_hdf_msg[config.hardfile[0].enabled & HDF_TYPEMASK]);
			OsdWrite(2, s, config.enable_ide ? (menusub == 1) : 0 ,config.enable_ide==0);
			if (config.hardfile[0].present)
			{
					strcpy(s, "                                ");
					if (config.hardfile[0].long_name[0])
							strncpy(&s[14], config.hardfile[0].long_name, sizeof(config.hardfile[0].long_name));
					else
							strncpy(&s[14], config.hardfile[0].name, sizeof(config.hardfile[0].name));
			}
			else
					strcpy(s, "       ** file not found **");

			enable=config.enable_ide && ((config.hardfile[0].enabled&HDF_TYPEMASK)==HDF_FILE);
			if(enable)
				menumask|=0x04;	// Make hardfile selectable
				OsdWrite(3, s, enable ? (menusub == 2) : 0 , enable==0);

					strcpy(s, "  Slave : ");
			if(config.hardfile[1].enabled==(HDF_FILE|HDF_SYNTHRDB))
				strcat(s,"Hardfile (filesys)");
			else
				strcat(s, config_hdf_msg[config.hardfile[1].enabled & HDF_TYPEMASK]);
			OsdWrite(4, s, config.enable_ide ? (menusub == 3) : 0 ,config.enable_ide==0);
			if (config.hardfile[1].present) {
					strcpy(s, "                                ");
					if (config.hardfile[1].long_name[0])
							strncpy(&s[14], config.hardfile[1].long_name, sizeof(config.hardfile[0].long_name));
					else
							strncpy(&s[14], config.hardfile[1].name, sizeof(config.hardfile[0].name));
			}
			else
					strcpy(s, "       ** file not found **");
			enable=config.enable_ide && ((config.hardfile[1].enabled&HDF_TYPEMASK)==HDF_FILE);
			if(enable)
				menumask|=0x10;	// Make hardfile selectable
			OsdWrite(5, s, enable ? (menusub == 4) : 0 ,enable==0);

			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 5,0);

			menustate = MENU_SETTINGS_HARDFILE2;

			break;

		case MENU_SETTINGS_HARDFILE2 :
			if (select)
			{
				if (menusub == 0)
				{
					config.enable_ide=(config.enable_ide==0);
					menustate = MENU_SETTINGS_HARDFILE1;
				}
				if (menusub == 1)
				{
					if(config.hardfile[0].enabled==HDF_FILE)
					{
						config.hardfile[0].enabled|=HDF_SYNTHRDB;
					}
					else if(config.hardfile[0].enabled==(HDF_FILE|HDF_SYNTHRDB))
					{
						config.hardfile[0].enabled&=~HDF_SYNTHRDB;
						config.hardfile[0].enabled +=1;
					}
					else
					{
						config.hardfile[0].enabled +=1;
						config.hardfile[0].enabled %=HDF_CARDPART0+partitioncount;
					}
					menustate = MENU_SETTINGS_HARDFILE1;
				}
				else if (menusub == 2)
				{
					SelectFile("HDF", SCAN_LFN, MENU_HARDFILE_SELECTED, MENU_SETTINGS_HARDFILE1, 0);
				}
				else if (menusub == 3)
				{
					if(config.hardfile[1].enabled==HDF_FILE)
					{
						config.hardfile[1].enabled|=HDF_SYNTHRDB;
					}
					else if(config.hardfile[1].enabled==(HDF_FILE|HDF_SYNTHRDB))
					{
						config.hardfile[1].enabled&=~HDF_SYNTHRDB;
						config.hardfile[1].enabled +=1;
					}
					else
					{
						config.hardfile[1].enabled +=1;
						config.hardfile[1].enabled %=HDF_CARDPART0+partitioncount;
					}
					menustate = MENU_SETTINGS_HARDFILE1;
				}
				else if (menusub == 4)
				{
					SelectFile("HDF", SCAN_LFN, MENU_HARDFILE_SELECTED, MENU_SETTINGS_HARDFILE1, 0);
				}
				else if (menusub == 5) // return to previous menu
				{
						menustate = MENU_HARDFILE_EXIT;
				}
			}

			if (menu) // return to previous menu
			{
					menustate = MENU_HARDFILE_EXIT;
			}
			break;

		/******************************************************************/
		/* hardfile selected menu                                         */
		/******************************************************************/
		case MENU_HARDFILE_SELECTED :
			if (menusub == 2) // master drive selected
			{
				// Read RDB from selected drive and determine type...
				memcpy((void*)config.hardfile[0].name, (void*)file.name, sizeof(config.hardfile[0].name));
				memcpy((void*)config.hardfile[0].long_name, (void*)file.long_name, sizeof(config.hardfile[0].long_name));
				switch(GetHDFFileType(file.name))
				{
					case HDF_FILETYPE_RDB:
						config.hardfile[0].enabled=HDF_FILE;
									config.hardfile[0].present = 1;
								menustate = MENU_SETTINGS_HARDFILE1;
						break;
					case HDF_FILETYPE_DOS:
						config.hardfile[0].enabled=HDF_FILE|HDF_SYNTHRDB;
									config.hardfile[0].present = 1;
								menustate = MENU_SETTINGS_HARDFILE1;
						break;
					case HDF_FILETYPE_UNKNOWN:
									config.hardfile[0].present = 1;
						if(config.hardfile[0].enabled==HDF_FILE)	// Warn if we can't detect the type
							menustate=MENU_SYNTHRDB1;
						else
							menustate=MENU_SYNTHRDB2_1;
						menusub=0;
						break;
					case HDF_FILETYPE_NOTFOUND:
					default:
									config.hardfile[0].present = 0;
								menustate = MENU_SETTINGS_HARDFILE1;
						break;
				}
			}

			if (menusub == 4) // slave drive selected
			{
				memcpy((void*)config.hardfile[1].name, (void*)file.name, sizeof(config.hardfile[1].name));
				memcpy((void*)config.hardfile[1].long_name, (void*)file.long_name, sizeof(config.hardfile[1].long_name));
				switch(GetHDFFileType(file.name))
				{
					case HDF_FILETYPE_RDB:
						config.hardfile[1].enabled=HDF_FILE;
									config.hardfile[1].present = 1;
								menustate = MENU_SETTINGS_HARDFILE1;
						break;
					case HDF_FILETYPE_DOS:
						config.hardfile[1].enabled=HDF_FILE|HDF_SYNTHRDB;
									config.hardfile[1].present = 1;
								menustate = MENU_SETTINGS_HARDFILE1;
						break;
					case HDF_FILETYPE_UNKNOWN:
									config.hardfile[1].present = 1;
						if(config.hardfile[1].enabled==HDF_FILE)	// Warn if we can't detect the type...
							menustate=MENU_SYNTHRDB1;
						else
							menustate=MENU_SYNTHRDB2_1;
						menusub=0;
						break;
					case HDF_FILETYPE_NOTFOUND:
					default:
									config.hardfile[1].present = 0;
								menustate = MENU_SETTINGS_HARDFILE1;
						break;
				}
			}
			break;

		 // check if hardfile configuration has changed
		case MENU_HARDFILE_EXIT :

			 if (memcmp(config.hardfile, t_hardfile, sizeof(t_hardfile)) != 0)
			 {
					 menustate = MENU_HARDFILE_CHANGED1;
					 menusub = 1;
			 }
			 else
			 {
					 menustate = MENU_MAIN1;
					 menusub = 5;
			 }

			 break;

		// hardfile configuration has changed, ask user if he wants to use the new settings
		case MENU_HARDFILE_CHANGED1 :
			menumask=0x03;
			parentstate=menustate;
			OsdSetTitle("Confirm",0);

			OsdWrite(0, "", 0,0);
			OsdWrite(1, "    Changing configuration", 0,0);
			OsdWrite(2, "      requires reset.", 0,0);
			OsdWrite(3, "", 0,0);
			OsdWrite(4, "       Reset Minimig?", 0,0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "             yes", menusub == 0,0);
			OsdWrite(7, "             no", menusub == 1,0);

			menustate = MENU_HARDFILE_CHANGED2;
			break;

		case MENU_HARDFILE_CHANGED2 :
			if (select)
			{
				if (menusub == 0) // yes
				{
					// FIXME - waiting for user-confirmation increases the window of opportunity for file corruption!

					if ((config.hardfile[0].enabled != t_hardfile[0].enabled)
						|| (strncmp(config.hardfile[0].name, t_hardfile[0].name, sizeof(t_hardfile[0].name)) != 0))
					{
						OpenHardfile(0);
						//					if((config.hardfile[0].enabled == HDF_FILE) && !FindRDB(0))
						//						menustate = MENU_SYNTHRDB1;
					}
					if (config.hardfile[1].enabled != t_hardfile[1].enabled
						|| (strncmp(config.hardfile[1].name, t_hardfile[1].name, sizeof(t_hardfile[1].name)) != 0))
					{
						OpenHardfile(1);
						//					if((config.hardfile[1].enabled == HDF_FILE) && !FindRDB(1))
						//						menustate = MENU_SYNTHRDB2_1;
					}

					if(menustate==MENU_HARDFILE_CHANGED2)
					{
						ConfigIDE(config.enable_ide, config.hardfile[0].present && config.hardfile[0].enabled, config.hardfile[1].present && config.hardfile[1].enabled);
						OsdReset(RESET_NORMAL);

						menustate = MENU_NONE1;
					}
				}
				else if (menusub == 1) // no
				{
						memcpy(config.hardfile, t_hardfile, sizeof(t_hardfile)); // restore configuration
						menustate = MENU_MAIN1;
						menusub = 3;
				}
			}

			if (menu)
			{
					memcpy(config.hardfile, t_hardfile, sizeof(t_hardfile)); // restore configuration
					menustate = MENU_MAIN1;
					menusub = 3;
			}
			break;

		case MENU_SYNTHRDB1 :
			menumask=0x01;
			parentstate=menustate;
			OsdSetTitle("Warning",0);
			OsdWrite(0, "", 0,0);
			OsdWrite(1, " No partition table found -", 0,0);
			OsdWrite(2, " Hardfile image may need", 0,0);
			OsdWrite(3, " to be prepped with HDToolbox,", 0,0);
			OsdWrite(4, " then formatted.", 0,0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, "             OK", menusub == 0,0);

			menustate = MENU_SYNTHRDB2;
			break;


		case MENU_SYNTHRDB2_1 :

			menumask=0x01;
			parentstate=menustate;
			OsdSetTitle("Warning",0);
			OsdWrite(0, "", 0,0);
			OsdWrite(1, " No filesystem recognised.", 0,0);
			OsdWrite(2, " Hardfile may need formatting", 0,0);
			OsdWrite(3, " (or may simply be an", 0,0);
			OsdWrite(4, " unrecognised filesystem)", 0,0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, "             OK", menusub == 0,0);

			menustate = MENU_SYNTHRDB2;
			break;


		case MENU_SYNTHRDB2 :
			if (select || menu)
			{
					if (menusub == 0) // OK
					menustate = MENU_SETTINGS_HARDFILE1;
			}
			break;


		/******************************************************************/
		/* video settings menu                                            */
		/******************************************************************/
		case MENU_SETTINGS_VIDEO1 :
			menumask=minimig_v1()?0x0f:0x1f;
			parentstate=menustate;
			helptext=helptexts[HELPTEXT_VIDEO];
					
			OsdSetTitle("Video",OSD_ARROW_LEFT|OSD_ARROW_RIGHT);
			OsdWrite(0, "", 0,0);
			strcpy(s, "   Lores Filter : ");
			strcat(s, config_filter_msg[config.filter.lores & 0x03]);
			OsdWrite(1, s, menusub == 0,0);
			strcpy(s, "   Hires Filter : ");
			strcat(s, config_filter_msg[config.filter.hires & 0x03]);
			OsdWrite(2, s, menusub == 1,0);
			strcpy(s, "   Scanlines    : ");
			if(minimig_v1()) {
					strcat(s, config_scanlines_msg[config.scanlines % 3]);
					OsdWrite(3, s, menusub == 2,0);
					OsdWrite(4, "", 0,0);
					OsdWrite(5, "", 0,0);
					OsdWrite(6, "", 0,0);
					OsdWrite(7, STD_EXIT, menusub == 3,0);
			} else {
					strcat(s, config_scanlines_msg[(config.scanlines&0x3) % 3]);
					OsdWrite(3, s, menusub == 2,0);
					strcpy(s, "   Dither       : ");
					strcat(s, config_dither_msg[(config.scanlines>>2) & 0x03]);
					OsdWrite(4, s, menusub == 3,0);
					OsdWrite(5, "", 0,0);
					OsdWrite(6, "", 0,0);
					OsdWrite(7, STD_EXIT, menusub == 4,0);
			}
		
			menustate = MENU_SETTINGS_VIDEO2;
			break;

		case MENU_SETTINGS_VIDEO2 :
			if (select)
			{
					if (menusub == 0)
					{
						config.filter.lores++;
						config.filter.lores &= 0x03;
						menustate = MENU_SETTINGS_VIDEO1;
						MM1_ConfigFilter(config.filter.lores, config.filter.hires);
						if(minimig_v1())
							MM1_ConfigFilter(config.filter.lores, config.filter.hires);
						else
							ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
					}
					else if (menusub == 1)
					{
						config.filter.hires++;
						config.filter.hires &= 0x03;
						menustate = MENU_SETTINGS_VIDEO1;
						if(minimig_v1())
							MM1_ConfigFilter(config.filter.lores, config.filter.hires);
						else
							ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
					}
					else if (menusub == 2)
					{
						if(minimig_v1()) {
							config.scanlines++;
							if (config.scanlines > 2)
									config.scanlines = 0;
							menustate = MENU_SETTINGS_VIDEO1;
							MM1_ConfigScanlines(config.scanlines);
						} else {
							config.scanlines = ((config.scanlines + 1)&0x03) | (config.scanlines&0xfc);
							if ((config.scanlines&0x03) > 2)
								config.scanlines = config.scanlines&0xfc;
							menustate = MENU_SETTINGS_VIDEO1;
							ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
						}
					}
					else if (menusub == 3)
					{
						if(minimig_v1()) {
							menustate = MENU_MAIN2_1;
							menusub = 4;
						} else {
								config.scanlines = (config.scanlines + 4)&0x0f;
								menustate = MENU_SETTINGS_VIDEO1;
								ConfigVideo(config.filter.hires, config.filter.lores, config.scanlines);
						}
					}
					else if (menusub == 4)
					{
							menustate = MENU_MAIN2_1;
							menusub = 4;
					}
			}

			if (menu)
			{
					menustate = MENU_MAIN2_1;
					menusub = 4;
			}
			else if (right)
			{
					menustate = MENU_SETTINGS_CHIPSET1;
					menusub = 0;
			}
			else if (left)
			{
					menustate = MENU_SETTINGS_MEMORY1;
					menusub = 0;
			}
			break;

			/******************************************************************/
			/* rom file selected menu                                         */
			/******************************************************************/
			case MENU_ROMFILE_SELECTED :
			menusub = 1;
			menustate=MENU_ROMFILE_SELECTED1;
			// no break intended

		case MENU_ROMFILE_SELECTED1 :
			menumask=0x03;
			parentstate=menustate;
			OsdSetTitle("Confirm",0);
			OsdWrite(0, "", 0,0);
			OsdWrite(1, "       Reload Kickstart?", 0,0);
			OsdWrite(2, "", 0,0);
			OsdWrite(3, "              yes", menusub == 0,0);
			OsdWrite(4, "              no", menusub == 1,0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, "", 0,0);
			menustate = MENU_ROMFILE_SELECTED2;
			break;

			case MENU_ROMFILE_SELECTED2 :

				if (select)
				{
					if (menusub == 0)
					{
						memcpy((void*)config.kickstart.name, (void*)file.name, sizeof(config.kickstart.name));
						memcpy((void*)config.kickstart.long_name, (void*)file.long_name, sizeof(config.kickstart.long_name));
						if(minimig_v1()) {
								OsdDisable();
								OsdReset(RESET_BOOTLOADER);
								ConfigChipset(config.chipset | CONFIG_TURBO);
								ConfigFloppy(config.floppy.drives, CONFIG_FLOPPY2X);
								if (UploadKickstart(config.kickstart.name)) {
									BootExit();
								}
								ConfigChipset(config.chipset); // restore CPU speed mode
								ConfigFloppy(config.floppy.drives, config.floppy.speed); // restore floppy speed mode
						} 
						else {
								// reset bootscreen cursor position
								BootHome();
								OsdDisable();
								EnableOsd();
								SPI(OSD_CMD_RST);
								rstval = (SPI_RST_CPU | SPI_CPU_HLT);
								SPI(rstval);
								DisableOsd();
								SPIN(); SPIN(); SPIN(); SPIN();
								UploadKickstart(config.kickstart.name);
								EnableOsd();
								SPI(OSD_CMD_RST);
								rstval = (SPI_RST_USR | SPI_RST_CPU);
								SPI(rstval);
								DisableOsd();
								SPIN(); SPIN(); SPIN(); SPIN();
								EnableOsd();
								SPI(OSD_CMD_RST);
								rstval = 0;
								SPI(rstval);
								DisableOsd();
								SPIN(); SPIN(); SPIN(); SPIN();
						}

					menustate = MENU_NONE1;
					}
					else if (menusub == 1)
					{
							menustate = MENU_SETTINGS_MEMORY1;
							menusub = 2;
					}
				}

				if (menu)
				{
						menustate = MENU_SETTINGS_MEMORY1;
						menusub = 2;
				}
				break;

		/******************************************************************/
		/* firmware menu */
		/******************************************************************/
		case MENU_FIRMWARE1 :
			helptext=helptexts[HELPTEXT_NONE];
			parentstate=menustate;

			menumask = fat_uses_mmc()?0x07:0x03;

			OsdSetTitle("FW & Core",0);
			//OsdWrite(0, "", 0, 0);
			siprintf(s, "   ARM  s/w ver. %s", version + 5);
			OsdWrite(0, s, 0, 0);
			char *v = GetFirmwareVersion(&file, "FIRMWAREUPG");
			if(v) {
				siprintf(s, "   FILE s/w ver. %s", v);
				OsdWrite(1, s, 0, 0);
			} else
				OsdWrite(1, "", 0, 0);

			// don't allow update when running from USB
			if(fat_uses_mmc()) {
				i=1;
				OsdWrite(2, "           Update", menusub == 0, 0);
			} else {
				i=0;
				OsdWrite(2, "           Update", 0, 1);
			}
			OsdWrite(3, "", 0, 0);
			
			if(strlen(OsdCoreName())<26) {
				siprintf(s, "%*s%s", (29-strlen(OsdCoreName()))/2, " ", OsdCoreName()); 
			}
			else strcpy(s, OsdCoreName());
			s[28] = 0;

			OsdWrite(4, s, 0, 0);
			OsdWrite(5, "      Change FPGA core", menusub == i, 0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_EXIT, menusub == i+1,0);

			menustate = MENU_FIRMWARE2;
			break;

		case MENU_FIRMWARE2 :
			if (menu) {
				switch(user_io_core_type()) {
				case CORE_TYPE_MIST:
					menusub = 5;
					menustate = MENU_MIST_MAIN1;
					break;
				case CORE_TYPE_ARCHIE:
					menusub = 3;
					menustate = MENU_ARCHIE_MAIN1;
					break;
				default:
					menusub = 0;
					menustate = MENU_8BIT_SYSTEM1;
					break;
				}
			}
			else if (select) {
				if (fat_uses_mmc() && (menusub == 0)) {
					if (CheckFirmware(&file, "FIRMWAREUPG"))
						menustate = MENU_FIRMWARE_UPDATE1;
					else
						menustate = MENU_FIRMWARE_UPDATE_ERROR1;
					menusub = 1;
					OsdClear();
				}
				else if (menusub == fat_uses_mmc()?1:0) {
					SelectFile("RBF", SCAN_LFN, MENU_FIRMWARE_CORE_FILE_SELECTED, MENU_FIRMWARE1, 0);
				}
				else if (menusub == fat_uses_mmc()?2:1) {
					switch(user_io_core_type()) {
					case CORE_TYPE_MIST:
						menusub = 5;
						menustate = MENU_MIST_MAIN1;
						break;
					case CORE_TYPE_ARCHIE:
						menusub = 3;
						menustate = MENU_ARCHIE_MAIN1;
						break;
					default:
						menusub = 0;
						menustate = MENU_8BIT_SYSTEM1;
						break;
					}
				}
			}
			break;

		case MENU_FIRMWARE_CORE_FILE_SELECTED :
			// close OSD now as the new core may not even have one
			OsdDisable();

			// reset minimig boot text position
			BootHome();
			
			//remember core name loaded
			if (strlen(file.long_name)>0)
				OsdCoreNameSet(file.long_name);
			else
				OsdCoreNameSet(file.name);
			
			// reset fpga with core
			fpga_init(file.name);

			// make sure new config gets current button/dip status
			user_io_send_buttons(1);
			
			menustate = MENU_NONE1;
			break;


		/******************************************************************/
		/* firmware update message menu */
		/******************************************************************/
		case MENU_FIRMWARE_UPDATE1 :
			helptext=helptexts[HELPTEXT_NONE];
			parentstate=menustate;
			menumask=0x03;

			OsdSetTitle("Confirm",0);

			OsdWrite(0, "", 0,0);
			OsdWrite(1, "     Update the firmware", 0,0);
			OsdWrite(2, "        Are you sure?", 0 ,0);
			OsdWrite(3, "", 0,0);
			OsdWrite(4, "             yes", menusub == 0,0);
			OsdWrite(5, "             no", menusub == 1,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, "", 0,0);

			menustate = MENU_FIRMWARE_UPDATE2;

			break;

		case MENU_FIRMWARE_UPDATE2 :
			if (select)
			{
					if (menusub == 0)
					{
							menustate = MENU_FIRMWARE_UPDATING1;
							menusub = 0;
							OsdClear();
					}
					else if (menusub == 1)
					{
							menustate = MENU_FIRMWARE1;
							menusub = 2;
					}
			}
			break;

		/******************************************************************/
		/* firmware update in progress message menu*/
		/******************************************************************/
		case MENU_FIRMWARE_UPDATING1 :
			helptext=helptexts[HELPTEXT_NONE];
			parentstate=menustate;
			menumask=0x00;

			OsdSetTitle("Updating",0);

			OsdWrite(0, "", 0,0);
			OsdWrite(1, "", 0,0);
			OsdWrite(2, "      Updating firmware", 0, 0);
			OsdWrite(3, "", 0,0);
			OsdWrite(4, "         Please wait", 0, 0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, "", 0,0);
			menustate = MENU_FIRMWARE_UPDATING2;
			break;

		case MENU_FIRMWARE_UPDATING2 :

			WriteFirmware(&file, "FIRMWAREUPG");
			Error = ERROR_UPDATE_FAILED;
			menustate = MENU_FIRMWARE_UPDATE_ERROR1;
			menusub = 0;
			OsdClear();
			break;

		/******************************************************************/
		/* firmware update error message menu*/
		/******************************************************************/
		case MENU_FIRMWARE_UPDATE_ERROR1 :
			parentstate=menustate;
			OsdSetTitle("Error",0);
			OsdWrite(0, "", 0, 0);
			OsdWrite(1, "", 0, 0);

			switch (Error)
			{
			case ERROR_FILE_NOT_FOUND :
				OsdWrite(2, "       Update file", 0, 0);
					OsdWrite(3, "        not found!", 0, 0);
					break;
			case ERROR_INVALID_DATA :
				OsdWrite(2, "       Invalid ", 0, 0);
				OsdWrite(3, "     update file!", 0, 0);
					break;
			case ERROR_UPDATE_FAILED :
				OsdWrite(2, "", 0, 0);
				OsdWrite(3, "    Update failed!", 0, 0);
					break;
			}
			OsdWrite(4, "", 0, 0);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_EXIT, 1,0);
			menustate = MENU_FIRMWARE_UPDATE_ERROR2;
			break;

		case MENU_FIRMWARE_UPDATE_ERROR2 :
			if (select) {
					menustate = MENU_FIRMWARE1;
					menusub = 2;
			}
			break;

		/******************************************************************/
		/* error message menu                                             */
		/******************************************************************/
		case MENU_ERROR :
			if (menu)
					menustate = MENU_NONE1;
			break;

		/******************************************************************/
		/* popup info menu                                                */
		/******************************************************************/
		case MENU_INFO :

			if (menu)
					menustate = MENU_NONE1;
			else if (CheckTimer(menu_timer))
					menustate = MENU_NONE1;

			break;

		/******************************************************************/
		/* we should never come here                                      */
		/******************************************************************/
		default :
			break;
	}
}


void ScrollLongName(void)
{
// this function is called periodically when file selection window is displayed
// it checks if predefined period of time has elapsed and scrolls the name if necessary

    char k = sort_table[iSelectedEntry];
	static int len;
	int max_len;

    if (DirEntryLFN[k][0]) // && CheckTimer(scroll_timer)) // scroll if long name and timer delay elapsed
    {
		// FIXME - yuk, we don't want to do this every frame!
        len = strlen(DirEntryLFN[k]); // get name length

        if (len > 4)
            if (DirEntryLFN[k][len - 4] == '.')
                len -= 4; // remove extension

        max_len = 30; // number of file name characters to display (one more required for scrolling)
        if (DirEntry[k].Attributes & ATTR_DIRECTORY)
            max_len = 25; // number of directory name characters to display

		ScrollText(iSelectedEntry,DirEntryLFN[k],len,max_len,1);
    }
}


char* GetDiskInfo(char* lfn, long len)
{
// extracts disk number substring form file name
// if file name contains "X of Y" substring where X and Y are one or two digit number
// then the number substrings are extracted and put into the temporary buffer for further processing
// comparision is case sensitive

    short i, k;
    static char info[] = "XX/XX"; // temporary buffer
    static char template[4] = " of "; // template substring to search for
    char *ptr1, *ptr2, c;
    unsigned char cmp;

    if (len > 20) // scan only names which can't be fully displayed
    {
        for (i = (unsigned short)len - 1 - sizeof(template); i > 0; i--) // scan through the file name starting from its end
        {
            ptr1 = &lfn[i]; // current start position
            ptr2 = template;
            cmp = 0;
            for (k = 0; k < sizeof(template); k++) // scan through template
            {
                cmp |= *ptr1++ ^ *ptr2++; // compare substrings' characters one by one
                if (cmp)
                   break; // stop further comparing if difference already found
            }

            if (!cmp) // match found
            {
                k = i - 1; // no need to check if k is valid since i is greater than zero

                c = lfn[k]; // get the first character to the left of the matched template substring
                if (c >= '0' && c <= '9') // check if a digit
                {
                    info[1] = c; // copy to buffer
                    info[0] = ' '; // clear previous character
                    k--; // go to the preceding character
                    if (k >= 0) // check if index is valid
                    {
                        c = lfn[k];
                        if (c >= '0' && c <= '9') // check if a digit
                            info[0] = c; // copy to buffer
                    }

                    k = i + sizeof(template); // get first character to the right of the mached template substring
                    c = lfn[k]; // no need to check if index is valid
                    if (c >= '0' && c <= '9') // check if a digit
                    {
                        info[3] = c; // copy to buffer
                        info[4] = ' '; // clear next char
                        k++; // go to the followwing character
                        if (k < len) // check if index is valid
                        {
                            c = lfn[k];
                            if (c >= '0' && c <= '9') // check if a digit
                                info[4] = c; // copy to buffer
                        }
                        return info;
                    }
                }
            }
        }
    }
    return NULL;
}

// print directory contents
void PrintDirectory(void)
{
    unsigned char i;
    unsigned char k;
    unsigned long len;
    char *lfn;
    char *info;
    char *p;
    unsigned char j;

    s[32] = 0; // set temporary string length to OSD line length

	ScrollReset();

    for (i = 0; i < 8; i++)
    {
        memset(s, ' ', 32); // clear line buffer
        if (i < nDirEntries)
        {
            k = sort_table[i]; // ordered index in storage buffer
            lfn = DirEntryLFN[k]; // long file name pointer
            DirEntryInfo[i][0] = 0; // clear disk number info buffer

            if (lfn[0]) // item has long name
            {
                len = strlen(lfn); // get name length
                info = NULL; // no disk info

                if (!(DirEntry[k].Attributes & ATTR_DIRECTORY)) // if a file
                {
                if (len > 4)
                    if (lfn[len-4] == '.')
                        len -= 4; // remove extension

                info = GetDiskInfo(lfn, len); // extract disk number info

                if (info != NULL)
                   memcpy(DirEntryInfo[i], info, 5); // copy disk number info if present
                }

                if (len > 30)
                    len = 30; // trim display length if longer than 30 characters

                if (i != iSelectedEntry && info != NULL)
                { // display disk number info for not selected items
                    strncpy(s + 1, lfn, 30-6); // trimmed name
                    strncpy(s + 1+30-5, info, 5); // disk number
                }
                else
                    strncpy(s + 1, lfn, len); // display only name
            }
            else  // no LFN
            {
                strncpy(s + 1, (const char*)DirEntry[k].Name, 8); // if no LFN then display base name (8 chars)
                if (DirEntry[k].Attributes & ATTR_DIRECTORY && DirEntry[k].Extension[0] != ' ')
                {
                    p = (char*)&DirEntry[k].Name[7];
                    j = 8;
                    do
                    {
                        if (*p-- != ' ')
                            break;
                    } while (--j);

                    s[1 + j++] = '.';
                    strncpy(s + 1 + j, (const char*)DirEntry[k].Extension, 3); // if no LFN then display base name (8 chars)
                }
            }

            if (DirEntry[k].Attributes & ATTR_DIRECTORY) // mark directory with suffix
                strcpy(&s[22], " <DIR>");
        }
        else
        {
            if (i == 0 && nDirEntries == 0) // selected directory is empty
                strcpy(s, "          No files!");
        }

        OsdWrite(i, s, i == iSelectedEntry,0); // display formatted line text
    }
}

void _strncpy(char* pStr1, const char* pStr2, size_t nCount)
{
// customized strncpy() function to fill remaing destination string part with spaces

    while (*pStr2 && nCount)
    {
        *pStr1++ = *pStr2++; // copy strings
        nCount--;
    }

    while (nCount--)
        *pStr1++ = ' '; // fill remaining space with spaces
}

void inserttestfloppy() { 
  char name[] = "AUTO    ADF";
  int i;

  for(i=0;i<4;i++) {
    name[4] = '0'+i;
    
    if (FileOpen(&file, name) != 0)
      InsertFloppy(&df[i]);
  }
}

// insert floppy image pointed to to by global <file> into <drive>
void InsertFloppy(adfTYPE *drive)
{
    unsigned char i, j;
    unsigned long tracks;

    // calculate number of tracks in the ADF image file
    tracks = file.size / (512*11);
    if (tracks > MAX_TRACKS)
    {
        menu_debugf("UNSUPPORTED ADF SIZE!!! Too many tracks: %lu\r", tracks);
        tracks = MAX_TRACKS;
    }
    drive->tracks = (unsigned char)tracks;

    // fill index cache
    for (i = 0; i < tracks; i++) // for every track get its start position within image file
    {
        drive->cache[i] = file.cluster; // start of the track within image file
        for (j = 0; j < 11; j++)
            FileNextSector(&file); // advance by track length (11 sectors)
    }

    // copy image file name into drive struct
    if (file.long_name[0]) // file has long name
        _strncpy(drive->name, file.long_name, sizeof(drive->name)); // copy long name
    else
    {
        strncpy(drive->name, file.name, 8); // copy base name
        memset(&drive->name[8], ' ', sizeof(drive->name) - 8); // fill the rest of the name with spaces
    }

    if (DiskInfo[0]) // if selected file has valid disk number info then copy it to its name in drive struct
    {
        drive->name[16] = ' '; // precede disk number info with space character
        strncpy(&drive->name[17], DiskInfo, sizeof(DiskInfo)); // copy disk number info
    }

    // initialize the rest of drive struct
    drive->status = DSK_INSERTED;
    if (!(file.attributes & ATTR_READONLY)) // read-only attribute
        drive->status |= DSK_WRITABLE;

    drive->cluster_offset = drive->cache[0];
    drive->sector_offset = 0;
    drive->track = 0;
    drive->track_prev = -1;

    // some debug info
    if (file.long_name[0])
        menu_debugf("Inserting floppy: \"%s\"\r", file.long_name);
    else
        menu_debugf("Inserting floppy: \"%.11s\"\r", file.name);

    menu_debugf("file attributes: 0x%02X\r", file.attributes);
    menu_debugf("file size: %lu (%lu KB)\r", file.size, file.size >> 10);
    menu_debugf("drive tracks: %u\r", drive->tracks);
    menu_debugf("drive status: 0x%02X\r", drive->status);
}

static void set_text(const char *message, unsigned char code) {
  char i=0, l=1;

  OsdWrite(0, "", 0,0);

  do {
    s[i++] = *message;
    
    // line full or line break
    if((i == 29) || (*message == '\n') || !*message) {
	
      s[i] = 0;
      OsdWrite(l++, s, 0,0);
      i=0;  // start next line
    }
  } while(*message++);
  
  if(code && (l <= 7)) {
    siprintf(s, " Code: #%d", code);
    OsdWrite(l++, s, 0,0);
  }
  
  while(l <= 7)
    OsdWrite(l++, "", 0,0);
}

/*  Error Message */
void ErrorMessage(const char *message, unsigned char code) {
  menustate = MENU_ERROR;
  
  OsdSetTitle("Error",0);
  set_text(message, code);
  OsdEnable(0); // do not disable KEYBOARD
}

void InfoMessage(char *message) {
  if (menustate != MENU_INFO) {
    OsdSetTitle("Message",0);
    OsdEnable(0); // do not disable keyboard
  }
  
  set_text(message, 0);
  
  menu_timer = GetTimer(2000);
  menustate = MENU_INFO;
}

void EjectAllFloppies() {
  char i;
  for(i=0;i<drives;i++)
    df[i].status = 0;

  // harddisk
  config.hardfile[0].present = 0;
  config.hardfile[1].present = 0;
}
