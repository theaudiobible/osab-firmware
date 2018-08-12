/*
 * osab.c - Firmware for the Open Source Audio Bible player.
 * 
 * Copyright (C) 2011-2017 Theophilus (http://audiobibleplayer.org)
 *
 * Parts of this code are derived from VLSI's audio book and MMC sample code - http://vlsi.fi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vs1000.h>
#include <audio.h>
#include <mappertiny.h>
#include <minifat.h>
#include <codec.h>
#include <vsNand.h>
#include <player.h>
#include <usblowlib.h>
#include <dev1000.h>

/* Address of config data in eeprom = 8192 - 32 (i.e. last page) */
#define CONFIG          8160
#define CHAPTER         0       /* Offset of chapter number (file number) */
#define SECONDS         2       /* cs.playTimeSeconds */
#define VOLUME          4       /* Offset of volume setting */
#define BOOKMARK        6       /* Offset of bookmark setting */

/* Address of bookmark data in eeprom = 8192 - 2*32 (i.e. 2'nd last page) */
#define BOOKMARKS       8128
#define BKMK_FIRST      0       /* First Bookmark (4 bytes per bookmark) */
#define BKMK_LAST       28      /* Last Bookmark (n-1)*4 */

#define VOL_MIN         64      /* Minimum volume to save to eeprom - remember
                                   that the greater this number, the lower the
                                   volume */

#define SYSTEMMAINFREQ      6000000
#define BATTERYCHECKFREQ    1
#define BATTERYLOWTIME      90

/* Battery Status LED on GPIO0_13 */
#define BAT_LED_BIT 13
#define BAT_LED (1 << BAT_LED_BIT)

/* Amp Shutdown on GPIO0_14 */
#define AMP_BIT 14
#define AMP (1 << AMP_BIT)

/* UART TX and RX pins on GPIO1_4 and GPIO1_5, respectively */
#define TX_BIT 4
#define RX_BIT 5
#define TX (1 << TX_BIT)
#define RX (1 << RX_BIT)

// #define USE_DEBUG

#ifdef USE_DEBUG
#define DEBUG_LEVEL 1
#endif

/* Removes 4G restriction from USB (SCSI).
    Also detects MMC/SD removal while attached to USB.
    (62 words) */
#define PATCH_LBAB
/* Note that PATCH_LBAB is still needed - srp */

extern struct FsPhysical *ph;
extern struct FsMapper *map;
extern struct Codec *cod;
extern struct CodecServices cs;
extern u_int16 codecVorbis[];

struct MENUENTRY {
    u_int16 parent;         /*0 parent index:  0 or 1..65535 */
    u_int16 subtree;        /*1 subtree index: 0 or 1..65535 */
};

struct MENU {
    u_int32 currentSector;
    u_int16 buffer[256];
} menu;


/* Global variables */
struct MENUENTRY playingEntry;
u_int32 menuStart;          /* menu file must be unfragmented! */
u_int16 offset;             /* menu index offset of first file */
u_int16 book1;              /* parent index of first book */
u_int16 battery_low = BATTERYLOWTIME; /* Seconds to warn of battery low */
s_int16 bookmark;           /* pointer to current bookmark */
u_int16 bkmk_pressed = 0;   /* set if bookmark was pressed */
u_int16 goTo;               /* offset of seconds to start within file */
u_int16 repeat = 0;         /* repeat chapter */
u_int16 prejump_file;       /* to save file before jump elsewhere */
u_int16 prejump_playtime;   /* to save PlayTime (sec) before jump */


static const u_int32 oggFiles[] = { FAT_MKID('O','G','G'), 0 };

const struct KeyMapping playModeMap[] = {
    {KEY_POWER, ke_pauseToggle}, 
    {KEY_LONG_PRESS|KEY_POWER, ke_powerOff}, 
    {(~KEY_8)&KEY_1, ke_volumeDown2}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_1, ke_volumeDown2}, 
    {(~KEY_8)&KEY_2, ke_volumeUp2}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_2, ke_volumeUp2}, 
    {(~KEY_8)&KEY_1|KEY_2, ke_repeat},
    {(~KEY_8)&KEY_3, ke_previous}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_3, ke_rewind}, 
    {(~KEY_8)&KEY_4, ke_next}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_4, ke_ff_faster}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_1|KEY_2, KEY_LONG_ONESHOT|(u_int16)ke_earSpeakerToggle},
    {(~KEY_8)&KEY_LONG_PRESS|KEY_RELEASED, ke_ff_off}, 
    {(~KEY_8)&KEY_5, ke_OT_NT}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_5, KEY_LONG_ONESHOT|(u_int16)ke_bookmark},
    {(~KEY_8)&KEY_6, ke_bookPrev}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_6, KEY_LONG_ONESHOT|(u_int16)ke_markPrev}, 
    {(~KEY_8)&KEY_7, ke_bookNext}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_7, KEY_LONG_ONESHOT|(u_int16)ke_markNext}, 
    {(~KEY_8)&KEY_LONG_PRESS|KEY_5|KEY_6, KEY_LONG_ONESHOT|(u_int16)ke_resetBookmarks},
    {(~KEY_8)&KEY_1|KEY_3, KEY_LONG_ONESHOT|(u_int16)ke_back},
    {(~KEY_8)&KEY_2|KEY_3, KEY_LONG_ONESHOT|(u_int16)ke_back},
    {0, ke_null}
};

