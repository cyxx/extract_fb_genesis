
CPPFLAGS += -fPIC -Wall -Wpedantic

fb_decode.so: bitmap.o decode.o decode_lev.o decode_spc.o unpack.o
	$(CC) -shared -o $@ $^

clean:
	rm *.so *.o
