#include <stdio.h>
#include <string.h>
#include <vs1000.h>
#include <fat.h>
#include <player.h>
#include <dev1000.h>

#define PAGESIZE        32
#define SECTORSIZE      512

#define DEBUG

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

void SingleCycleCommand(u_int16 cmd){
    SPI_MASTER_8BIT_CSHI; 
    SpiDelay(0);
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(cmd);
    SPI_MASTER_8BIT_CSHI;
    SpiDelay(0);
}

/// Wait for not_busy (status[0] = 0) and return status
void SpiWaitStatus(void) {

    SPI_MASTER_8BIT_CSHI;
    SpiDelay(0);
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_READ_STATUS_REGISTER); 
    while (SpiSendReceive(0xff) & 0x01) SpiDelay(0);
    SPI_MASTER_8BIT_CSHI; 
}

void SpiWriteBlock(register u_int16 blockn, register u_int16 *dptr) {
    u_int16 i;
    register u_int16 addr = blockn*512;

    for (i = SECTORSIZE/(PAGESIZE); i > 0; i--){
        SingleCycleCommand(SPI_EEPROM_COMMAND_WRITE_ENABLE);
        SPI_MASTER_8BIT_CSLO;
        SpiSendReceive(SPI_EEPROM_COMMAND_WRITE);
        SPI_MASTER_16BIT_CSLO;
        SpiSendReceive(addr);
        {
            register u_int16 j;
            for (j = PAGESIZE/2; j > 0; j--) SpiSendReceive(*dptr++);
        }
        SpiWaitStatus();
        addr += PAGESIZE;
    }
}

u_int16 SpiReadBlock(register u_int16 blockn, register u_int16 *dptr) {
    SpiWaitStatus();
    SPI_MASTER_8BIT_CSLO;
    SpiSendReceive(SPI_EEPROM_COMMAND_READ);
    SPI_MASTER_16BIT_CSLO;
    SpiSendReceive(blockn << 9);    // Address[15:8]  = blockn[6:0]0
                                    // Address[7:0]   = 00000000
    {
        register u_int16 i;
        for (i = SECTORSIZE/2; i > 0; i--) *dptr++ = SpiSendReceive(0);
    }
    SPI_MASTER_8BIT_CSHI;
    return 0;
}

#ifdef DEBUG
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

void WriteEEPROM(void) {
    FILE *fp;
    PERIP(INT_ENABLEL) &= ~INTF_RX;
#ifdef DEBUG
    puts("Trying to open eeprom.img");
#endif
    if (fp = fopen ("eeprom.img", "rb")) {
        u_int16 len;
        u_int16 sectorNumber = 0;

#ifdef DEBUG
        puts("Erase first sector...");
#endif
        memset(minifatBuffer, 0, SECTORSIZE/2);
        SpiWriteBlock(sectorNumber, minifatBuffer);
        SpiReadBlock(0, minifatBuffer);
#ifdef DEBUG
        puthex(minifatBuffer[0]); 
        puts("\n");
#endif
        if (minifatBuffer[0] != 0x0000) {
#ifdef DEBUG
            puts("Can't erase EEPROM!");
#endif
            return;
        }
#ifdef DEBUG
        puts("Programming...");
#endif
        while ((len = fread(minifatBuffer, 1, SECTORSIZE/2, fp))){
            fputs("Sector ", stdout);
            puthex(sectorNumber);
            puts("...");
            SpiWriteBlock(sectorNumber, minifatBuffer);
            sectorNumber++;
        }
        fclose(fp);  // Programming complete.

        minifatBuffer[0]=0;
#ifdef DEBUG
        fputs("Reading first 2 words of EEPROM: ", stdout);
#endif
        SpiReadBlock(0, minifatBuffer);
#ifdef DEBUG
        puthex(minifatBuffer[0]); 
        puthex(minifatBuffer[1]);
        fputs(" (\"", stdout);
        putchar(minifatBuffer[0] >> 8);
        putchar(minifatBuffer[0] & 0xff);
        putchar(minifatBuffer[1] >> 8);
        putchar(minifatBuffer[1] & 0xff);
        if ((minifatBuffer[0] == 0x564c) && (minifatBuffer[1] == 0x5349)) {
            puts("\"), which is a valid VLSI boot id.");
        } else {
            puts("\"), which is NOT a valid VLSI boot id!");
        }
        puts("Done.");
#endif
    } else {
#ifdef DEBUG
        puts("File not found\n");
#endif
    }
    PERIP(INT_ENABLEL) |= INTF_RX;
}

void main(void) {
#ifdef DEBUG
    puts("Entered main()");
#endif

    SPI_MASTER_8BIT_CSHI;
    PERIP(SPI0_FSYNC) = 0;
    PERIP(SPI0_CLKCONFIG) = SPI_CC_CLKDIV * (12-1); 
    PERIP(GPIO1_MODE) |= 0x1f;      // enable SPI pins
    PERIP(INT_ENABLEL) &= ~INTF_RX; // disable UART RX interrupt

    PERIP(INT_ENABLEL) |= INTF_RX | INTF_TIM0;

    WriteEEPROM();
}

