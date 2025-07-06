CC := /usr/bin/gcc
CFLAGS := -g

.PHONY: all
all: ns ss c

build:
	mkdir -p build

build/client: | build
	mkdir -p build/client

build/naming-server: | build
	mkdir -p build/naming-server

build/storage-server: | build
	mkdir -p build/storage-server

build/lib: | build
	mkdir -p build/lib

build/client/%.o: src/client/%.c | build/client
	$(CC) $(CFLAGS) -c $< -o $@ -lao

build/lib/%.o: src/lib/%.c | build/lib
	$(CC) $(CFLAGS) -c $< -o $@

build/naming-server/%.o: src/naming-server/%.c | build/naming-server
	$(CC) $(CFLAGS) -c $< -o $@

build/storage-server/%.o: src/storage-server/%.c | build/storage-server
	$(CC) $(CFLAGS) -c $< -o $@

c: $(patsubst src/client/%.c, build/client/%.o, $(wildcard src/client/*.c)) $(patsubst src/lib/%.c, build/lib/%.o, $(wildcard src/lib/*.c)) | build/client
	$(CC) $(CFLAGS) -o $@ $^ -lao

ns: $(patsubst src/naming-server/%.c, build/naming-server/%.o, $(wildcard src/naming-server/*.c)) $(patsubst src/lib/%.c, build/lib/%.o, $(wildcard src/lib/*.c)) | build/naming-server
	$(CC) $(CFLAGS) -o $@ $^

ss: $(patsubst src/storage-server/%.c, build/storage-server/%.o, $(wildcard src/storage-server/*.c)) $(patsubst src/lib/%.c, build/lib/%.o, $(wildcard src/lib/*.c)) | build/storage-server
	$(CC) $(CFLAGS) -o $@ $^

.PHONY: clean
clean:
	rm -rf build c ns ss a.out

.PHONY: reset
reset:
	rm -rf storage paths.txt *.bin

.PHONY: test
test:
	gcc tests/read.c src/lib/*.c
	./a.out 127.0.0.1 3001
	rm -f a.out