# Makefile
objs = watermark.o bitops.o wfile.c

watermark: Makefile $(objs)
	cc -Wall -o watermark $(objs) -lpng -DDEBUG

watermark.o: watermark.c
	cc -Wall -c watermark.c -DDEBUG
	#cc -Wall -c watermark.c

wfile.o: bitops.h wfile.c wfile.h
	cc -Wall -c wfile.c -DDEBUG

bitops.o: bitops.c bitops.h
	cc -Wall -c bitops.c -DDEBUG

clean:
	rm -f  watermark *.o *~
