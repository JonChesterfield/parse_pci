#!/bin/bash

CC=`which clang`
echo $CC
$CC reference.c -c -o reference.o
$CC main.c -c -o main.o

$CC main.o reference.o -o main -lpci

./main
