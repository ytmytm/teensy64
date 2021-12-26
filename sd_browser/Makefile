all: sdbrowser.h

browser.prg:
	cl65 -t c64 -Oi screen.c browser.c dir.c base.c loader.s -o browser.prg

sdbrowser.h: browser.prg
	xxd -i browser.prg > sdbrowser.h

clean:
	rm -rf *.o browser.prg
