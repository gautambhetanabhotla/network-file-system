CC := /usr/bin/gcc
CFLAGS := -g

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
	$(CC) $(CFLAGS) -c $< -o $@ -lao

build/naming-server/%.o: src/naming-server/%.c | build/naming-server
	$(CC) $(CFLAGS) -c $< -o $@

build/storage-server/%.o: src/storage-server/%.c | build/storage-server
	$(CC) $(CFLAGS) -c $< -o $@

c: $(patsubst src/client/%.c, build/client/%.o, $(wildcard src/client/*.c)) | build/client
	$(CC) $(CFLAGS) -o $@ $^ -lao

ns: $(patsubst src/naming-server/%.c, build/naming-server/%.o, $(wildcard src/naming-server/*.c)) | build/naming-server
	$(CC) $(CFLAGS) -o $@ $^

ss: $(patsubst src/storage-server/%.c, build/storage-server/%.o, $(wildcard src/storage-server/*.c)) | build/storage-server
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -rf build c ns ss a.out

.PHONY: reset
reset:
	rm -rf storage paths.txt *.bin

req:
	gcc tests/send_req_to_ss.c
	./a.out 127.0.0.1 3001
	rm -f a.out