#define SPI_EEPROM_COMMAND_WRITE_ENABLE             0x06
#define SPI_EEPROM_COMMAND_WRITE_DISABLE            0x04
#define SPI_EEPROM_COMMAND_READ_STATUS_REGISTER     0x05
#define SPI_EEPROM_COMMAND_WRITE_STATUS_REGISTER    0x01
#define SPI_EEPROM_COMMAND_READ                     0x03
#define SPI_EEPROM_COMMAND_WRITE                    0x02

//macro to set SPI to MASTER; 8BIT; FSYNC Idle => xCS high
#define SPI_MASTER_8BIT_CSHI    PERIP(SPI0_CONFIG) = \
    SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE1

//macro to set SPI to MASTER; 8BIT; FSYNC not Idle => xCS low
#define SPI_MASTER_8BIT_CSLO    PERIP(SPI0_CONFIG) = \
    SPI_CF_MASTER | SPI_CF_DLEN8 | SPI_CF_FSIDLE0

//macro to set SPI to MASTER; 16BIT; FSYNC not Idle => xCS low
#define SPI_MASTER_16BIT_CSLO   PERIP(SPI0_CONFIG) = \
    SPI_CF_MASTER | SPI_CF_DLEN16 | SPI_CF_FSIDLE0


void SetInterruptVector_Timer1(void);

/* Emit a short beep for user feedback */

void beep(void) {
    __y s_int16 *p = audioBuffer;
    register u_int16 i;
    for (i = 100; i > 0; i--) {
        if (p >= audioBuffer + DEFAULT_AUDIO_BUFFER_SAMPLES) {
            p -= DEFAULT_AUDIO_BUFFER_SAMPLES;
        }
        *p += 8000;
        p += 27;
    }
}

/// Wait for not_busy (status[0] = 0) and return status
void SpiWaitStatus(void) {
    u_int16 status;

    SPI_MASTER_8BIT_CSHI;
    SpiDelay(0);
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_READ_STATUS_REGISTER); 
    while ((status = SpiSendReceive(0xff)) & 0x01){
#ifdef USE_DEBUG
    puthex(status); puts("=eeprom status");
#endif
    SpiDelay(0);
    }
    SPI_MASTER_8BIT_CSHI; 
}

void SpiWrite(u_int16 addr, u_int16 data) {
    SPI_MASTER_8BIT_CSHI; 
    SpiDelay(0);
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_WRITE_ENABLE);
    SPI_MASTER_8BIT_CSHI;
    SpiDelay(0);
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_WRITE);
    SPI_MASTER_16BIT_CSLO;
    SpiSendReceive(addr);
    SpiSendReceive(data); 
    SPI_MASTER_8BIT_CSHI;
    SpiWaitStatus();
    SPI_MASTER_8BIT_CSHI; 
    SpiDelay(0);
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_WRITE_DISABLE);
    SPI_MASTER_8BIT_CSHI;
}

u_int16 SpiRead(u_int16 addr) {
    register u_int16 data;

    SpiWaitStatus();    
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_READ);
    SPI_MASTER_16BIT_CSLO;
    SpiSendReceive(addr);
    data = SpiSendReceive(0);
    SPI_MASTER_8BIT_CSHI;
    return data;
}

int MenuInit(void) {
    static const u_int32 mnuFiles[] = {FAT_MKID('M', 'N', 'U'), 0 };

    minifatInfo.supportedSuffixes = &mnuFiles[0];
    /* MENU.MNU */
#ifdef USE_DEBUG
    puts("Entered MenuInit()");
#endif
    if (OpenFileBaseName("\pMENU    ") != 0xffffU) {
#ifdef USE_DEBUG
        puts("Opened menu.mnu");
#endif
        menuStart = minifatFragments[0].start & 0x7fffffffUL;
#ifdef USE_DEBUG
        puthex(menuStart); puts("=menuStart");
#endif
        return 0;
    }
#ifdef USE_DEBUG
    puts("Failed to open menu.mnu");
#endif
    return -1;
}

