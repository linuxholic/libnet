
VPATH = src:examples

CFLAGS += -g -Isrc
LUAFLAGS = $(CFLAGS) -Ipuc-lua/include -Lpuc-lua/lib
LIBS = -llua -lm -ldl

CORE := net.c util.c hash.c
BINS := http-server http-client tcp-relay socks4 hello timer hello-lua

all: $(BINS)

http-server: http-server.c http.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

http-client: http-client.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

broken-client: broken-client.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

tcp-relay: tcp-relay.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

socks4: socks4.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

timer: timer.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

hello: hello-server.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

hello-lua: hello-lua.c $(CORE)
	gcc $(LUAFLAGS) $^ -o bin/$@ $(LIBS)

raft-server: raft-server.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

raft-client: raft-client.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

raft-log: raft-log.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

hash-demo: hash-demo.c $(CORE)
	gcc $(CFLAGS) $^ -o bin/$@

clean:
	rm -f bin/*

gdb-server:
	gdb --args bin/http-server 127.0.0.1 8889

.PHONY: all clean
