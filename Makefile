CC := /usr/bin/gcc

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
	$(CC) -c $< -o $@

build/naming-server/%.o: src/naming-server/%.c | build/naming-server
	$(CC) -c $< -o $@

build/storage-server/%.o: src/storage-server/%.c | build/storage-server
	$(CC) -c $< -o $@

c: $(patsubst src/client/%.c, build/client/%.o, $(wildcard src/client/*.c)) | build/client
	$(CC) -o $@ $^

ns: $(patsubst src/naming-server/%.c, build/naming-server/%.o, $(wildcard src/naming-server/*.c)) | build/naming-server
	$(CC) -o $@ $^

ss: $(patsubst src/storage-server/%.c, build/storage-server/%.o, $(wildcard src/storage-server/*.c)) | build/storage-server
	$(CC) -o $@ $^

.PHONY: clean
clean:
	rm -rf build
	rm c ns ss