const void *MenuGetEntry(register __c0 u_int16 entry) {
    register u_int32 wordPos = (u_int32)entry * 2; /* 4 bytes per entry */
    u_int32 sector = (wordPos >> 8) + menuStart;
#if 0
    puthex(entry);
    puts("=entry");
    puthex(wordPos);
    puts("=pos");
    puthex(sector);
    puts("=sector");
#endif
    if (sector != menu.currentSector) {
        menu.currentSector = sector;
        MapperReadDiskSector(menu.buffer, sector);
    }
    return (void *)(menu.buffer + (wordPos & 255));
}

#ifdef PATCH_LBAB
#include <scsi.h>
extern struct SCSIVARS {
    SCSIStageEnum State;
    SCSIStatusEnum Status;
    u_int16 *DataOutBuffer;
    u_int16 DataOutSize;
    u_int16 DataBuffer[32];
    u_int16 *DataInBuffer;
    u_int16 DataInSize;
    unsigned int BlocksLeftToSend;
    unsigned int BlocksLeftToReceive;
    u_int32 CurrentDiskSector;
    u_int32 mapperNextFlushed;
    u_int16 cswPacket[7];   /*< command reply (Command Status Wrapper) data */
    u_int32 DataTransferLength; /*< what is expected by CBW */
    s_int32 Residue; /*< difference of what is actually transmitted by SCSI */
    s_int16 DataDirection;
} SCSI;
#endif/*PATCH_LBAB*/

enum mmcState {
    mmcNA = 0, 
    mmcOk = 1, 
};
struct {
    enum mmcState state;
    s_int16 errors;
    s_int16 hcShift;
    u_int32 blocks;
} mmc;

#ifdef USE_DEBUG
static char hex[] = "0123456789ABCDEF";
void puthex(u_int16 d) {
    char mem[5];
    mem[0] = hex[(d>>12)&15];
    mem[1] = hex[(d>>8)&15];
    mem[2] = hex[(d>>4)&15];
    mem[3] = hex[(d>>0)&15];
    mem[4] = '\0';
    fputs(mem, stdout);
}
#endif

