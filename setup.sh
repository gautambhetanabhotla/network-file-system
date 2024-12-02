#!/bin/bash

make reset
make clean
make
for i in {1..9}; do
	cp ss testdir/$i
	cp ss store/$i
done
