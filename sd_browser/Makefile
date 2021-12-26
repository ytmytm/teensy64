all: sdbrowser.prg

sdbrowser.prg:
	cl65 -t c64 -Oi screen.c browser.c dir.c base.c loader.s -o sdbrowser.prg
	xxd -i sdbrowser.prg > ../Teensy64/sdbrowser.h

clean:
	rm -rf *.o sdbrowser.prg