s_int16 InitializeMmc(s_int16 tries) {
    register u_int16 i;
    mmc.state = mmcNA;
    mmc.blocks = 0;
    mmc.errors = 0;

#if DEBUG_LEVEL > 1
    puthex(clockX);
    puts(" clockX");
#endif
tryagain:
    IdleHook();

    mmc.hcShift = 9;
    if (tries-- <= 0) {
        return ++mmc.errors;
    }

    for (i = 512; i > 0; i--) {
        SpiSendClocks();
    }

    /* MMC Init, command 0x40 should return 0x01 if all is ok. */
    i = MmcCommand(MMC_GO_IDLE_STATE/*CMD0*/|0x40, 0);
    if (i != 1) {
#if DEBUG_LEVEL > 1
        puthex(i);
        puts(" Init");
#endif
        BusyWait10();
        goto tryagain;//continue; /* No valid idle response */
    }
    //cmd = MMC_SEND_OP_COND|0x40;

    /*CMD8 is mandatory before ACMD41
        for hosts compliant to phys. spec. 2.00 */
    i = MmcCommand(MMC_SEND_IF_COND/*CMD8*/|0x40, 0x00000122/*2.7-3.6V*/); /*note: 0x22 for the right CRC!*/
#if DEBUG_LEVEL > 2
    puthex(i);
    puts("=IF_COND");
#endif
    {
    register u_int16 parametr;
    if (i == 1) parametr = 0x4010;
    else if (i == 5) parametr = 0;
    else goto tryagain;//return ++mmc.errors;
#if DEBUG_LEVEL > 1
    puthex(SpiSendReceiveMmc(-1, 16));
    puthex(SpiSendReceiveMmc(-1, 16));
    puts("=R7");
    i = MmcCommand(MMC_READ_OCR/*CMD58*/|0x40, 0);
    puthex(i);
    puts("=READ_OCR");
    puthex(SpiSendReceiveMmc(-1, 16));
    puthex(SpiSendReceiveMmc(-1, 16));
    puts("=R3");
#else
    MmcCommand(MMC_READ_OCR/*CMD58*/|0x40, 0);
    i = SpiSendReceiveMmc(-1, 16);
    SpiSendReceiveMmc(-1, 16);
    if ((i & 0x00FF) != 0x00FF) goto tryagain;//return ++mmc.errors;    /*Check support voltage*/
#endif
    while (1) {
        MmcCommand(0x40|55/*CMD55*/, 0);
#if DEBUG_LEVEL > 2
        puthex(i);
        puts("=CMD55");
#endif
        i = MmcCommand(41|0x40/*MMC_SEND_OP_COND|0x40*/, (u_int32)parametr << 16);
#if DEBUG_LEVEL > 1
        puthex(i);
        puts("=ACMD41");
#endif
        if (i == 0) {
#if DEBUG_LEVEL > 1
            puts("got 0");
#endif
            break;
        }
        if (i != 1) {
#if DEBUG_LEVEL > 1
            puthex(i);
            puts(" Timeout 2");
#endif
            goto tryagain; /* Not able to power up mmc */
        }
        BusyWait10();
    }

    if (parametr) {
#if DEBUG_LEVEL > 1
        i = MmcCommand(MMC_READ_OCR/*CMD58*/|0x40, 0);
        puthex(i);
        puts("=READ_OCR");
        if (i == 0) {
            i = SpiSendReceiveMmc(-1, 16);
            if (i & 0x4000) {
                /* OCR[30]:CCS - card capacity select */
                /* HighCapacity! */
                puts("=HC");
                mmc.hcShift = 0;
            }
            puthex(i);
            puthex(SpiSendReceiveMmc(-1, 16));
            puts("=R3");
        }
#else
        if (MmcCommand(MMC_READ_OCR/*CMD58*/|0x40, 0) == 0) {
            if (SpiSendReceiveMmc(-1, 16) & 0x4000) mmc.hcShift = 0;
            SpiSendReceiveMmc(-1, 16);
        }
#endif
    }
    if (MmcCommand(MMC_SEND_CSD/*CMD9*/|0x40, 0) == 0) {
        register s_int16 *p = (s_int16 *)minifatBuffer;
        i = 640;
        while (SpiSendReceiveMmc(0xff00, 8) == 0xff) {
            if (i-- == 0) {
#if DEBUG_LEVEL > 1
                puts("Timeout 3");
#endif
                goto tryagain;
            }
        }
        for (i = 9; i > 0; i--) {
            *p++ = SpiSendReceiveMmc(-1, 16);
#if DEBUG_LEVEL > 1
            puthex(p[-1]);
#endif
        }
        if ((minifatBuffer[0] & 0xf000) == 0x4000) {
        /* v2.0 in 512kB resolution */
            mmc.blocks = (((((u_int32)minifatBuffer[3] << 16) | minifatBuffer[4]) & 0x3fffff) + 1) << 10;
        } else {
        /* v1.0 */
            register u_int16 c_mult = ((minifatBuffer[4] & 3) << 1) | ((u_int16)minifatBuffer[5] >> 15);
            register u_int32 c_size = (((minifatBuffer[3] & 0x03ff) << 2 ) | ((minifatBuffer[4] >> 14) & 3)) + 1;
            mmc.blocks = c_size << (2 + c_mult + (minifatBuffer[2] & 15) - 9);
        }
#if DEBUG_LEVEL > 1
        puts("=CSD");
        puthex(mmc.blocks>>16);
        puthex(mmc.blocks);
        puts("=mmcBlocks");
#endif
    }

#if DEBUG_LEVEL > 1
    if (MmcCommand(MMC_SEND_CID/*CMD10*/|0x40, 0) == 0) {
        i = 3200;
        while (SpiSendReceiveMmc(0xff00, 8) == 0xff) {
            if (i-- == 0) goto tryagain;
        }
        for (i = 9; i > 0; i--) puthex(SpiSendReceiveMmc(-1, 16));
        puts("=CID");
    }
#endif

    /* Set Block Size of 512 bytes -- default for at least HC */
    /* Needed by MaxNova S043618ATA 2J310700 MV016Q-MMC */
    /* Must check return value! (MicroSD restart!) */
#if DEBUG_LEVEL > 1
    i = MmcCommand(MMC_SET_BLOCKLEN|0x40, 512);
    if (i != 0) {
        puthex(i);
        puts(" SetBlockLen failed");
        goto tryagain;
    }
#else
    if (MmcCommand(MMC_SET_BLOCKLEN|0x40, 512) != 0) {
        goto tryagain;
    }
#endif

    /* All OK return */
    //mmc.errors = 0;
    mmc.state = mmcOk;
    map->blocks = mmc.blocks;
#ifdef USE_DEBUG
    puts("Completed MMC Init OK.");
#endif
    return 0;//mmc.errors;
    }
}


