# Makefile
objs = watermark.o bitops.o wmf.c

watermark: Makefile $(objs)
	cc -Wall -o watermark $(objs) -lgd -DDEBUG

watermark.o: watermark.c
	cc -Wall -c watermark.c -DDEBUG
	#cc -Wall -c watermark.c

wmf.o: bitops.h wmf.c wmf.h
	cc -Wall -c wmf.c -DDEBUG

bitops.o: bitops.c bitops.h
	cc -Wall -c bitops.c -DDEBUG

clean:
	rm -f  watermark *.o *~
