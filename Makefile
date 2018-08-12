BIN      = tools/vskit130/bin
CC       = $(BIN)/vcc
ASM      = $(BIN)/vsa
LINK     = $(BIN)/vslink
LIBS     = tools/vs1000b-lib
COFF2SPI = $(BIN)/coff2spiboot
CCFLAGS  = -P130 -O6 -fsmall-code

export PATH := $(BIN):$(PATH)

all: eeprom.img

eeprom.img: osab.bin prommer.bin $(COFF2SPI)
	$(COFF2SPI) -x 0x50 $< $@

osab.bin: osab.o timer1int.o
	$(LINK) -k -m mem_user -o $@ -L $(LIBS) -lc -ldev1000 $(LIBS)/c-spi.o $(LIBS)/rom1000.o $^

osab.o: osab.c | toolchain
	$(CC) $(CCFLAGS) -I $(LIBS) -o $@ $<

timer1int.o: tools/timerexample/timer1int.s | toolchain
	$(ASM) -o $@ $< -I $(LIBS)

prommer.bin: prommer.o | toolchain
	$(LINK) -k -m mem_user -o $@ -L $(LIBS) -lc $< $(LIBS)/c-spi.o $(LIBS)/rom1000.o

prommer.o: prommer.c | toolchain
	$(CC) $(CCFLAGS) -I $(LIBS) -o $@ $<

$(BIN)/coff2spiboot: | toolchain
	sed -i 's/\o32//g' tools/vskit134b/bin/src/coff2spiboot.c
	gcc -o $@ tools/vskit134b/bin/src/coff2spiboot.c

toolchain: | tools/vs1000b-lib tools/vskit130

tools:
	mkdir $@

tools/vs1000b-lib: | tools
	curl -O http://www.vlsi.fi/fileadmin/software/VS1000/vs1000b-lib-20110428.zip
	unzip vs1000b-lib-20110428.zip -d tools
	rm -f vs1000b-lib-20110428.zip
	patch tools/vs1000b-lib/player.h player.h.patch

tools/vskit130: | tools
	curl -O http://www.vlsi.fi/fileadmin/software/VS10XX/vskit130_linux_free_i386.tar.gz
	tar xfv vskit130_linux_free_i386.tar.gz -C tools
	rm -f vskit130_linux_free_i386.tar.gz
	curl -O http://www.vlsi.fi/fileadmin/software/VS1000/vskit134b.zip
	unzip vskit134b.zip -d tools
	rm -f vskit134b.zip
	cp -v tools/vskit134b/vs1000bc/hw_desc .
	cp -v tools/vskit134b/vs1000bc/mem_desc .
	cp -v tools/vskit134b/vs1000bc/mem_user .
	cp -v tools/vskit134b/vs1000bc/e.cmd .

tools/timerexample/timer1int.s: | tools
	curl -O http://www.vlsi.fi/fileadmin/software/VS1000/timerexample.zip
	unzip timerexample.zip -d tools
	rm -f timerexample.zip

upload: eeprom.img
	vs3emu -chip vs1000 -s 115200 -l prommer.bin e.cmd

clean:
	rm -f *.a *.o *.bin *.img

very-clean: clean
	rm -fr tools
	rm -f *_desc* mem_user e.cmd *.zip