auto u_int16 MyReadDiskSector(register __i0 u_int16 *buffer, register __reg_a u_int32 sector) {
    register s_int16 i;
    register u_int16 t = 65535;

    if (mmc.state == mmcNA || mmc.errors) {
        cs.cancel = 1;
        return 5;
    }
#if 0 && defined(USE_DEBUG)
    puthex(sector>>16);
    puthex(sector);
    puts("=ReadDiskSector");
#endif
    MmcCommand(MMC_READ_SINGLE_BLOCK|0x40, sector << mmc.hcShift);
    do {
        i = SpiSendReceiveMmc(0xff00, 8);
    } while (i == 0xff && --t != 0);

    if (i != 0xfe) {
        memset(buffer, 0, 256);
        if (i > 15 /*unknown error code*/) {
            mmc.errors++;
#if 0
        putch('R');
#endif
        } else {
        /* data retrieval failed:
            D4-D7 = 0
            D3 = out of range
            D2 = Card ECC Failed
            D1 = CC Error
            D0 = Error
            */
        }
        SpiSendClocks();
        return 1;
    }
    for (i = 512/2; i > 0; i--) {
        *buffer++ = SpiSendReceiveMmc(0xffff, 16);
    }
    SpiSendReceiveMmc(0xffff, 16); /* discard crc */

    /* generate some extra SPI clock edges to finish up the command */
    SpiSendClocks();
    SpiSendClocks();

    /* We force a call of user interface after each block even if we
        have no idle CPU. This prevents problems with key response in
        fast play mode. */
    IdleHook();
    return 0; /* All OK return */
}

u_int16 FsMapMmcRead(struct FsMapper *map, u_int32 firstBlock, u_int16 blocks, u_int16 *data);

const struct FsMapper mmcMapper = {
    0x010c,         /*version*/
    256,            /*blocksize in words*/
    0,              /*blocks -- read from CSD*/
    0,              /*cacheBlocks*/
    NULL,           //FsMapMmcCreate, 
    FsMapFlNullOk,  //RamMapperDelete, 
    FsMapMmcRead, 
    NULL,           //FsMapMmcWrite, 
    NULL,           //FsMapFlNullOk, //RamMapperFree, 
    FsMapFlNullOk,  //RamMapperFlush, 
    NULL            /* no physical */
};


u_int16 FsMapMmcRead(struct FsMapper *map, u_int32 firstBlock, u_int16 blocks, u_int16 *data) {
    register u_int16 bl = 0;
#ifndef PATCH_LBAB /*if not patched already*/
    firstBlock &= 0x00ffffff; /*remove sign extension: 4G -> 8BG limit*/
#endif
    while (bl < blocks) {
        if (MyReadDiskSector(data, firstBlock))
        break; /* probably MMC detached */
        data += 256;
        firstBlock++;
        bl++;
    }
    return bl;
}


#if defined(PATCH_TEST_UNIT_READY) && defined(PATCH_LBAB)
void ScsiTestUnitReady(void) {
    /* Poll MMC present by giving it a command. */
    if (mmc.state == mmcOk && mmc.errors == 0 && MmcCommand(MMC_SET_BLOCKLEN|0x40, 512) != 0) {
        mmc.errors++;
    }
    if (mmc.state == mmcNA || mmc.errors) {
        SCSI.Status = SCSI_REQUEST_ERROR; /* report error at least once! */
    }
}
#endif

