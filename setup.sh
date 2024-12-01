#!/bin/bash

make
for i in {1..9}; do
	cp ss testdir/$i
done
