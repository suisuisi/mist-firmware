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

#include <stdlib.h>
#include <inttypes.h>
#include <ctype.h>
#include "stdio.h"
#include "string.h"
#include "errors.h"
#include "utils.h"
#include "fat_compat.h"
#include "osd.h"
#include "state.h"
#include "fpga.h"
#include "fdd.h"
#include "hdd.h"
#include "hardware.h"
#include "firmware.h"
#include "config.h"
#include "menu.h"
#include "user_io.h"
#include "data_io.h"
#include "tos.h"
#include "cdc_control.h"
#include "debug.h"
#include "boot.h"
#include "archie.h"
#include "arc_file.h"
#include "misc_cfg.h"
#include "usb/joymapping.h"
#include "mist_cfg.h"

// test features (not used right now)
// #define ALLOW_TEST_MENU 0 //remove to disable in prod version


// other constants
#define DIRSIZE 8 // number of items in directory display window

// TODO!
#define SPIN() asm volatile ( "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0\n\t" \
                              "mov r0, r0");

static unsigned char menustate = MENU_NONE1;
static unsigned char first_displayed_8bit = 0;
static unsigned char currentpage_8bit;
static unsigned char menuidx_8bit[8];
static unsigned char selected_drive_slot;
static unsigned char parentstate;
static unsigned char menusub = 0;
static unsigned char menusub_last = 0; //for when we allocate it dynamically and need to know last row
static unsigned int menumask = 0; // Used to determine which rows are selectable...
static unsigned char first_displayed_8bit_prev; // used to returing from a submenu page
static unsigned char menusub_prev;
static unsigned long menu_timer = 0;

extern unsigned char drives;
extern adfTYPE df[4];

extern configTYPE config;
extern char s[FF_LFN_BUF + 1];

extern unsigned char fat32;

extern FILINFO  DirEntries[MAXDIRENTRIES];
extern unsigned char sort_table[MAXDIRENTRIES];
extern unsigned char nDirEntries;
extern unsigned char iSelectedEntry;
char DirEntryInfo[MAXDIRENTRIES][5]; // disk number info of dir entries
char DiskInfo[5]; // disk number info of selected entry
char *SelectedName;

extern char minimig_ver_beta;
extern char minimig_ver_major;
extern char minimig_ver_minor;
extern char minimig_ver_minion;

extern const char version[];
const char *config_tos_mem[] =  {"512 kB", "1 MB", "2 MB", "4 MB", "8 MB", "14 MB", "--", "--" };
const char *config_tos_wrprot[] =  {"none", "A:", "B:", "A: and B:"};
const char *config_tos_usb[] =  {"none", "control", "debug", "serial", "parallel", "midi"};

const char *config_filter_msg[] =  {"none", "HORIZONTAL", "VERTICAL", "H+V"};
const char *config_memory_chip_msg[] = {"0.5 MB", "1.0 MB", "1.5 MB", "2.0 MB"};
const char *config_memory_slow_msg[] = {"none  ", "0.5 MB", "1.0 MB", "1.5 MB"};
const char *config_scanlines_msg[] = {"off", "dim", "black"};
const char *config_dither_msg[] = {"off", "SPT", "RND", "S+R"};
const char *config_memory_fast_msg[] = {"none  ", "2.0 MB", "4.0 MB","8.0 MB","Maximum"};
const char *config_cpu_msg[] = {"68000 ", "68010", "-----","68020"};
const char *config_hdf_msg[] = {"Disabled", "Hardfile (disk img)", "MMC/SD card", "MMC/SD partition 1", "MMC/SD partition 2", "MMC/SD partition 3", "MMC/SD partition 4"};
const char *config_chipset_msg[] = {"OCS-A500", "OCS-A1000", "ECS", "---", "---", "---", "AGA", "---"};
const char *config_turbo_msg[] = {"none", "CHIPRAM", "KICK", "BOTH"};
char *config_autofire_msg[] = {"        AUTOFIRE OFF", "        AUTOFIRE FAST", "        AUTOFIRE MEDIUM", "        AUTOFIRE SLOW"};
const char *config_cd32pad_msg[] =  {"OFF", "ON"};
char *config_button_turbo_msg[] = {"OFF", "FAST", "MEDIUM", "SLOW"};
char *config_button_turbo_choice_msg[] = {"A only", "B only", "A & B"};
const char *config_audio_filter_msg[] = {"switchable", "always off", "always on"};
const char *config_power_led_off_msg[] = {"dim", "off"};
const char *days[] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };


enum HelpText_Message {HELPTEXT_NONE,HELPTEXT_MAIN,HELPTEXT_HARDFILE,HELPTEXT_CHIPSET,HELPTEXT_MEMORY,HELPTEXT_VIDEO,HELPTEXT_FEATURES};
const char *helptexts[]={
	0,
	"                                Welcome to MiST!  Use the cursor keys to navigate the menus.  Use space bar or enter to select an item.  Press Esc or F12 to exit the menus.  Joystick emulation on the numeric keypad can be toggled with the numlock key, while pressing Ctrl-Alt-0 (numeric keypad) toggles autofire mode.",
	"                                Minimig can emulate an A600 IDE harddisk interface.  The emulation can make use of Minimig-style hardfiles (complete disk images) or UAE-style hardfiles (filesystem images with no partition table).  It is also possible to use either the entire SD card or an individual partition as an emulated harddisk.",
	"                                Minimig's processor core can emulate a 68000 or 68020 processor (though the 68020 mode is still experimental.)  If you're running software built for 68000, there's no advantage to using the 68020 mode, since the 68000 emulation runs just as fast.",
	"                                Minimig can make use of up to 2 megabytes of Chip RAM, up to 1.5 megabytes of Slow RAM (A500 Trapdoor RAM), and up to 8 megabytes (68000/68010) / 24 megabytes (68020) of true Fast RAM.  To use the HRTmon feature you will need a file on the SD card named hrtmon.rom.",
	"                                Minimig's video features include a blur filter, to simulate the poorer picture quality on older monitors, and also scanline generation to simulate the appearance of a screen with low vertical resolution.",
	"                                Minimig can set the audio filter to switchable with power LED (A500r5+), always off or always on (A1000, A500r3). The power LED off-state can be configured to dim (A500r6+) or off (A1000, A500r3/5).",
	0
};

// one screen width
const char* HELPTEXT_SPACER= "                                ";
char helptext_custom[450]; // spacer(32) + corename(64) + minimig version(16) + helptexts[x](335)

const char* scanlines[]={"Off","25%","50%","75%"};
const char* stereo[]={"Mono","Stereo"};
const char* blend[]={"Off","On"};
const char* atari_chipset[]={"ST","STE","MegaSTE","STEroids"};

// file selection menu variables
char fs_pFileExt[13] = "xxx";
unsigned char fs_ShowExt = 0;
unsigned char fs_Options;
unsigned char fs_MenuSelect;
unsigned char fs_MenuCancel;

static char* GetExt(char *ext) {
	static char extlist[32];
	char *p = extlist;

	while(*ext) {
		strcpy(p, ",");
		strncat(p, ext, 3);
		if(strlen(ext)<=3) break;
		ext +=3;
		p += strlen(p);
	}

	return extlist+1;
}

void ResetMenu()
{
	strcpy(fs_pFileExt, "xxx");
}

static void PrintDirectory(void);
static void ScrollLongName(void);
static void InsertFloppy(adfTYPE *drive, const unsigned char *name);