void MyKeyEventHandler(enum keyEvent event) { /*140 words*/
    register const struct MENUENTRY *m;
    register u_int16 subtree, parent, i;

    /* separate the small-numbered cases */
    switch (event) {
        case ke_bookPrev:
            if (playingEntry.parent > book1) {
                m = (struct MENUENTRY *)MenuGetEntry(playingEntry.parent-1);
            } else {
                m = (struct MENUENTRY *)MenuGetEntry(offset-1);
            }
            subtree = m->subtree;
            player.nextFile = subtree - offset;
            cs.cancel = 1;
            repeat = 0;
            prejump_file = player.currentFile;
            prejump_playtime = (u_int16)cs.playTimeSeconds;
            beep();
            break;
        case ke_bookNext:
            if (playingEntry.parent < offset-1) {
                m = (struct MENUENTRY *)MenuGetEntry(playingEntry.parent+1);
                subtree = m->subtree;
                player.nextFile = subtree - offset;
            } else {
                player.nextFile = 0;
            }
            cs.cancel = 1;
            repeat = 0;
            prejump_file = player.currentFile;
            prejump_playtime = (u_int16)cs.playTimeSeconds;
            beep();
            break;
        case ke_OT_NT:
            m = (struct MENUENTRY *)MenuGetEntry(playingEntry.parent);
            parent = m->parent;
            if (++parent >= book1) {
                parent = 0;
            }
            m = (struct MENUENTRY *)MenuGetEntry(parent);
            subtree = m->subtree;
            while (subtree) {
                parent = subtree;
                m = (struct MENUENTRY *)MenuGetEntry(subtree);
                subtree = m->subtree;
            }
            player.nextFile = parent - offset;
            cs.cancel = 1;
            repeat = 0;
            prejump_file = player.currentFile;
            prejump_playtime = (u_int16)cs.playTimeSeconds;
            beep();
            break;
        case ke_previous:
            if (player.currentFile==0) player.nextFile = player.totalFiles - 1;
            else player.nextFile = player.currentFile - 1;
            cs.cancel = 1;
            repeat = 0;
            prejump_file = player.currentFile;
            prejump_playtime = (u_int16)cs.playTimeSeconds;
            beep();
            break;
        case ke_next:
            if (player.currentFile == player.totalFiles - 1) player.nextFile = 0;
            else player.nextFile = player.currentFile + 1;
            cs.cancel = 1;
            repeat = 0;
            prejump_file = player.currentFile;
            prejump_playtime = (u_int16)cs.playTimeSeconds;
            beep();
            break;
        case ke_pauseToggle:
            player.pauseOn ^= 1;
            PERIP(GPIO0_ODATA) ^= AMP;
            break;
        case ke_bookmark:
            beep();
            SpiWrite(BOOKMARKS + bookmark, player.currentFile);
            SpiWrite(BOOKMARKS + bookmark + 2, (u_int16)cs.playTimeSeconds);
            bookmark = (bookmark + 4) & 0x1f;
            break;
        case ke_markPrev:
            bookmark -= 8;
        case ke_markNext:
            beep();
            for (i = 20; i > 0; i-- ) BusyWait10();
            PERIP(GPIO0_ODATA) &= ~AMP; /* amp off */
            bkmk_pressed = 1;
            bookmark = (bookmark + 4) & 0x1f;
            player.nextFile = SpiRead(BOOKMARKS + bookmark);
            goTo = SpiRead(BOOKMARKS + bookmark + 2);
            cs.cancel = 1;
            repeat = 0;
            prejump_file = player.currentFile;
            prejump_playtime = (u_int16)cs.playTimeSeconds;
            break;
        case ke_repeat:
            player.nextFile = player.currentFile;
            repeat = 1;
            beep();
            break;
        case ke_resetBookmarks:
            beep();
            for (i = 0; i < 32; i += 2) {
                SpiWrite(BOOKMARKS + i, 0);
            }
            break;
        case ke_back:
            beep();
            for (i = 20; i > 0; i-- ) BusyWait10();
            PERIP(GPIO0_ODATA) &= ~AMP; /* amp off */
            bkmk_pressed = 1;
            player.nextFile = prejump_file;
            goTo = prejump_playtime;
            cs.cancel = 1;
            repeat = 0;
            break;
        default:
            RealKeyEventHandler(event);
    }
}

void MyUserInterfaceIdleHook(void) { /*94 words*/
    if (uiTrigger) {
        uiTrigger = 0;
        KeyScan9();
    }
}

auto void MyPowerOff(void) {
    register u_int16 i;
    SpiWrite(CONFIG + CHAPTER, player.currentFile); /* save current chapter */
    SpiWrite(CONFIG + SECONDS, (u_int16)cs.playTimeSeconds); /* and time */
    if (player.volume > VOL_MIN) player.volume = VOL_MIN;
    SpiWrite(CONFIG + VOLUME, player.volume);   /* save current volume */
    SpiWrite(CONFIG + BOOKMARK, bookmark);  /* save current bookmark */
    PERIP(INT_ENABLEL) &= ~INTF_TIM1;   /*Disable interrupt TIM1*/
    i = PERIP(GPIO0_ODATA);
    i &= ~AMP; /* amp off */
    i |= BAT_LED; /*LED off */
    PERIP(GPIO0_ODATA) = i;
    for (i = 50; i > 0; i--) BusyWait10();
    RealPowerOff();
}

void InterruptHandler_Timer1(void) {
    register u_int16 i;
    PERIP(INT_ENABLEL) &= ~INTF_TIM1;   /*Disable interrupt TIM1*/
    if (PERIP(SCI_STATUS) & SCISTF_REGU_POWERLOW) {
#ifdef USE_DEBUG
        puts("=LOW");
#endif
        i = battery_low - 1;
        if (!i) PowerOff(); /* check the 60seconds */
        if (i < 60) {
            beep();
            PERIP(GPIO0_ODATA) ^= BAT_LED;
        }
    } else {
        PERIP(GPIO0_ODATA) &= ~BAT_LED; /* switch it on */
        i = BATTERYLOWTIME;
#if DEBUG_LEVEL > 1
        puts("=GOOD");
#endif
    }
    battery_low = i;
    PERIP(INT_ENABLEL) |= INTF_TIM1;
}


