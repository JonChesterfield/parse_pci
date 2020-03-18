#!/bin/bash

CC=`which clang`
echo $CC
CFLAG="-g -O2 -Wall -Wextra"
$CC $CFLAG reference.c -c -o reference.o
$CC $CFLAG main.c -c -o main.o

$CC main.o reference.o -o main -lpci

time ./main
