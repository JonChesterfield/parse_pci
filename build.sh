#!/bin/bash

CC=`which clang`
DIS=`which llvm-dis`
OPT=`which opt`

CFLAG="-g -O2 -Wall -Wextra"
$CC $CFLAG reference.c -c -o reference.o
$CC $CFLAG main.c -c -emit-llvm -o main.bc
$OPT -strip-debug main.bc -S -o main.ll

$CC main.bc reference.o -o main -lpci

time ./main