void Initialize(void) {
    /* Configure GPIO for Amp Shutdown, Battery Status LED*/
    PERIP(GPIO0_MODE) &= ~(AMP | BAT_LED);
    PERIP(GPIO0_DDR) |= AMP | BAT_LED;
    PERIP(GPIO0_ODATA) &= ~(AMP | BAT_LED); /* amp off, LED on */

#ifndef USE_DEBUG
    /* Make UART pins output low to make invisible on USB bus */
    PERIP(GPIO1_MODE) &= ~(TX | RX);
    PERIP(GPIO1_DDR) |= TX | RX;
    PERIP(GPIO1_ODATA) &= ~(TX | RX);
#endif

    SPI_MASTER_8BIT_CSHI;
    PERIP(SPI0_FSYNC) = 0;
    PERIP(SPI0_CLKCONFIG) = SPI_CC_CLKDIV * (12-1); 
    PERIP(GPIO1_MODE) |= 0x0f; /* enable SPI pins */
    PERIP(INT_ENABLEL) &= ~INTF_RX; //Disable UART RX interrupt

    /* Adjust voltages */
    //voltages[voltIoPlayer]    = 27; /*3.3V for microSD card */
    voltages[voltAnaPlayer] = 16;/*3.0V for high-current MMC/SD!*/
    PowerSetVoltages(&voltages[voltCorePlayer]);
    BusyWait10();   /*make the regulator stable */
#ifdef USE_DEBUG
    puthex(PERIP(SCI_STATUS));
    puts("=SCI_STATUS");
#endif
    {
        register u_int16 i = 0; /*VHIGH >= 3.3V*/
        while (PERIP(SCI_STATUS) & SCISTF_REGU_POWERLOW) {
            i++;
            BusyWait10();
            if (i > 10) PowerOff();
        }
    }

    currentKeyMap = playModeMap;

    SetHookFunction((u_int16)KeyEventHandler, MyKeyEventHandler);
    SetHookFunction((u_int16)IdleHook, MyUserInterfaceIdleHook);
    SetHookFunction((u_int16)PowerOff, MyPowerOff);

    {
        register u_int16 low = SYSTEMMAINFREQ / BATTERYCHECKFREQ;
        register u_int16 high = SYSTEMMAINFREQ / BATTERYCHECKFREQ >> 16;

        PERIP(TIMER_T1L) = low; //default main clock is 6MHz See Page74
        PERIP(TIMER_T1H) = high; //change to 1Hz
        PERIP(TIMER_T1CNTL) = low; //Current Value Low
        PERIP(TIMER_T1CNTH) = high; //Current Value High
    }
    SetInterruptVector_Timer1();
    PERIP(TIMER_ENABLE) |= (1 << 1); //Enable timer 1

#if 1 /*Perform some extra inits because we are started from SPI boot. */
    InitAudio(); /* goto 3.0x..4.0x */
    PERIP(INT_ENABLEL) = INTF_RX | INTF_TIM0 | INTF_TIM1;
    PERIP(INT_ENABLEH) = INTF_DAC;
#endif

    SetHookFunction((u_int16)OpenFile, FatFastOpenFile); /*Faster!*/

#ifdef PATCH_LBAB
    /* Increases the allowed disk size from 4GB to 2TB.
        Reference to PatchMSCPacketFromPC will link the function from
        libdev1000.a. */
    SetHookFunction((u_int16)MSCPacketFromPC, PatchMSCPacketFromPC);
#endif/*PATCH_LBAB*/

    /* Replicate main loop */
    player.volume = -24;
    player.volumeOffset = -24;
    keyOld = KEY_POWER;
    keyOldTime = -32767; /* ignores the first release of KEY_POWER */

    bookmark = BKMK_FIRST;

    PERIP(GPIO0_MODE) &= ~(MMC_MISO|MMC_CLK|MMC_MOSI|MMC_XCS);
    PERIP(GPIO0_DDR) = (PERIP(GPIO0_DDR) & ~(MMC_MISO)) | (MMC_CLK|MMC_MOSI|MMC_XCS);
    PERIP(GPIO0_ODATA) = (PERIP(GPIO0_ODATA) & ~(MMC_CLK|MMC_MOSI)) | (MMC_XCS | GPIO0_CS1); /* NFCE high */
#if DEBUG_LEVEL > 1
    puts("Configured MMC pins\n");
#endif
    memset(&mmc, 0, sizeof(mmc));
    map = &mmcMapper;
    PlayerVolume();
}