static void SelectFile(char* pFileExt, unsigned char Options, unsigned char MenuSelect, unsigned char MenuCancel, char chdir)
{
	// this function displays file selection menu

	menu_debugf("%s - %s\n", pFileExt, fs_pFileExt);

	if (strncmp(pFileExt, fs_pFileExt, 12) != 0) // check desired file extension
	{ // if different from the current one go to the root directory and init entry buffer
		ChangeDirectoryName("/");

		// for 8 bit cores try to 
		if(((user_io_core_type() == CORE_TYPE_8BIT) || (user_io_core_type() == CORE_TYPE_ARCHIE)) && chdir)
			user_io_change_into_core_dir();
		ScanDirectory(SCAN_INIT, pFileExt, Options);
	}

	menu_debugf("pFileExt = %3s\n", pFileExt);
	strcpy(fs_pFileExt, pFileExt);
	fs_ShowExt = ((strlen(fs_pFileExt)>3 && strncmp(fs_pFileExt, "RBFARC", 6)) || strchr(fs_pFileExt, '*') || strchr(fs_pFileExt, '?'));
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

#define HELPTEXT_DELAY 10000
#define FRAME_DELAY 150

// prints input as a string of binary (on/off) values
// assumes big endian, returns using special characters (checked box/unchecked box)
static void siprintbinary(char* buffer, size_t const size, void const * const ptr)
{
	unsigned char *b = (unsigned char*) ptr;
	unsigned char byte;
	int i, j;
	memset(buffer, '\0', sizeof(buffer));
	for (i=size-1;i>=0;i--)
	{
		for (j=0;j<8;j++)
		{
			byte = (b[i] >> j) & 1;
			buffer[j]=byte?'\x1a':'\x19';
		}
	}
	return;
}

static void get_joystick_state( char *joy_string, char *joy_string2, uint8_t joy_num ) {
	// helper to get joystick status (both USB or DB9)
	uint16_t vjoy;
	memset(joy_string, '\0', sizeof(joy_string));
	memset(joy_string2, '\0', sizeof(joy_string2));
	vjoy = StateJoyGet(joy_num);
	vjoy |=  StateJoyGetExtra(joy_num) << 8;
	if (vjoy==0) {
		strcpy(joy_string2, "                             ");
		memset(joy_string2, ' ', 8);
		memset(joy_string2+8, '\x14', 1);
		memset(joy_string2+9, ' ', 1);
		strcat(joy_string2, "\0");
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

static void get_joystick_state_usb( char *s, unsigned char joy_num ) {
	/* USB specific - current "raw" state 
	  (in reverse binary format to correspont to MIST.INI mapping entries)
	*/
	char buffer[5];
	unsigned short i;
	char binary_string[9]="00000000";
	unsigned char joy = 0;
	unsigned int max_btn = 1;
	if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && StateNumJoysticks() <= joy_num))
	{
		strcpy( s, " ");
		return;
	}
	max_btn = StateUsbGetNumButtons(joy_num);
	joy = StateUsbJoyGet(joy_num);
	siprintf(s, "  USB: ---- 0000 0000 0000");
	siprintbinary(binary_string, sizeof(joy), &joy);
	s[7]  = binary_string[0]=='\x1a'?'>':'\x1b';
	s[8]  = binary_string[1]=='\x1a'?'<':'\x1b';
	s[9]  = binary_string[2]=='\x1a'?'\x13':'\x1b';
	s[10] = binary_string[3]=='\x1a'?'\x12':'\x1b';  
	s[12] = binary_string[4];
	s[13] = max_btn>1 ? binary_string[5] : ' ';
	s[14] = max_btn>2 ? binary_string[6] : ' ';
	s[15] = max_btn>3 ? binary_string[7] : ' ';
	joy = StateUsbJoyGetExtra(joy_num);
	siprintbinary(binary_string, sizeof(joy), &joy);
	s[17] = max_btn>4 ? binary_string[0] : ' ';
	s[18] = max_btn>5 ? binary_string[1] : ' ';
	s[19] = max_btn>6 ? binary_string[2] : ' ';
	s[20] = max_btn>7 ? binary_string[3] : ' ';
	s[22] = max_btn>8 ? binary_string[4] : ' ';
	s[23] = max_btn>9 ? binary_string[5] : ' ';
	s[24] = max_btn>10 ? binary_string[6] : ' ';
	s[25] = max_btn>11 ? binary_string[7] : ' ';	
	return;
}
			
static void append_joystick_usbid ( char *usb_id, unsigned int usb_vid, unsigned int usb_pid ) {
	siprintf(usb_id, "VID:%04X PID:%04X", usb_vid, usb_pid);
}		
		
static void get_joystick_id ( char *usb_id, unsigned char joy_num, short raw_id ) {
	/*
	Builds a string containing the USB VID/PID information of a joystick
	*/
	char buffer[32]="";

	if (raw_id==0) {
		if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && joy_num >= StateNumJoysticks()))
		{
			strcpy( usb_id, "      ");
			if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && joy_num < StateNumJoysticks()+2)) {
				strcat( usb_id, "Atari DB9 Joystick");
			} else {
				strcat( usb_id, "None");
			}
			return;
		}
	}

	//hack populate from outside
	int vid = StateUsbVidGet(joy_num);
	int pid = StateUsbPidGet(joy_num);

	memset(usb_id, '\0', sizeof(usb_id));
	if (vid>0) {
		if (raw_id == 0) {
			strcpy(buffer, get_joystick_alias( vid, pid ));
		}
		if(strlen(buffer)==0) {
			append_joystick_usbid( buffer, vid, pid );
		}
	} else {
		if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && joy_num >= StateNumJoysticks()))
		{
			if ((mist_cfg.joystick_db9_fixed_index && joy_num < 2) || (!mist_cfg.joystick_db9_fixed_index && joy_num < StateNumJoysticks()+2)) {
				strcpy( buffer, "Atari DB9 Joystick");
			} else {
				strcpy( buffer, "None");
			}
		}
	}
	if(raw_id == 0)
		siprintf(usb_id, "%*s", (28-strlen(buffer))/2, " ");
	else
		strcpy(usb_id, "");
	strcat(usb_id, buffer);
	return;
}

static unsigned char getIdx(char *opt) {
	if((opt[1]>='0') && (opt[1]<='9')) return opt[1]-'0';    // bits 0-9
	if((opt[1]>='A') && (opt[1]<='Z')) return opt[1]-'A'+10; // bits 10-35
	if((opt[1]>='a') && (opt[1]<='z')) return opt[1]-'a'+36; // bits 36-61
	return 0; // basically 0 cannot be valid because used as a reset. Thus can be used as a error.
}

static unsigned char getStatus(char *opt, unsigned long long status) {
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt+1);
	unsigned char x = (status & ((unsigned long long)1<<idx1)) ? 1 : 0;

	if(idx2>idx1) {
		x = status >> idx1;
		x = x & ~(~0 << (idx2 - idx1 + 1));
	}

	return x;
}

static unsigned long long setStatus(char *opt, unsigned long long status, unsigned char value) {
	unsigned char idx1 = getIdx(opt);
	unsigned char idx2 = getIdx(opt+1);
	unsigned long long x = 1;

	if(idx2>idx1) x = ~(~0 << (idx2 - idx1 + 1));
	x = x << idx1;

	return (status & ~x) | (((unsigned long long)value << idx1) & x);
}

static unsigned long long getStatusMask(char *opt) {
	char idx1 = getIdx(opt);
	char idx2 = getIdx(opt+1);
	unsigned long long x = 1;

	if(idx2>idx1) x = ~(~0 << (idx2 - idx1 + 1));

	//iprintf("grtStatusMask %d %d %x\n", idx1, idx2, x);

	return x << idx1;
}

static char* get_keycode_table()
{
	switch(user_io_core_type())
	{
		case CORE_TYPE_MINIMIG:
		case CORE_TYPE_MINIMIG2:
			return "Amiga";
  
		case CORE_TYPE_MIST:
		case CORE_TYPE_MIST2:
			return "  ST";

		case CORE_TYPE_ARCHIE:
			return "Archie";
	}

	return   " PS/2";
}

