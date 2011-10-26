# Makefile
objs = watermark.o bitops.o wfile.c

watermark: Makefile $(objs)
	cc -Wall -o watermark $(objs) -lpng -DDEBUG

watermark.o: watermark.c
	cc -Wall -c watermark.c
	#cc -Wall -c watermark.c

wfile.o: bitops.h wfile.c wfile.h
	cc -Wall -c wfile.c

bitops.o: bitops.c bitops.h
	cc -Wall -c bitops.c

clean:
	rm -f  watermark *.o *~