void main(void) {
    register const struct MENUENTRY *m;

#ifdef USE_DEBUG
    puts("Entered main()");
#endif

    Initialize();

    {   // Check button lock and power off if locked
        register u_int16 i;
        PERIP(GPIO0_MODE) &= ~KEY_8;
        PERIP(GPIO0_DDR) &= ~KEY_8;
        for (i=0;i<100;i++) PERIP(GPIO0_IDATA);
        if ((PERIP(GPIO0_IDATA) & KEY_8) == 0) RealPowerOff();
    }

    while (1) {
    /* If MMC is not available, try to reinitialize it. */
        if (mmc.state == mmcNA || mmc.errors) {
#ifdef USE_DEBUG
            puts("InitializeMmc(50)");
#endif
            InitializeMmc(50);
        }

#ifdef USE_DEBUG
        puts("Try to init FAT...");
#endif
    /* Try to init FAT. */
        if (InitFileSystem() == 0) {
#ifdef USE_DEBUG
            puts("FAT init ok.");
#endif

#ifdef USE_DEBUG
            puts("Start MenuInit()...");
#endif
            if (MenuInit()) {
            /* no menu found */
#ifdef USE_DEBUG
                puts("No menu found.");
#endif
                while (1) {;} // No menu found
            }
#ifdef USE_DEBUG
            puts("Done MenuInit()...");
#endif

            /* Restore the default suffixes. */
            minifatInfo.supportedSuffixes = oggFiles;
            player.totalFiles = OpenFile(0xffffU);

            if (player.totalFiles == 0) {
                /* If no files found, output some samples.
                This causes the idle hook to be called, which in turn
                scans the keys and you can turn off the unit if you like.
                */
#ifdef USE_DEBUG
                puts("no .ogg files");
#endif
                goto noFSnorFiles;
            }

            /* Determine offset, i.e. index of first file */
            m = (struct MENUENTRY *)MenuGetEntry(0);
            book1 = m->subtree;
            m = (struct MENUENTRY *)MenuGetEntry(book1);
            offset = m->subtree;

            {
                register u_int16 subtree, offsetlastbook = offset - 1;
                m = (struct MENUENTRY *)MenuGetEntry(offsetlastbook);
                subtree = m->subtree;
                do {
                    m = (struct MENUENTRY *)MenuGetEntry(subtree);
                    subtree++;
                } while (m->parent == offsetlastbook);
                if (player.totalFiles > (subtree - offset)) player.totalFiles = subtree - offset;
            }

            player.pauseOn = 0;
            player.nextStep = 1;
#ifdef USE_DEBUG
            puts("About to read eeprom...");
#endif
            player.nextFile = SpiRead(CONFIG + CHAPTER);    /* read saved chapter */
            goTo = SpiRead(CONFIG + SECONDS);       /* read saved playTimeSeconds */
            player.volume = SpiRead(CONFIG + VOLUME);   /* read saved volume */
            if (player.volume > VOL_MIN) player.volume = VOL_MIN;
            bookmark = SpiRead(CONFIG + BOOKMARK) & 0x1C;// read saved bookmark
#ifdef USE_DEBUG
            puthex(player.nextFile); puts("=SpiRead");
#endif
            while (1) {
                PERIP(GPIO0_ODATA) |= AMP; /* amp on */
                player.currentFile = player.nextFile;
                if ((player.currentFile < 0) || (player.currentFile >= player.totalFiles)) {
#ifdef USE_DEBUG
                    puthex(player.currentFile); puts("=player.currentFile (< 0)");
#endif
                    player.currentFile = 0;
                }
                player.nextFile = player.currentFile + 1 - repeat;

                /* If the file can be opened, start playing it. */
                if (OpenFile(player.currentFile) < 0) {
                    player.ffCount = 0;
                    cs.cancel = 0;
                    cs.goTo = goTo; /* start playing from saved place */
                    cs.fileSize = cs.fileLeft = minifatInfo.fileSize;
                    cs.fastForward = 1; /* reset play speed to normal */
#ifdef USE_DEBUG
                    puthex(player.currentFile); puts("=player.currentFile");
#endif
                    m = (struct MENUENTRY *)MenuGetEntry(player.currentFile + offset);
                    memcpy(&playingEntry, m, sizeof(playingEntry));

                    {
                        register s_int16 oldStep = player.nextStep;
                        register s_int16 ret;

                        ret = PlayCurrentFile();

                        /* If unsupported, keep skipping */
                        if (ret == ceFormatNotFound) player.nextFile = player.currentFile + player.nextStep;
                        /* If a supported file found, restore play direction */
                        if (ret == ceOk && oldStep == -1) player.nextStep = 1;
                    }
                }
                /* Leaves play loop when MMC changed */
                if (mmc.state == mmcNA || mmc.errors) break;

                if (bkmk_pressed) {
                    bkmk_pressed = 0;
                } else {
                    goTo = -1;
                }
            }
        } else {
        /* If not a valid FAT (perhaps because MMC/SD is not inserted), 
        just send some samples to audio buffer and try again. */
noFSnorFiles:
#ifdef USE_DEBUG
            puts("FAT init failed.");
#endif
            LoadCheck(&cs, 32); /* decrease or increase clock */
            PERIP(GPIO0_ODATA) ^= BAT_LED;  /* flash LED */
        }
    }
}