void HandleUI(void)
{
	char *p;
	unsigned char i, c, m, up, down, select, backsp, menu, right, left, plus, minus;
	uint8_t mod;
	unsigned long len;
	static hardfileTYPE t_hardfile[HARDFILES]; // temporary copy of former hardfile configuration
	static unsigned char t_enable_ide[2]; // temporary copy of former IDE configuration
	static unsigned char t_ide_idx;
	static unsigned char ctrl = false;
	static unsigned char lalt = false;
	char enable;
	static long helptext_timer;
	static const char *helptext;
	static char helpstate=0;
	uint8_t keys[6] = {0,0,0,0,0,0};
	uint16_t keys_ps2[6] = {0,0,0,0,0,0};
	
	mist_joystick_t joy0, joy1;
	static unsigned char joytest_num;
	
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
	backsp=false;
	
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
			// Only sent by Minimig
			if (ctrl && lalt)
			{
				if (menustate == MENU_NONE2 || menustate == MENU_INFO)
				{
					char autofire_tmp = config.autofire & 3;
					autofire_tmp++;
					config.autofire=(config.autofire & 4) | (autofire_tmp & 3);
					ConfigAutofire(config.autofire);
					if (menustate == MENU_NONE2 || menustate == MENU_INFO)
						InfoMessage(config_autofire_msg[config.autofire & 3]);
				}
			}
			break;
			
		case KEY_MENU:
			menu = true;
			OsdKeySet(KEY_MENU | KEY_UPSTROKE);
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
		case KEY_BACK :
			backsp = true;
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
			ScrollText(7,helptext,0,0,0,0);
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
				else if((user_io_core_type() == CORE_TYPE_MIST) ||
				        (user_io_core_type() == CORE_TYPE_MIST2))
					menustate = MENU_MIST_MAIN1;
				else if(user_io_core_type() == CORE_TYPE_ARCHIE)
					menustate = MENU_ARCHIE_MAIN1;
				else {
					// the "menu" core is special in jumps directly to the core selection menu
					if(!strcmp(user_io_get_core_name(), "MENU") || (user_io_get_core_features() & FEAT_MENU))
						SelectFile("RBFARC", SCAN_LFN | SCAN_SYSDIR, MENU_FIRMWARE_CORE_FILE_SELECTED, MENU_FIRMWARE1, 0);
					else {
						menustate = MENU_8BIT_MAIN1;
					}
				}
				menusub = 0;
				menusub_prev = 0;
				OsdClear();
				OsdEnable(DISABLE_KEYBOARD);
				first_displayed_8bit = 0;
				first_displayed_8bit_prev = 0;
				currentpage_8bit = 0;
			}
			break;

			/******************************************************************/
			/* archimedes main menu                                           */
			/******************************************************************/

		case MENU_ARCHIE_MAIN1: {
			menumask=0xff;
			OsdSetTitle("ARCHIE", 0);

			strcpy(s, " Floppy 0: ");
			strcat(s, archie_get_floppy_name(0));
			OsdWrite(0, s, menusub == 0, 0);

			strcpy(s, " Floppy 1: ");
			strcat(s, archie_get_floppy_name(1));
			OsdWrite(1, s, menusub == 1, 0);
			
			strcpy(s, "   OS ROM: ");
			strcat(s, archie_get_rom_name());
			OsdWrite(2, s, menusub == 2, 0);

			strcpy(s, " CMOS RAM: ");
			strcat(s, archie_get_cmos_name());
			OsdWrite(3, s, menusub == 3, 0);

			strcpy(s, " Save CMOS RAM ");
			OsdWrite(4, s, menusub == 4, 0);

			// the following is exactly like the atatri st core
			OsdWrite(5, " Firmware & Core           \x16", menusub == 5,0);
			OsdWrite(6, " Save config                ", menusub == 6,0);
			OsdWrite(7, STD_EXIT, menusub == 7,0);
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
						if(user_io_is_mounted(menusub)) {
							archie_set_floppy(menusub, NULL);
							menustate = MENU_ARCHIE_MAIN1;
						} else
							SelectFile("ADF", SCAN_DIR | SCAN_LFN, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1, 1);
						break;
						
					case 2:  // Load ROM
						SelectFile("ROM", SCAN_LFN, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1, 0);
						break;

					case 3:  // Load CMOS
						SelectFile("RAM", SCAN_LFN, MENU_ARCHIE_MAIN_FILE_SELECTED, MENU_ARCHIE_MAIN1, 0);
						break;

					case 4:  // Save CMOS
						menustate = MENU_NONE1;
						archie_save_cmos();
						break;

					case 5:  // Firmware submenu
						menustate = MENU_FIRMWARE1;
						menusub = 1;
						break;

					case 6:  // Save config
						menustate = MENU_NONE1;
						archie_save_config();
						break;

					case 7:  // Exit
						menustate = MENU_NONE1;
						break;
				}
			}
			break;

		case MENU_ARCHIE_MAIN_FILE_SELECTED : // file successfully selected
			if(menusub == 0) {
				archie_set_floppy(0, SelectedName);
			}
			if(menusub == 1) {
				archie_set_floppy(1, SelectedName);
			}
			if(menusub == 2) archie_set_rom(SelectedName);
			if(menusub == 3) archie_set_cmos(SelectedName);
			menustate = MENU_ARCHIE_MAIN1;
			break;

		/******************************************************************/
		/* 8 bit main menu                                                */
		/******************************************************************/
			
		case MENU_8BIT_MAIN1: {
			char entry=0;
			char last=0;

			menumask=0;
			p = user_io_get_core_name();
			if(!p[0]) OsdSetTitle("8BIT", OSD_ARROW_RIGHT);
			else      OsdSetTitle(p, OSD_ARROW_RIGHT);

			if(!p[0]) OsdCoreNameSet("8BIT");
			else      OsdCoreNameSet(p);

			// add options as requested by core
			i = first_displayed_8bit + 1;
			do {
				char* pos;
				unsigned long long status = user_io_8bit_set_status(0,0);  // 0,0 gets status

				p = user_io_8bit_get_string(i);
				menu_debugf("Option %d: %s\n", i, p);
				// check if there's a file type supported
				if(i == 1) {
					if (currentpage_8bit == 0 && p && strlen(p)) {
						menumask = 1;
						strcpy(s, " Load *.");
						strcat(s, GetExt(p));
						OsdWrite(entry, s, menusub==entry, 0);
						menuidx_8bit[entry] = i;
						entry++;
					} else {
						first_displayed_8bit = 1;
					}
					i++;
					p = user_io_8bit_get_string(i);
				}

				// check for 'V'ersion strings
				if(p && (p[0] == 'V')) {

					// p[1] is not used but kept for future use
					char x = p[1];

					// get version string
					strcpy(s, OsdCoreName()); // max 65
					strcat(s," ");
					substrcpy(s+strlen(s), p, 1);
					OsdCoreNameSet(s);
				}

				if(entry<7) last = i;

				// check for 'P'age
				char page = 0;
				if(entry<7 && p && (p[0] == 'P')) {
					if (p[2] == ',') {
						// 'P' is to open a submenu
						if (currentpage_8bit == 0) {
							s[0] = ' ';
							substrcpy(s+1, p, 1);
							char l = 25-strlen(s); 
							while(l--) strcat(s, " ");
							strcat(s,"\x16");
							menu_debugf("Add submenu: %s\n", s);

							OsdWrite(entry, s, menusub == entry,0);
							menumask = (menumask << 1) | 1;
							menuidx_8bit[entry] = i;
							entry++;
						}
					} else {
						// 'P' is a prefix fo F,S,O,T,R
						page = getIdx(p);
						p+=2;
						menu_debugf("P is prefix for: %s\n", p);
					}
				}

				// check for 'F'ile or 'S'D image strings
				if(currentpage_8bit == page && entry<7 && p && ((p[0] == 'F') || (p[0] == 'S'))) {
					if (entry == 0) first_displayed_8bit = i - 1;
					substrcpy(s, p, 2);
					if(strlen(s)) {
						strcpy(s, " ");
						substrcpy(s+1, p, 2);
						strcat(s, " *.");
					} else {
						if(p[0] == 'F') strcpy(s, " Load *.");
						else            strcpy(s, " Mount *.");
					}

					pos = s+strlen(s);
					substrcpy(pos, p, 1);
					strcpy(pos, GetExt(pos));
					if (p[0] == 'S' && p[1] && p[2] == 'U') {
						char slot = 0;
						if (p[1]>='0' && p[1]<='9') slot = p[1]-'0';
						if (user_io_is_mounted(slot)) {
							s[0] = '\x1e';
						}
					}
					if (p[0] == 'S' && p[1] == 'C') {
						if (user_io_is_cue_mounted())
							s[0] = '\x1f';
					}

					OsdWrite(entry, s, menusub==entry, 0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					menuidx_8bit[entry] = i;
					entry++;
				}

				// check for 'T'oggle strings
				if(currentpage_8bit == page && entry<7 && p && (p[0] == 'T')) {
					if (entry == 0) first_displayed_8bit = i - 1;

					s[0] = ' ';
					substrcpy(s+1, p, 1);
					OsdWrite(entry, s, menusub == entry,0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					menuidx_8bit[entry] = i;
					entry++;
				}

				// check for 'O'ption strings
				if(currentpage_8bit == page && entry<7 && p && (p[0] == 'O')) {
					if (entry == 0) first_displayed_8bit = i - 1;
					unsigned char x = getStatus(p, status);

					menu_debugf("Option %s %llx %llx\n", p, x, status);

					// get currently active option
					substrcpy(s, p, 2+x);
					char l = strlen(s);
					if(!l) {
						// option's index is outside of available values.
						// reset to 0.
						x = 0;
						user_io_8bit_set_status(setStatus(p, status, x), ~0);
						substrcpy(s, p, 2+x);
						l = strlen(s);
					}

					s[0] = ' ';
					substrcpy(s+1, p, 1);
					strcat(s, ":");
					l = 26-l-strlen(s); 
					while(l-- >= 0) strcat(s, " ");
					substrcpy(s+strlen(s), p, 2+x);
					OsdWrite(entry, s, menusub == entry,0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					menuidx_8bit[entry] = i;
					entry++;
				}

				// check for 'R'AM strings
				if(currentpage_8bit == page && entry<7 && p && (p[0] == 'R')) {
					if (entry == 0) first_displayed_8bit = i - 1;

					s[0] = ' ';
					substrcpy(s+1, p, 1);
					OsdWrite(entry, s, menusub == entry,0);

					// add bit in menu mask
					menumask = (menumask << 1) | 1;
					menuidx_8bit[entry] = i;
					entry++;
				}

				i++;
			} while(p);

			// exit row
			OsdWrite(7, STD_EXIT, menusub == entry, 0);
			menusub_last=entry; //remember final row
			if (entry<6) menumask = (menumask << 1) | 1;
			else {
				char i = 1;
				while (1) {
					p = user_io_8bit_get_string(last+i);
					// set exit selectable if no option to scroll down
					if (!p || !strlen(p)) {
						menumask = (menumask << 1) | 1;
						break;
					}
					if (p[0] != 'V') {
						// the next option belongs to the current page?
						if ((currentpage_8bit && p[0] == 'P' && p[2] != ',' && currentpage_8bit == getIdx(p)) ||
						    (!currentpage_8bit && (p[0] != 'P' || p[2] == ','))) break;

					}
					i++;
				}
			}

			// clear rest of OSD
			for(;entry<7;entry++) 
				OsdWrite(entry, "", 0,0);

			menustate = MENU_8BIT_MAIN2;
			parentstate=MENU_8BIT_MAIN1;

			// set helptext with core display on top of basic info
			strcpy(helptext_custom, HELPTEXT_SPACER);
			strcat(helptext_custom, OsdCoreName());
			strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
			helptext=helptext_custom;

		} break; // end MENU_8BIT_MAIN1

		case MENU_8BIT_MAIN2 :
			// menu key closes the menu or returns to the main page
			if (menu)
				if (currentpage_8bit) {
					currentpage_8bit = 0;
					menusub = menusub_prev;
					first_displayed_8bit = first_displayed_8bit_prev;
					menustate = MENU_8BIT_MAIN1;
				} else {
					menustate = MENU_NONE1;
				}
			if(select) {
				
				if (menusub==menusub_last) {
					menustate = MENU_NONE1;
				} else {
					// entry 0 = file selector
					if(!(menusub + first_displayed_8bit)) {
						p = user_io_8bit_get_string(1);

						// use a local copy of "p" since SelectFile will destroy the buffer behind it
						static char ext[13];
						strncpy(ext, p, 13);
						while(strlen(ext) < 3) strcat(ext, " ");
						SelectFile(ext, SCAN_DIR | SCAN_LFN, MENU_8BIT_MAIN_FILE_SELECTED, MENU_8BIT_MAIN1, 1);
					} else {
						p = user_io_8bit_get_string(menuidx_8bit[menusub]);

						if((p[0] == 'P') && (p[2] != ',')) p+=2;

						if((p[0] == 'F')||(p[0] == 'S')) {
							static char ext[13];
							selected_drive_slot = 0;
							if (p[1]>='0' && p[1]<='9') selected_drive_slot = p[1]-'0';
							substrcpy(ext, p, 1);
							while(strlen(ext) < 3) strcat(ext, " ");
							SelectFile(ext, SCAN_DIR | SCAN_LFN, 
							(p[0] == 'F')?MENU_8BIT_MAIN_FILE_SELECTED:(p[1] == 'C')?MENU_8BIT_CUE_FILE_SELECTED:MENU_8BIT_MAIN_IMAGE_SELECTED, 
							MENU_8BIT_MAIN1, 1);
						} else if(p[0] == 'O') {
							unsigned long long status = user_io_8bit_set_status(0,0);  // 0,0 gets status
							unsigned char x = getStatus(p, status) + 1;

							//unsigned long long mask = getStatusMask(p);
							//unsigned char x2 = x;

							// check if next value available
							substrcpy(s, p, 2+x);
							if(!strlen(s)) x = 0;

							//menu_debugf("Option %s %llx %llx %x %x\n", p, status, mask, x2, x);

							user_io_8bit_set_status(setStatus(p, status, x), ~0);

							menustate = MENU_8BIT_MAIN1;
						} else if(p[0] == 'T') {
							// determine which status bit is affected
							unsigned long long mask = 1<<getIdx(p);
							unsigned long long status = user_io_8bit_set_status(0,0);  // 0,0 gets status

							menu_debugf("Option %s %x\n", p, status ^ mask);

							// change bit
							user_io_8bit_set_status(status ^ mask, mask);

							// ... and change it again in case of a toggle bit
							user_io_8bit_set_status(status, mask);

							menustate = MENU_8BIT_MAIN1;
						} else if(p[0] == 'R') {
							menustate = MENU_8BIT_MAIN1;
							int len = strtol(p+1,0,0);
							menu_debugf("Option %s %d\n", p, len);
							if (len) {
								FIL file;

								if (!user_io_create_config_name(s, "RAM", CONFIG_ROOT)) {
									menu_debugf("Saving RAM file");
									if (f_open(&file, s, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) == FR_OK) {
										data_io_file_rx(&file, -1, len);
										f_close(&file);
									} else {
										ErrorMessage("Error saving RAM file", 0);
									}
								}
							}
						} else if(p[0] == 'P') {
							currentpage_8bit = getIdx(p);
							menusub_prev = menusub;
							menusub = 0;
							first_displayed_8bit_prev = first_displayed_8bit;
							first_displayed_8bit = 0;
							menu_debugf("Page switch to %d\n", currentpage_8bit);
							menustate = MENU_8BIT_MAIN1;
						}
					}
				}
			} else if (backsp) {
				if (menusub!=menusub_last && (menusub + first_displayed_8bit)) {
					p = user_io_8bit_get_string(menuidx_8bit[menusub]);

					if((p[0] == 'P') && (p[2] != ',')) p+=2;

					if (p[0] == 'S' && p[1] && p[2] == 'U') {
						// umount image
						char slot = 0;
						if (p[1]>='0' && p[1]<='9') slot = p[1]-'0';
						if (user_io_is_mounted(slot)) {
							user_io_file_mount(0, slot);
						}
					}

					if (p[0] == 'S' && p[1] == 'C') {
						// umount cue
						if (user_io_is_cue_mounted())
							user_io_cue_mount(NULL);
					}
				}
				menustate = MENU_8BIT_MAIN1;
			} else if (right) {
				menustate = MENU_8BIT_SYSTEM1;
				menusub = 0;
			} else if (menusub == 6 && menusub != menusub_last && down) {
				char i = 1;
				while (1) {
					p = user_io_8bit_get_string(menuidx_8bit[menusub] + i);
					if (!p || !strlen(p)) break;
					if (p[0] != 'V') {
						// the next option belongs to the current page?
						if ((currentpage_8bit && p[0] == 'P' && p[2] != ',' && currentpage_8bit == getIdx(p)) ||
						    (!currentpage_8bit && (p[0] != 'P' || p[2] == ','))) {
							first_displayed_8bit++;
							menustate = MENU_8BIT_MAIN1;
							break;
						}
					}
					i++;
				}
				menu_debugf("Next hidden option %d %d %s\n", menusub_last, first_displayed_8bit, p);
			} else if (!menusub && up) {
				while (first_displayed_8bit) {
					p = user_io_8bit_get_string(first_displayed_8bit);
					first_displayed_8bit--;
					if (!p || !strlen(p)) break; // should not happen

					if (p[0] != 'V') {
						if ((currentpage_8bit && p[0] == 'P' && p[2] != ',' && currentpage_8bit == getIdx(p)) ||
						    (!currentpage_8bit && (p[0] != 'P' || p[2] == ','))) break;
					}
				}
				menustate = MENU_8BIT_MAIN1;
			}
			break;

		case MENU_8BIT_MAIN_FILE_SELECTED : {// file successfully selected
			FIL file;
			// this assumes that further file entries only exist if the first one also exists
			if (f_open(&file, SelectedName, FA_READ) == FR_OK) {
				data_io_file_tx(&file, user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | (menusub+1), GetExtension(SelectedName));
				f_close(&file);
			}
			// close menu afterwards
			menustate = MENU_NONE1;
			break;
		}
		case MENU_8BIT_CUE_FILE_SELECTED : {
			char res;
			menustate = MENU_NONE1;
			iprintf("Cue file selected: %s\n", SelectedName);
			data_io_set_index(user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | (menusub+1));
			res = user_io_cue_mount(SelectedName);
			if (res) ErrorMessage("Error mounting CD image!", res);
			break;
		}
		case MENU_8BIT_MAIN_IMAGE_SELECTED :
			iprintf("Image selected: %s\n", SelectedName);
			data_io_set_index(user_io_ext_idx(SelectedName, fs_pFileExt)<<6 | (menusub+1));
			user_io_file_mount(SelectedName, selected_drive_slot);
			// select image for SD card
			menustate = MENU_NONE1;
			break;

		case MENU_8BIT_SYSTEM1: {
			uint8_t date[7];
			char have_rtc = GetRTC((uint8_t*)&date);
			helptext=helptexts[HELPTEXT_MAIN];
			m = 0;
			if (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2)
				m = 1;
			menumask = m ? 0x3f : 0x7f; // 5 selections + Exit
			if (!have_rtc) menumask &= ~0x02;
			OsdSetTitle("System", OSD_ARROW_LEFT);
			menustate = MENU_8BIT_SYSTEM2;
			parentstate = MENU_8BIT_SYSTEM1;
			OsdWrite(0, " Firmware & Core           \x16", menusub == 0,0);
			OsdWrite(1, " Date & Time               \x16", menusub == 1 && have_rtc, !have_rtc);
			OsdWrite(2, " Input Devices             \x16", menusub == 2,0);
			OsdWrite(3, m ? " Reset" : " Reset settings", menusub == 3,0);
			if(m)
				OsdWrite(4, "", 0,0);
			else
				OsdWrite(4, " Save settings", menusub == 4, 0); // Minimig saves settings elsewhere
			OsdWrite(5, " About", menusub == (5-m),0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_EXIT, menusub == (6-m),0);
			break;
		}
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
						// RTC submenu
						menustate = MENU_RTC1;
						menusub = 0;
						break;
					case 2:
						// Input tests and settings
						menustate = MENU_8BIT_CONTROLLERS1;
						menusub = 0;
						break;
					case 3:
						menustate = MENU_RESET1; 
						menusub = 1;
						break;
					case 4:
						if(m) {
							menustate = MENU_8BIT_ABOUT1; 
							menusub = 0;
						} else {
							// Save settings
							FIL file;
							UINT br;
							menustate = MENU_8BIT_MAIN1;
							menusub = 0;
							if (!user_io_create_config_name(s, "CFG", CONFIG_ROOT)) {;
								iprintf("Saving config to %s\n", s);
								if(f_open(&file, s, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) == FR_OK) {
									// finally write data
									((unsigned long long*)sector_buffer)[0] = user_io_8bit_set_status(0,0);
									if (f_write(&file, sector_buffer, 8, &br) == FR_OK)
										iprintf("Settings for %s written\n", s);
									else
										ErrorMessage("Error writing settings\n", 0);
									f_close(&file);
								}
							}
						}
						break;
					case 5:
						if(m) {
							menustate=MENU_NONE1;
							menusub = 0;
						} else {
							// About logo
							menustate = MENU_8BIT_ABOUT1; 
							menusub = 0;
						}
						break;
					case 6:
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
						case CORE_TYPE_MIST2:
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
			ScrollText(5,"                                 MiST by Till Harbaum, based on Minimig by Dennis van Weeren and other projects. MiST hardware and software is distributed under the terms of the GNU General Public License version 3. MiST FPGA cores are the work of their respective authors under individual licensing.", 0, 0, 0, 0);			
			// menu key closes menu
			if (menu) {
				menustate = MENU_8BIT_SYSTEM1;
				menusub = 4;
			}
			if(select) {
				//iprintf("Selected", 0);
				if (menusub==0) {
					menustate = MENU_8BIT_SYSTEM1;
					menusub = 4;
				}
			}
			else { 
				if (left)
				{
					menustate = MENU_8BIT_SYSTEM1;
					menusub = 4;
				} 
			}
			break;

		case MENU_8BIT_CONTROLLERS1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=0x7f;
			OsdSetTitle("Inputs", 0);
			menustate = MENU_8BIT_CONTROLLERS2;
			parentstate=MENU_8BIT_CONTROLLERS1;
			OsdWrite(0, " Joystick 1 Test           \x16", menusub==0, 0);
			OsdWrite(1, " Joystick 2 Test           \x16", menusub==1, 0);
			OsdWrite(2, " Joystick 3 Test           \x16", menusub==2, 0);
			OsdWrite(3, " Joystick 4 Test           \x16", menusub==3, 0);
			OsdWrite(4, " Keyboard Test             \x16", menusub==4, 0);
			OsdWrite(5, " USB status                \x16", menusub==5, 0);
			//OsdWrite(5, " CHR test                  \x16", menusub==6, 0);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_EXIT, menusub==6, 0);
			break;

		case MENU_8BIT_CONTROLLERS2:
			// menu key goes back to previous menu
			if (menu) {
				menusub = 2;
				menustate = MENU_8BIT_SYSTEM1;
			}
			if(select) {
				switch (menusub) {
					case 0:
					case 1:
					case 2:
					case 3:
						// Joystick Test
						menustate = MENU_8BIT_JOYTEST1;
						joytest_num = menusub;
						menusub = 0;
						break;
					case 4:
						// Keyboard test
						menustate = MENU_8BIT_KEYTEST1;
						menusub = 0;
						break;
					case 5:
						// USB status
						menustate=MENU_8BIT_USB1;
						menusub = 0;
						break;
					case 6:
						// Exit to system menu
						menustate=MENU_8BIT_SYSTEM1;
						menusub = 1;
						break;
					/*case 6:
						// character rom test
						menustate=MENU_8BIT_CHRTEST1;
						menusub = 0;
						break;
					*/
				}
			}
			break;
		
		case MENU_8BIT_KEYTEST1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=1;
			OsdSetTitle("Keyboard", 0);
			menustate = MENU_8BIT_KEYTEST2;
			parentstate=MENU_8BIT_KEYTEST1;
			StateKeyboardPressed(keys);
			OsdWrite(0, "       USB scancodes", 0,0);
			siprintf(s, "     %2x   %2x   %2x   %2x", keys[0], keys[1], keys[2], keys[3]); // keys[4], keys[5]); - no need to show all, save some space...
			OsdWrite(1, s, 0,0);
			mod = StateKeyboardModifiers();
			siprintbinary(usb_id, sizeof(mod), &mod);
			siprintf(s, "    mod keys - 00000000 ");
			for(i=0; i<8; i++)
				s[15+i] = usb_id[i];
			OsdWrite(2, s, 0,0);
			OsdWrite(3, "", 0, 0);
			siprintf(s, "      %s scancodes", get_keycode_table());
			OsdWrite(4, s, 0,0);
			//StateKeyboardPressedPS2(keys_ps2);
			uint16_t keys_ps2b[6]={0,0,0,0,0,0};
			siprintf(s, "   %4x %4x %4x %4x", keys_ps2b[0], keys_ps2b[1], keys_ps2b[2], keys_ps2b[3]); // keys_ps2[4], keys_ps2[5]);
			OsdWrite(5, s, 0, 0);
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_COMBO_EXIT, menusub==0, 0);
			break;
			
		case MENU_8BIT_KEYTEST2:
			StateKeyboardPressed(keys);
			OsdWrite(0, "       USB scancodes", 0,0);
			siprintf(s, "     %2x   %2x   %2x   %2x", keys[0], keys[1], keys[2], keys[3]); // keys[4], keys[5]);
			OsdWrite(1, s, 0,0);
			mod = StateKeyboardModifiers();
			strcpy(usb_id, "                      ");
			siprintbinary(usb_id, sizeof(mod), &mod);
			siprintf(s, "    mod keys - %s ", usb_id);
			/*for(i=0; i<8; i++)
				s[15+i] = usb_id[i];*/
			OsdWrite(2, s, 0,0);
			uint16_t keys_ps2[6]={0,0,0,0,0,0};
			StateKeyboardPressedPS2(keys_ps2);
			add_modifiers(mod, keys_ps2);
			siprintf(s, "   %4x %4x %4x %4x ", keys_ps2[0], keys_ps2[1], keys_ps2[2], keys_ps2[3]);// keys_ps2[4], keys_ps2[5]);
			OsdWrite(5, s, 0, 0);
			//OsdWrite(5, "", 0, 0);
			// allow allow exit when hitting space and ESC
			for(i=0; i<6; i++) {
				if(keys[i]==0x29) { //ESC
					if(c==KEY_SPACE) {
						menustate = MENU_8BIT_CONTROLLERS1;
						menusub = 4;
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
			for(i=0;i<6;i++) {
				strcpy(usb_id, " ");
				get_joystick_id( usb_id, i, 1);
				siprintf(s, " Joy%d - %s", i+1, usb_id);
				OsdWrite(i, s, 0, 0);
			}
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_EXIT, menusub==0, 0);
			break;

		case MENU_8BIT_USB2:
			menumask=1;
			OsdSetTitle("USB", 0);
			for(i=0;i<6;i++) {
				strcpy(usb_id, " ");
				get_joystick_id( usb_id, i, 1);
				siprintf(s, " Joy%d - %s", i+1, usb_id);
				OsdWrite(i, s, 0, 0);
			}
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_EXIT, menusub==0, 0);
			// menu key goes back to previous menu
			if (menu) {
					menustate = MENU_8BIT_CONTROLLERS1;
					menusub = 5;
			}	
			if(select) {
				if(menusub==0) {
					menustate = MENU_8BIT_CONTROLLERS1;
					menusub = 5;
				}
			}
			break;

		case MENU_8BIT_JOYTEST1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=1;
			get_joystick_id( usb_id, joytest_num, 0);
			siprintf(s, "Joy%d", joytest_num + 1);
			OsdSetTitle(s, 0);
			menustate = MENU_8BIT_JOYTEST2;
			parentstate=MENU_8BIT_JOYTEST1;
			siprintf(s, "       Test Joystick %d", joytest_num + 1);
			OsdWrite(0, s, 0, 0);
			OsdWrite(1, usb_id, 0, 0);
			OsdWrite(2, "", 0, 0);
			OsdWrite(3, "", 0, 0);
			OsdWrite(4, "", 0, 0);
			OsdWrite(5, "", 0, 0);
			OsdWrite(6, " ", 0, 0);
			OsdWrite(7, STD_SPACE_EXIT, menusub==0, 0);
			break;

		case MENU_8BIT_JOYTEST2:
			get_joystick_state( joy_string, joy_string2, joytest_num ); //grab state of joy
			get_joystick_id( usb_id, joytest_num, 0 );
			OsdWrite(1, usb_id, 0, 0);
			OsdWrite(3, joy_string, 0, 0);
			OsdWrite(4, joy_string2, 0, 0);
			OsdWrite(5, " ", 0, 0);
			// display raw USB input
			get_joystick_state_usb ( s, joytest_num );
			OsdWrite(6, s, 0,0);
			// allow exit when hitting space
			if(c==KEY_SPACE) {
				menustate = MENU_8BIT_CONTROLLERS1;
				menusub = joytest_num;
			}
			break;

		case MENU_8BIT_CHRTEST1:
			helptext = helptexts[HELPTEXT_NONE];
			menumask=0;
			OsdSetTitle("CHR", 0);
			menustate = MENU_8BIT_CHRTEST2;
			parentstate=MENU_8BIT_CHRTEST1;
			strcpy(usb_id, "                          ");
			for(i=1; i<24; i++) {
				if(i<4 || i>13)
					usb_id[i] = i;
				else
					usb_id[i] = ' ';
			}
			OsdWrite(0, usb_id, 0, 0);
			for(i=0; i<24; i++) usb_id[i] = i+24;
			OsdWrite(1, usb_id, 0, 0);
			for(i=0; i<24; i++) usb_id[i] = i+(24*2);
			OsdWrite(2, usb_id, 0, 0);	
			for(i=0; i<24; i++) usb_id[i] = i+(24*3);
			OsdWrite(3, usb_id, 0, 0);
			for(i=0; i<24; i++) usb_id[i] = i+(24*4);
			OsdWrite(4, usb_id, 0, 0);
			strcpy(usb_id, "                          ");
			for(i=0; i<8; i++) usb_id[i] = i+(24*5);
			OsdWrite(5, usb_id, 0, 0);
			//for(i=0; i<24; i++) usb_id[i] = i+(24*6);
			OsdWrite(6, "", 0, 0);
			OsdWrite(7, STD_SPACE_EXIT, menusub==0, 0);
			break;
			
		case MENU_8BIT_CHRTEST2:
			
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

			// most important: main page has setup for floppy A:
			strcpy(s, " A: ");
			strcat(s, tos_get_disk_name(0));
			if(tos_system_ctrl() & TOS_CONTROL_FDC_WR_PROT_A) strcat(s, " \x17");
			OsdWrite(0, s, menusub == 0,0);

			/* everything else is in submenus */
			OsdWrite(1, " Storage                   \x16", menusub == 1,0);
			OsdWrite(2, " System                    \x16", menusub == 2,0);
			OsdWrite(3, " Audio / Video             \x16", menusub == 3,0);
			OsdWrite(4, " Firmware & Core           \x16", menusub == 4,0);

			OsdWrite(5, " Load config               \x16", menusub == 5,0);
			OsdWrite(6, " Save config               \x16", menusub == 6,0);

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

					case 1:  // Storage submenu
						menustate = MENU_MIST_STORAGE1;
						menusub = 0;
						break;

					case 2:  // System submenu
						menustate = MENU_MIST_SYSTEM1;
						menusub = 0;
						break;

					case 3:  // Video submenu
						menustate = MENU_MIST_VIDEO1;
						menusub = 0;
						break;

					case 4:  // Firmware submenu
						menustate = MENU_FIRMWARE1;
						menusub = 1;
						break;

					case 5:  // Load config
						menustate = MENU_MIST_LOAD_CONFIG1;
						menusub = 0;
						break;

					case 6:  // Save config
						menustate = MENU_MIST_SAVE_CONFIG1;
						menusub = 0;
						break;

					case 7:  // Exit
						menustate = MENU_NONE1;
						break;
				}
			}
			break;

		case MENU_MIST_MAIN_FILE_SELECTED : // file successfully selected
			tos_insert_disk(0, SelectedName);
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
				menusub = 1;
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
					menusub = 1;
				}
			}
			break;

		case MENU_MIST_STORAGE_FILE_SELECTED : // file successfully selected
			// floppy/hdd
			if(menusub < 2) {
				iprintf("Insert image %s for disk %d\n", SelectedName, menusub);
				tos_insert_disk(menusub, SelectedName);
			} else {
				char disk_idx = menusub - (tos_get_direct_hdd()?1:2);
				iprintf("Insert image for disk %d\n", disk_idx);
				tos_insert_disk(disk_idx, SelectedName);
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
				menusub = 2;
			}
			if(select) {
				switch(menusub) {
					case 0: { // RAM
						int mem = (tos_system_ctrl() >> 1)&7;   // current memory config
						mem++;
						if(mem > 5) mem = 0;
						tos_update_sysctrl((tos_system_ctrl() & ~0x0e) | (mem<<1) );
						tos_reset(1);
						menustate = MENU_MIST_SYSTEM1;
					} break;

					case 1: { // CPU
						int cpu = (tos_system_ctrl() >> 4)&3;   // current cpu config
						cpu = (cpu+1)&3;
						if(cpu == 2 || (user_io_core_type() == CORE_TYPE_MIST2 && cpu == 1)) cpu = 3; // skip unused config
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
						menusub = 2;
						break;
				}
			}
			break;
	
		case MENU_MIST_SYSTEM_FILE_SELECTED : // file successfully selected
			if(menusub == 2) {
				tos_upload(SelectedName);
				menustate = MENU_MIST_SYSTEM1;
			}
			if(menusub == 3) {
				tos_load_cartridge(SelectedName);
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

			if(user_io_core_type() == CORE_TYPE_MIST) {
				OsdWrite(4, " Video adjust              \x16", menusub == 4, 0);
			} else {
				strcpy(s, " Scanlines:     ");
				strcat(s,scanlines[(tos_system_ctrl()>>20)&3]);
				OsdWrite(4, s, menusub == 4, 0);
			}

			strcpy(s, " YM-Audio:      ");
			strcat(s, stereo[(tos_system_ctrl() & TOS_CONTROL_STEREO)?1:0]);
			OsdWrite(5, s, menusub == 5,0);
			if(user_io_core_type() == CORE_TYPE_MIST) {
				OsdWrite(6, "", 0, 0);
				OsdWrite(7, STD_EXIT, menusub == 6,0);
			} else {
				strcpy(s, " Comp. blend:   ");
				strcat(s, blend[(tos_system_ctrl() & TOS_CONTROL_BLEND)?1:0]);
				OsdWrite(6, s, menusub == 6,0);
				OsdWrite(7, STD_EXIT, menusub == 7,0);
				menumask |= 0x80;
			}

			parentstate = menustate;
			menustate = MENU_MIST_VIDEO2;
			break;

		case MENU_MIST_VIDEO2 :
			if (menu) {
				menustate = MENU_MIST_MAIN1;
				menusub = 3;
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
					}
					break;

				case 4: if(user_io_core_type() == CORE_TYPE_MIST) {
						menustate = MENU_MIST_VIDEO_ADJUST1;
						menusub = 0;
					} else {
						// next scanline state
						int scan = ((tos_system_ctrl() >> 20)+1)&3;
						tos_update_sysctrl((tos_system_ctrl() & ~TOS_CONTROL_SCANLINES) | (scan << 20));
						menustate = MENU_MIST_VIDEO1;
					}
					break;

				case 5:
					tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_STEREO);
					menustate = MENU_MIST_VIDEO1;
					break;
					
				case 6:
					if(user_io_core_type() == CORE_TYPE_MIST) {
						menustate = MENU_MIST_MAIN1;
						menusub = 3;
					} else {
						tos_update_sysctrl(tos_system_ctrl() ^ TOS_CONTROL_BLEND);
						menustate = MENU_MIST_VIDEO1;
					}
					break;
				case 7:
					menustate = MENU_MIST_MAIN1;
					menusub = 3;
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
			else                                        strcat(s, "56Hz");
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

		case MENU_MIST_LOAD_CONFIG1 :
			helptext=helptexts[HELPTEXT_NONE];
			if(parentstate!=menustate)	// First run?
			{
				menumask=0x20;
				if(tos_config_exists(0)) menumask|=0x01;
				if(tos_config_exists(1)) menumask|=0x02;
				if(tos_config_exists(2)) menumask|=0x04;
				if(tos_config_exists(3)) menumask|=0x08;
				if(tos_config_exists(4)) menumask|=0x10;
			}
			parentstate=menustate;
			parentstate=menustate;
			OsdSetTitle("Load",0);

			OsdWrite(0, "", 0, 0);
			for(int i = 0; i < 5; i++) {
				strcpy(s,"          ");
				strcat(s, atarist_cfg.conf_name[i]);
				OsdWrite(i+1, s, menusub == i, !(menumask & (1<<i)));
			}
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 5,0);

			menustate = MENU_MIST_LOAD_CONFIG2;
			break;

		case MENU_MIST_LOAD_CONFIG2 :

			if (menu)
			{
				menustate = MENU_MIST_MAIN1;
				menusub = 5;
			}

			if (select)
			{
				if(menusub<5)
				{
					tos_insert_disk(2, NULL);
					tos_insert_disk(3, NULL);
					tos_config_load(menusub);
					tos_upload(NULL);
					menustate = MENU_NONE1;
				}
				else
				{
					menustate = MENU_MIST_MAIN1;
					menusub = 5;
				}
			}
			break;

		case MENU_MIST_SAVE_CONFIG1 :
			helptext=helptexts[HELPTEXT_NONE];
			menumask=0x3f;
			parentstate=menustate;
			OsdSetTitle("Save",0);

			OsdWrite(0, "", 0, 0);
			for(int i = 0; i < 5; i++) {
				strcpy(s,"          ");
				strcat(s, atarist_cfg.conf_name[i]);
				OsdWrite(i+1, s, menusub == i, 0);
			}
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 5,0);

			menustate = MENU_MIST_SAVE_CONFIG2;
			break;

		case MENU_MIST_SAVE_CONFIG2 :

			if (menu)
			{
				menustate = MENU_MIST_MAIN1;
				menusub = 6;
			}

			if (select)
			{
				if(menusub<5)
				{
					tos_config_save(menusub);
					menustate = MENU_NONE1;
				}
				else
				{
					menustate = MENU_MIST_MAIN1;
					menusub = 6;
				}
			}
			break;


		/******************************************************************/
		/* minimig main menu                                              */
		/******************************************************************/
		case MENU_MAIN1 :
			menumask=0xF0;	// b11110000 Floppy turbo, Harddisk options & Exit.
			OsdSetTitle("Minimig",OSD_ARROW_RIGHT);
			// set helptext with core display on top of basic info
			strcpy(helptext_custom, HELPTEXT_SPACER);
			strcat(helptext_custom, OsdCoreName());
			siprintf(s, "%s v%d.%d.%d", minimig_ver_beta ? " BETA" : "", minimig_ver_major, minimig_ver_minor, minimig_ver_minion);
			strcat(helptext_custom, s);
			strcat(helptext_custom, helptexts[HELPTEXT_MAIN]);
			helptext=helptext_custom;

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
			OsdWrite(5, " Primary hard disks \x16", menusub == 5,0);
			OsdWrite(6, " Secondary hard disks \x16", menusub == 6,0);
			OsdWrite(7, STD_EXIT, menusub == 7,0);

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
				else if (menusub == 5)	// Go to primary harddrives page.
				{
					memcpy(t_hardfile, config.hardfile, sizeof(config.hardfile));
					t_enable_ide[0] = config.enable_ide[0];
					t_enable_ide[1] = config.enable_ide[1];
					t_ide_idx = 0;
					menustate = MENU_SETTINGS_HARDFILE1;
					menusub=0;
				}
				else if (menusub == 6)	// Go to primary harddrives page.
				{
					memcpy(t_hardfile, config.hardfile, sizeof(config.hardfile));
					t_enable_ide[0] = config.enable_ide[0];
					t_enable_ide[1] = config.enable_ide[1];
					t_ide_idx = 1;
					menustate = MENU_SETTINGS_HARDFILE1;
					menusub=0;
				}
				else if (menusub == 7)
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

			InsertFloppy(&df[menusub], SelectedName);
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
			menumask=0x7f;
			OsdSetTitle("Settings",OSD_ARROW_LEFT|OSD_ARROW_RIGHT);
			OsdWrite(0, "    load configuration", menusub == 0,0);
			OsdWrite(1, "    save configuration", menusub == 1,0);
			OsdWrite(2, "", 0,0);
			OsdWrite(3, "    chipset settings \x16", menusub == 2,0);
			OsdWrite(4, "     memory settings \x16", menusub == 3,0);
			OsdWrite(5, "      video settings \x16", menusub == 4,0);
			OsdWrite(6, "   features settings \x16", menusub == 5,0);
			OsdWrite(7, STD_EXIT, menusub == 6,0);
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
				{
					menustate = MENU_SETTINGS_FEATURES1;
					menusub = 0;
				}
				else if (menusub == 6)
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
			for(int i = 0; i < 5; i++) {
				strcpy(s,"          ");
				strcat(s, minimig_cfg.conf_name[i]);
				OsdWrite(i+1, s, menusub == i, !(menumask & (1<<i)));
			}
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
					LoadConfiguration(NULL, 0);
					ResetMenu();
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
					ChangeDirectoryName("..");
					if (ScanDirectory(SCAN_INIT_FIRST, fs_pFileExt, fs_Options))
						ScanDirectory(SCAN_INIT_NEXT, fs_pFileExt, fs_Options);
					else
						ScanDirectory(SCAN_INIT, fs_pFileExt, fs_Options);

					menustate = MENU_FILE_SELECT1;
				}
			}

			if ((c == KEY_PGUP) || (c == KEY_LEFT))
			{
				ScanDirectory(SCAN_PREV_PAGE, fs_pFileExt, fs_Options);
				menustate = MENU_FILE_SELECT1;
			}

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
					if (DirEntries[sort_table[iSelectedEntry]].fattrib & AM_DIR)
					{ // it's a directory
						if (tolower(i) < tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE))
								ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR);
						}
						else if (tolower(i) > tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
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
						if (tolower(i) < tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
						{
							if (!ScanDirectory(i, fs_pFileExt, fs_Options | FIND_DIR))
								ScanDirectory(i, fs_pFileExt, fs_Options | FIND_FILE);
						}
						else if (tolower(i) > tolower(DirEntries[sort_table[iSelectedEntry]].fname[0]))
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
				if (DirEntries[sort_table[iSelectedEntry]].fattrib & AM_DIR)
				{
					ChangeDirectoryName(DirEntries[sort_table[iSelectedEntry]].fname);
					{
						if (strncmp((char*)DirEntries[sort_table[iSelectedEntry]].fname, "..", 2) == 0)
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
						SelectedName = (char*) &DirEntries[sort_table[iSelectedEntry]].fname;
						strncpy(DiskInfo, DirEntryInfo[iSelectedEntry], sizeof(DiskInfo));
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
			m = 0;
			if (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2)
				m = 1;
			helptext=helptexts[HELPTEXT_NONE];
			OsdSetTitle("Reset",0);
			menumask=0x03;	// Yes / No
			parentstate=menustate;

			OsdWrite(0, "", 0,0);
			OsdWrite(1, m ? "         Reset MiST?" : "       Reset settings?", 0,0);
			OsdWrite(2, "", 0,0);
			OsdWrite(3, "             yes", menusub == 0,0);
			OsdWrite(4, "             no", menusub == 1,0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, "", 0,0);

			menustate = MENU_RESET2;
			break;

		case MENU_RESET2 :

			m = 0;
			if (user_io_core_type()==CORE_TYPE_MINIMIG || user_io_core_type()==CORE_TYPE_MINIMIG2)
				m = 1;

			if (select && menusub == 0)
			{
				if(m) {
					menustate = MENU_NONE1;
					OsdReset(RESET_NORMAL);
				} else {
					FIL file;
					UINT br;
					if (!user_io_create_config_name(s, "CFG", CONFIG_ROOT)) {
						iprintf("Saving config to %s\n", s);
						if(f_open(&file, s, FA_READ | FA_WRITE | FA_OPEN_ALWAYS) == FR_OK) {
						 // finally write data
							((unsigned long long*)sector_buffer)[0] = user_io_8bit_set_status(arc_get_default(),~0);
							f_write(&file, sector_buffer, 8, &br);
							iprintf("Settings for %s written\n", s);
							f_close(&file);
						}
					}
					menustate = MENU_8BIT_MAIN1;
					menusub = 0;
				}
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
			for(int i = 0; i < 5; i++) {
				strcpy(s,"          ");
				strcat(s, minimig_cfg.conf_name[i]);
				OsdWrite(i+1, s, menusub == i, 0);
			}
			OsdWrite(6, "", 0,0);
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
				menustate = MENU_SETTINGS_FEATURES1;
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
			strcat(s, config_memory_fast_txt());
			OsdWrite(3, s, menusub == 2,0);

			OsdWrite(4, "", 0,0);

			strcpy(s, "      ROM   : ");
			strncat(s, config.kickstart, sizeof(config.kickstart));
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
				//	if ((config.memory & 0x30) == 0x30)
				//		config.memory -= 0x30;
				//		if (!(config.disable_ar3 & 0x01)&&(config.memory & 0x20))
				//			config.memory &= ~0x30;
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
			if(config.enable_ide[t_ide_idx])
				menumask|=0x0a;  // b00001010 - HD0 and HD1 type
			siprintf(s, "   A600 %s IDE : %s",
				t_ide_idx ? "Secondary" : "Primary",
				config.enable_ide[t_ide_idx] ? "on " : "off");
			OsdWrite(0, s, menusub == 0,0);
			OsdWrite(1, "", 0,0);

			strcpy(s, " Master : ");
			if(config.hardfile[t_ide_idx << 1].enabled==(HDF_FILE|HDF_SYNTHRDB))
				strcat(s,"Hardfile (filesys)");
			else
				strcat(s, config_hdf_msg[config.hardfile[t_ide_idx << 1].enabled & HDF_TYPEMASK]);
			OsdWrite(2, s, config.enable_ide[t_ide_idx] ? (menusub == 1) : 0 ,config.enable_ide[t_ide_idx]==0);
			if (config.hardfile[t_ide_idx << 1].present)
			{
				strcpy(s, "                                ");
				strncpy(&s[14], config.hardfile[t_ide_idx << 1].name, sizeof(config.hardfile[0].name));
			}
			else
				strcpy(s, "       ** file not found **");

			enable=config.enable_ide[t_ide_idx] && ((config.hardfile[t_ide_idx << 1].enabled&HDF_TYPEMASK)==HDF_FILE);
			if(enable)
				menumask|=0x04;	// Make hardfile selectable
			OsdWrite(3, s, enable ? (menusub == 2) : 0 , enable==0);

			strcpy(s, "  Slave : ");
			if(config.hardfile[(t_ide_idx << 1) + 1].enabled==(HDF_FILE|HDF_SYNTHRDB))
				strcat(s,"Hardfile (filesys)");
			else
				strcat(s, config_hdf_msg[config.hardfile[(t_ide_idx << 1) + 1].enabled & HDF_TYPEMASK]);
			OsdWrite(4, s, config.enable_ide[t_ide_idx] ? (menusub == 3) : 0 ,config.enable_ide[t_ide_idx]==0);
			if (config.hardfile[(t_ide_idx << 1) + 1].present) {
				strcpy(s, "                                ");
				strncpy(&s[14], config.hardfile[(t_ide_idx << 1) + 1].name, sizeof(config.hardfile[0].name));
			}
			else
				strcpy(s, "       ** file not found **");
			enable=config.enable_ide[t_ide_idx] && ((config.hardfile[(t_ide_idx << 1) + 1].enabled&HDF_TYPEMASK)==HDF_FILE);
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
					config.enable_ide[t_ide_idx]=(config.enable_ide[t_ide_idx]==0);
					menustate = MENU_SETTINGS_HARDFILE1;
				}
				if (menusub == 1)
				{
					char idx = t_ide_idx << 1;
					if(config.hardfile[idx].enabled==HDF_FILE)
					{
						config.hardfile[idx].enabled|=HDF_SYNTHRDB;
					}
					else if(config.hardfile[idx].enabled==(HDF_FILE|HDF_SYNTHRDB))
					{
						config.hardfile[idx].enabled&=~HDF_SYNTHRDB;
						config.hardfile[idx].enabled +=1;
					}
					else
					{
						config.hardfile[idx].enabled +=1;
						config.hardfile[idx].enabled %=HDF_CARDPART0+partitioncount;
					}
					menustate = MENU_SETTINGS_HARDFILE1;
				}
				else if (menusub == 2)
				{
					SelectFile("HDF", SCAN_LFN, MENU_HARDFILE_SELECTED, MENU_SETTINGS_HARDFILE1, 0);
				}
				else if (menusub == 3)
				{
					char idx = (t_ide_idx << 1) + 1;
					if(config.hardfile[idx].enabled==HDF_FILE)
					{
						config.hardfile[idx].enabled|=HDF_SYNTHRDB;
					}
					else if(config.hardfile[idx].enabled==(HDF_FILE|HDF_SYNTHRDB))
					{
						config.hardfile[idx].enabled&=~HDF_SYNTHRDB;
						config.hardfile[idx].enabled +=1;
					}
					else
					{
						config.hardfile[idx].enabled +=1;
						config.hardfile[idx].enabled %=HDF_CARDPART0+partitioncount;
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
		case MENU_HARDFILE_SELECTED : {
			char idx;
			if (menusub == 2) // master drive selected
				idx = t_ide_idx << 1;
			else if (menusub == 4) // slave drive selected
				idx = (t_ide_idx << 1) + 1;
			else // invalid
				break;

			// Read RDB from selected drive and determine type...
			strncpy(config.hardfile[idx].name, SelectedName, sizeof(config.hardfile[idx].name));
			config.hardfile[idx].name[sizeof(config.hardfile[idx].name)-1] = 0;
			switch(GetHDFFileType(SelectedName))
			{
				case HDF_FILETYPE_RDB:
					config.hardfile[idx].enabled=HDF_FILE;
					config.hardfile[idx].present = 1;
					menustate = MENU_SETTINGS_HARDFILE1;
					break;
				case HDF_FILETYPE_DOS:
					config.hardfile[idx].enabled=HDF_FILE|HDF_SYNTHRDB;
					config.hardfile[idx].present = 1;
					menustate = MENU_SETTINGS_HARDFILE1;
					break;
				case HDF_FILETYPE_UNKNOWN:
					config.hardfile[idx].present = 1;
					if(config.hardfile[idx].enabled==HDF_FILE) // Warn if we can't detect the type
						menustate=MENU_SYNTHRDB1;
					else
						menustate=MENU_SYNTHRDB2_1;
					menusub=0;
					break;
				case HDF_FILETYPE_NOTFOUND:
				default:
					config.hardfile[idx].present = 0;
					menustate = MENU_SETTINGS_HARDFILE1;
					break;
			}
			break;
		}
		 // check if hardfile configuration has changed
		case MENU_HARDFILE_EXIT :

			if ((memcmp(config.hardfile, t_hardfile, sizeof(t_hardfile)) != 0) ||
			    (config.enable_ide[0] != t_enable_ide[0]) ||
			    (config.enable_ide[1] != t_enable_ide[1]))
			{
				menustate = MENU_HARDFILE_CHANGED1;
				menusub = 1;
			}
			else
			{
				menustate = MENU_MAIN1;
				menusub = 5 + t_ide_idx;
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
					for (int i = 0; i < HARDFILES; i++) {
						if ((config.hardfile[i].enabled != t_hardfile[i].enabled)
							|| (strncmp(config.hardfile[i].name, t_hardfile[i].name, sizeof(t_hardfile[0].name)) != 0))
						{
							OpenHardfile(i);
						//if((config.hardfile[0].enabled == HDF_FILE) && !FindRDB(0))
						//	menustate = MENU_SYNTHRDB1;
						}
					}

					if(menustate==MENU_HARDFILE_CHANGED2)
					{
						ConfigIDE(config.enable_ide[0],        config.hardfile[0].present && config.hardfile[0].enabled, config.hardfile[1].present && config.hardfile[1].enabled);
						ConfigIDE(config.enable_ide[1] | 0x02, config.hardfile[2].present && config.hardfile[2].enabled, config.hardfile[3].present && config.hardfile[3].enabled);
						OsdReset(RESET_NORMAL);

						menustate = MENU_NONE1;
					}
				}
				else if (menusub == 1) // no
				{
					memcpy(config.hardfile, t_hardfile, sizeof(t_hardfile)); // restore configuration
					config.enable_ide[t_ide_idx] = t_enable_ide[t_ide_idx];

					menustate = MENU_MAIN1;
					menusub = 5 + t_ide_idx;
				}
			}

			if (menu)
			{
				memcpy(config.hardfile, t_hardfile, sizeof(t_hardfile)); // restore configuration
				config.enable_ide[t_ide_idx] = t_enable_ide[t_ide_idx];

				menustate = MENU_MAIN1;
				menusub = 5;
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
				menustate = MENU_SETTINGS_FEATURES1;
				menusub = 0;
			}
			else if (left)
			{
				menustate = MENU_SETTINGS_MEMORY1;
				menusub = 0;
			}
			break;

		/******************************************************************/
		/* features settings menu                                         */
		/******************************************************************/
		case MENU_SETTINGS_FEATURES1 :
			menumask=0x07;
			parentstate=menustate;
			helptext=helptexts[HELPTEXT_FEATURES];

			OsdSetTitle("Features",OSD_ARROW_LEFT|OSD_ARROW_RIGHT);
			OsdWrite(0, "", 0,0);
			strcpy(s, "  Audio Filter  : ");
			strcat(s, config_audio_filter_msg[(config.features.audiofiltermode & 0x03) % 3]);
			OsdWrite(1, s, menusub == 0,0);
			strcpy(s, "  Power LED off : ");
			strcat(s, config_power_led_off_msg[config.features.powerledoffstate & 0x01]);
			OsdWrite(2, s, menusub == 1,0);
			OsdWrite(3, "", 0,0);
			OsdWrite(4, "", 0,0);
			OsdWrite(5, "", 0,0);
			OsdWrite(6, "", 0,0);
			OsdWrite(7, STD_EXIT, menusub == 2,0);

			menustate = MENU_SETTINGS_FEATURES2;
			break;

		case MENU_SETTINGS_FEATURES2 :
			if (select)
			{
				if (menusub == 0)
				{
					config.features.audiofiltermode++;
					if (config.features.audiofiltermode > 2)
						config.features.audiofiltermode = 0;
					menustate = MENU_SETTINGS_FEATURES1;
					ConfigFeatures(config.features.audiofiltermode, config.features.powerledoffstate);
				}
				else if (menusub == 1)
				{
					config.features.powerledoffstate ^= 1;
					menustate = MENU_SETTINGS_FEATURES1;
					ConfigFeatures(config.features.audiofiltermode, config.features.powerledoffstate);
				}
				else if (menusub == 2)
				{
					menustate = MENU_MAIN2_1;
					menusub = 5;
				}
			}

			if (menu)
			{
				menustate = MENU_MAIN2_1;
				menusub = 5;
			}
			else if (right)
			{
				menustate = MENU_SETTINGS_CHIPSET1;
				menusub = 0;
			}
			else if (left)
			{
				menustate = MENU_SETTINGS_VIDEO1;
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
					strncpy(config.kickstart, SelectedName, sizeof(config.kickstart));
					config.kickstart[sizeof(config.kickstart) - 1] = 0;
					if(minimig_v1()) {
						OsdDisable();
						OsdReset(RESET_BOOTLOADER);
						ConfigChipset(config.chipset | CONFIG_TURBO);
						ConfigFloppy(config.floppy.drives, CONFIG_FLOPPY2X);
						if (UploadKickstart(config.kickstart)) {
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
						UploadKickstart(config.kickstart);
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
					menusub = 3;
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
			char *v = GetFirmwareVersion("/FIRMWARE.UPG");
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
				case CORE_TYPE_MIST2:
					menusub = 4;
					menustate = MENU_MIST_MAIN1;
					break;
				case CORE_TYPE_ARCHIE:
					menusub = 5;
					menustate = MENU_ARCHIE_MAIN1;
					break;
				default:
					menusub = 0;
					menustate = (!strcmp(user_io_get_core_name(), "MENU") || (user_io_get_core_features() & FEAT_MENU)) ? MENU_NONE1 : MENU_8BIT_SYSTEM1;
					break;
				}
			}
			else if (select) {
				if (fat_uses_mmc() && (menusub == 0)) {
					if (CheckFirmware("/FIRMWARE.UPG"))
						menustate = MENU_FIRMWARE_UPDATE1;
					else
						menustate = MENU_FIRMWARE_UPDATE_ERROR1;
					menusub = 1;
					OsdClear();
				}
				else if (menusub == fat_uses_mmc()?1:0) {
					SelectFile("RBFARC", SCAN_LFN | SCAN_SYSDIR, MENU_FIRMWARE_CORE_FILE_SELECTED, MENU_FIRMWARE1, 0);
				}
				else if (menusub == fat_uses_mmc()?2:1) {
					switch(user_io_core_type()) {
					case CORE_TYPE_MIST:
					case CORE_TYPE_MIST2:
						menusub = 5;
						menustate = MENU_MIST_MAIN1;
						break;
					case CORE_TYPE_ARCHIE:
						menusub = 3;
						menustate = MENU_ARCHIE_MAIN1;
						break;
					default:
						menusub = 0;
						menustate = (!strcmp(user_io_get_core_name(), "MENU") || (user_io_get_core_features() & FEAT_MENU)) ? MENU_NONE1 : MENU_8BIT_SYSTEM1;
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
			OsdCoreNameSet(SelectedName);

			char mod = 0;
			const char *extension = GetExtension(SelectedName);
			char *rbfname = SelectedName;
			arc_reset();

			if (extension && !strncasecmp(extension,"ARC",3)) {
				mod = arc_open(SelectedName);
				if(mod < 0 || !strlen(arc_get_rbfname())) { // error
					menustate = MENU_NONE1;
					break;
				}
				strcpy(s, arc_get_rbfname());
				strcat(s, ".RBF");
				rbfname = (char*) &s;
			}
			user_io_reset();
			user_io_set_core_mod(mod);
			// reset fpga with core
			fpga_init(rbfname);

			// De-init joysticks to allow re-ordering for new core
			StateReset();

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

			WriteFirmware("/FIRMWARE.UPG");
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
		/* RTC menu                                                       */
		/******************************************************************/
		case MENU_RTC1: {

			uint8_t date[7]; //year,month,date,hour,min,sec,day
			helptext=helptexts[HELPTEXT_NONE];
			parentstate=menustate;

			menumask = 0xff;

			if (GetRTC((uint8_t*)&date)) {

				OsdSetTitle("Clock",0);
				siprintf(s, "       Year      %4d", 1900+date[0]);
				OsdWrite(0, s, menusub == 0, 0);
				siprintf(s, "       Month       %2d", date[1]);
				OsdWrite(1, s, menusub == 1, 0);
				siprintf(s, "       Date        %2d", date[2]);
				OsdWrite(2, s, menusub == 2, 0);
				siprintf(s, "       Hour        %2d", date[3]);
				OsdWrite(3, s, menusub == 3, 0);
				siprintf(s, "       Minute      %2d", date[4]);
				OsdWrite(4, s, menusub == 4, 0);
				siprintf(s, "       Second      %2d", date[5]);
				OsdWrite(5, s, menusub == 5, 0);
				siprintf(s, "       Day  %9s", date[6] <= 7 ? days[date[6]-1] : "--------");
				OsdWrite(6, s, menusub == 6, 0);
				OsdWrite(7, STD_EXIT, menusub == 7, 0);
				menustate = MENU_RTC2;
			} else {
				menustate = MENU_8BIT_SYSTEM1;
			}
			break;
		}

		case MENU_RTC2: {
			uint8_t date[7]; //year,month,date,hour,min,sec,day
			static const char mdays[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
			int year;
			uint8_t is_leap, month, maxday;

			static unsigned long timer = 0;
			if (CheckTimer(timer)) {
				timer = GetTimer(1000);
				menustate = MENU_RTC1;
				break;
			}
			if (menu) {
				menusub = 1;
				menustate = MENU_8BIT_SYSTEM1;
			}
			else if (select) {
				if (menusub == 7) {
					menusub = 1;
					menustate = MENU_8BIT_SYSTEM1;
				}
			} else if (left | right) {
				if (GetRTC((uint8_t*)&date)) {
					year = 1900+date[0];
					month = date[1];
					if (month > 12) month = 12;
					is_leap = (!(year % 4) && (year % 100)) || !(year % 400);
					maxday = mdays[month-1] + (month == 2 && is_leap);

					switch(menusub) {
					case 0: if (left) date[0]--; else date[0]++; break;
					case 1: if (left) date[1] = decval(date[1], 1, 12); else date[1] = incval(date[1], 1, 12); break;
					case 2: if (left) date[2] = decval(date[2], 1, maxday); else date[2] = incval(date[2], 1, maxday); break;
					case 3: if (left) date[3] = decval(date[3], 0, 23); else date[3] = incval(date[3], 0, 23); break;
					case 4: if (left) date[4] = decval(date[4], 0, 59); else date[4] = incval(date[4], 0, 59); break;
					case 5: if (left) date[5] = decval(date[5], 0, 59); else date[5] = incval(date[5], 0, 59); break;
					case 6: if (left) date[6] = decval(date[6], 1, 7); else date[6] = incval(date[6], 1, 7); break;
					}
					if (SetRTC((uint8_t*)&date))
						menustate = MENU_RTC1;
				}
			}
			break;
		}

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


static void ScrollLongName(void)
{
	// this function is called periodically when file selection window is displayed
	// it checks if predefined period of time has elapsed and scrolls the name if necessary

	char k = sort_table[iSelectedEntry];
	static int len;
	int max_len;

	if (DirEntries[k].fname[0]) // && CheckTimer(scroll_timer)) // scroll if long name and timer delay elapsed
	{
		// FIXME - yuk, we don't want to do this every frame!
		len = strlen(DirEntries[k].fname); // get name length

		if((len > 4) && !fs_ShowExt)
			if (DirEntries[k].fname[len - 4] == '.')
				len -= 4; // remove extension

		max_len = 30; // number of file name characters to display (one more required for scrolling)
		if (DirEntries[k].fattrib & AM_DIR)
			max_len = 23; // number of directory name characters to display

		ScrollText(iSelectedEntry,DirEntries[k].fname,len,max_len,1,2);
	}
}


static char* GetDiskInfo(char* lfn, long len)
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
static void PrintDirectory(void)
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
            lfn = DirEntries[k].fname; // long file name pointer
            DirEntryInfo[i][0] = 0; // clear disk number info buffer

            len = strlen(lfn); // get name length
            info = NULL; // no disk info

            if (!(DirEntries[k].fattrib & AM_DIR)) // if a file
            {
                if((len > 4) && !fs_ShowExt)
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

            if (DirEntries[k].fattrib & AM_DIR) // mark directory with suffix
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

static void _strncpy(char* pStr1, const char* pStr2, size_t nCount)
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

static void inserttestfloppy() {
  char name[] = "/AUTOX.ADF";
  int i;

  for(i=0;i<4;i++) {
    name[5] = '0'+i;
    InsertFloppy(&df[i], name);
  }
}

// insert floppy image pointed to to by global <file> into <drive>
static void InsertFloppy(adfTYPE *drive, const unsigned char *name)
{
    unsigned char i, j, readonly = false;
    unsigned long tracks;

    if (f_open(&drive->file, name, FA_READ | FA_WRITE) != FR_OK) {
        readonly = true;
        if (f_open(&drive->file, name, FA_READ) != FR_OK)
            return;
    }
    // calculate number of tracks in the ADF image file
    tracks = f_size(&drive->file) / (512*11);
    if (tracks > MAX_TRACKS)
    {
        menu_debugf("UNSUPPORTED ADF SIZE!!! Too many tracks: %lu\r", tracks);
        tracks = MAX_TRACKS;
    }
    drive->tracks = (unsigned char)tracks;

    // copy image file name into drive struct
    _strncpy(drive->name, name, sizeof(drive->name));

    if (DiskInfo[0]) // if selected file has valid disk number info then copy it to its name in drive struct
    {
        drive->name[16] = ' '; // precede disk number info with space character
        strncpy(&drive->name[17], DiskInfo, sizeof(DiskInfo)); // copy disk number info
    }

    // initialize the rest of drive struct
    drive->status = DSK_INSERTED;
    if (!readonly) // read-only attribute
        drive->status |= DSK_WRITABLE;

    drive->sector_offset = 0;
    drive->track = 0;
    drive->track_prev = -1;

    // some debug info
    menu_debugf("Inserting floppy: \"%s\"\r", name);
    menu_debugf("file readonly: 0x%u\r", readonly);
    menu_debugf("file size: %llu (%llu KB)\r", f_size(&drive->file), f_size(&drive->file) >> 10);
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

unsigned const char *config_memory_fast_txt()
{
  if (!(((config.cpu & 0x02) == 0x02) && ((config.memory >> 4 & 0x03) == 0x03)))
    return config_memory_fast_msg[config.memory >> 4 & 0x03];
  else
    return config_memory_fast_msg[(config.memory >> 4 & 0x03) + 1];
}
