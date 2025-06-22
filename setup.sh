#!/bin/bash

make reset
make clean
make
mkdir -p store
for i in {1..9}; do
	# cp ss testdir/$i
	mkdir -p store/$i
	cp ss store/$i
done
