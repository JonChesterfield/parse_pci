#!/bin/bash

CC=`which clang`
DIS=`which llvm-dis`
OPT=`which opt`

CFLAG="-g -O0 -Wall -Wextra -Wpedantic -std=c99"
$CC $CFLAG pci_ids_tested.c -c -emit-llvm -o pci_ids.bc
$CC $CFLAG regression.c -c -o regression.o
$OPT -strip-debug pci_ids.bc -S -o pci_ids.ll

$CC pci_ids.bc regression.o -o pci_ids -lpci

time ./pci_ids
exit 0
time valgrind --leak-check=no ./pci_ids
