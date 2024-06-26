/*
 main.arm9.c

 By Michael Chisholm (Chishm)

 All resetMemory and startBinary functions are based
 on the MultiNDS loader by Darkain.
 Original source available at:
 http://cvs.sourceforge.net/viewcvs.py/ndslib/ndslib/examples/loader/boot/main.cpp

 License:
    NitroHax -- Cheat tool for the Nintendo DS
    Copyright (C) 2008  Michael "Chishm" Chisholm

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#define ARM9
#undef ARM7
#include <nds/memory.h>
#include <nds/arm9/video.h>
#include <nds/arm9/input.h>
#include <nds/interrupts.h>
#include <nds/dma.h>
#include <nds/timers.h>
#include <nds/system.h>
#include <nds/ipc.h>

#include "common.h"
#include "miniConsole.h"

#define TMP_DATA 0x027FC000

volatile u32 arm9_BLANK_RAM = 0;
volatile u32 arm9_errorCode = 0xFFFFFFFF;
volatile bool consoleDebugMode = false;
// volatile u32 defaultFontPalSlot = 0;
volatile tLauncherSettings* tmpData = (tLauncherSettings*)TMP_DATA;

static bool consoleInit = false;
static bool debugMode = false;

static char TXT_STATUS[] = "STATUS: ";
static char TXT_ERROR[] = "ERROR: ";
static char ERRTXT_NONE[] = "NONE";
static char ERRTXT_STS_CLRMEM[] = "CLEAR MEMORY";
static char ERRTXT_STS_LOAD_BIN[] = "LOAD CART";
static char ERRTXT_STS_STARTBIN[] = "START BINARY";
static char ERRTXT_STS_START[] = "BOOTLOADER STARTUP";
static char ERRTXT_SDINIT[] = "SD INIT FAIL";
static char ERRTXT_FILE[] = "FILE LOAD FAIL";
static char NEW_LINE[] = "\n";

static void arm9_errorOutput (u32 code) {
	Print(NEW_LINE);
	switch (code) {
		case (ERR_NONE) : {
			// BG_PALETTE_SUB[defaultFontPalSlot] = 0x8360;
			Print(TXT_STATUS);
			Print(ERRTXT_NONE);
		} break;
		case (ERR_STS_CLR_MEM) : {
			// BG_PALETTE_SUB[defaultFontPalSlot] = 0x8360;
			Print(TXT_STATUS);
			Print(ERRTXT_STS_CLRMEM);
		} break;
		case (ERR_STS_LOAD_BIN) : {
			// BG_PALETTE_SUB[defaultFontPalSlot] = 0x8360;
			Print(TXT_STATUS);
			Print(ERRTXT_STS_LOAD_BIN);
		} break;
		case (ERR_STS_STARTBIN) : {
			// BG_PALETTE_SUB[defaultFontPalSlot] = 0x8360;
			Print(TXT_STATUS);
			Print(ERRTXT_STS_STARTBIN);
		} break;
		case (ERR_STS_START) : {
			// BG_PALETTE_SUB[defaultFontPalSlot] = 0x8360;
			Print(TXT_STATUS);
			Print(ERRTXT_STS_START);
		} break;
		case (ERR_SDINIT) : {
			// BG_PALETTE_SUB[defaultFontPalSlot] = 0x801B;
			Print(TXT_ERROR);
			Print(ERRTXT_SDINIT);
		} break;
		case (ERR_FILELOAD) : {
			// BG_PALETTE_SUB[defaultFontPalSlot] = 0x801B;
			Print(TXT_ERROR);
			Print(ERRTXT_FILE);
		} break;
	}
}

/*-------------------------------------------------------------------------
External functions
--------------------------------------------------------------------------*/
extern void arm9_clearCache (void);
extern void arm9_reset (void);

/*-------------------------------------------------------------------------
arm9_main
Clears the ARM9's icahce and dcache
Clears the ARM9's DMA channels and resets video memory
Jumps to the ARM9 NDS binary in sync with the  ARM7
Written by Darkain, modified by Chishm
--------------------------------------------------------------------------*/
void arm9_main (void) {
			
	register int i;

	//set shared ram to ARM7
	WRAM_CR = 0x03;
	REG_EXMEMCNT = 0xE880;
	
	// Disable interrupts
	REG_IME = 0;
	REG_IE = 0;
	REG_IF = ~0;

	if (debugMode)arm9_errorCode = ERR_STS_START;

	// Synchronise start
	ipcSendState(ARM9_START);
	while (ipcRecvState() != ARM7_START);

	ipcSendState(ARM9_MEMCLR);

	arm9_clearCache();
	
	// Removed VRAM clearing/Display reset code. It is not needed for booting bootstrap and makes the transition quicker

	for (i=0; i<16*1024; i+=4) {  //first 16KB
		(*(vu32*)(i+0x00000000)) = 0x00000000;      //clear ITCM
		(*(vu32*)(i+0x00800000)) = 0x00000000;      //clear DTCM
	}

	for (i=16*1024; i<32*1024; i+=4) {  //second 16KB
		(*(vu32*)(i+0x00000000)) = 0x00000000;      //clear ITCM
	}

	(*(vu32*)0x00803FFC) = 0;   //IRQ_HANDLER ARM9 version
	(*(vu32*)0x00803FF8) = ~0;  //VBLANK_INTR_WAIT_FLAGS ARM9 version

	// Clear out FIFO
	REG_IPC_FIFO_CR = IPC_FIFO_ENABLE | IPC_FIFO_SEND_CLEAR;
	REG_IPC_FIFO_CR = 0;

	// Clear out ARM9 DMA channels
	for (i=0; i<4; i++) {
		DMA_CR(i) = 0;
		DMA_SRC(i) = 0;
		DMA_DEST(i) = 0;
		TIMER_CR(i) = 0;
		TIMER_DATA(i) = 0;
	}
	
	// set ARM9 state to ready and wait for instructions from ARM7
	ipcSendState(ARM9_READY);
	while (ipcRecvState() != ARM7_BOOTBIN) {
		if (ipcRecvState() == ARM7_ERR) {
			if (!consoleInit) {
				/*BG_PALETTE_SUB[0] = RGB15(31,31,31);
				BG_PALETTE_SUB[255] = RGB15(0,0,0);
				defaultFontPalSlot = 0x1F;*/
				miniconsoleSetWindow(5, 11, 24, 1); // Set console position for debug text if/when needed.
				consoleInit = true;
			}
			arm9_errorOutput (arm9_errorCode);
			// Halt after displaying error code
			while(1);
		} else if ((arm9_errorCode != ERR_NONE) && debugMode) {
			if (!consoleInit) {
				/*BG_PALETTE_SUB[0] = RGB15(31,31,31);
				BG_PALETTE_SUB[255] = RGB15(0,0,0);
				defaultFontPalSlot = 0x1F;*/
				miniconsoleSetWindow(5, 11, 24, 1); // Set console position for debug text if/when needed.
				consoleInit = true;
			}
			while(REG_VCOUNT!=191); // Add vblank delay. Arm7 can somtimes go through the status codes pretty quick.
			while(REG_VCOUNT==191);
			arm9_errorOutput (arm9_errorCode);
			arm9_errorCode = ERR_NONE;
		}
	}

	arm9_reset();
}

