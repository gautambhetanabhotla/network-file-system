CC := /usr/bin/gcc
CFLAGS := -g -Wall

.PHONY: all
all: ns ss c

build:
	mkdir build

build/client: | build
	mkdir build/client

build/naming-server: | build
	mkdir build/naming-server

build/storage-server: | build
	mkdir build/storage-server

build/client/%.o: src/client/%.c | build/client
	$(CC) $(CFLAGS) -c $< -o $@

build/naming-server/%.o: src/naming-server/%.c | build/naming-server
	$(CC) $(CFLAGS) -c $< -o $@

build/storage-server/%.o: src/storage-server/%.c | build/storage-server
	$(CC) $(CFLAGS) -c $< -o $@

c: $(patsubst src/client/%.c, build/client/%.o, $(wildcard src/client/*.c)) | build/client
	$(CC) $(CFLAGS) -o $@ $^

ns: $(patsubst src/naming-server/%.c, build/naming-server/%.o, $(wildcard src/naming-server/*.c)) | build/naming-server
	$(CC) $(CFLAGS) -o $@ $^

ss: $(patsubst src/storage-server/%.c, build/storage-server/%.o, $(wildcard src/storage-server/*.c)) | build/storage-server
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -rf build c ns ss a.out

req:
	gcc tests/send_req_to_ss.c
	./a.out 192.168.169.210 8080
	rm -f a.out