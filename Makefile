all: client naming-server storage-server

client: client.o
	gcc -o client client.o

naming-server: naming-server.o
	gcc -o naming-server naming-server.o

storage-server: storage-server.o
	gcc -o storage-server storage-server.o

unit-tests: unit-tests.o