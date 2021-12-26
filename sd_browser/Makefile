all: sdbrowser.h

sdbrowser.prg:
	cl65 -t c64 -Oi screen.c browser.c dir.c base.c loader.s -o sdbrowser.prg

sdbrowser.h: sdbrowser.prg
	xxd -i sdbrowser.prg > sdbrowser.h

clean:
	rm -rf *.o sdbrowser.prg sdbrowser.h
