#!/bin/bash

CC=`which clang`
DIS=`which llvm-dis`
OPT=`which opt`

CFLAG="-g -O2 -Wall -Wextra"
$CC $CFLAG pci_ids.c -c -emit-llvm -o pci_ids.bc
$CC $CFLAG test.c -c -o test.o
$OPT -strip-debug pci_ids.bc -S -o pci_ids.ll

$CC pci_ids.bc test.o -o pci_ids -lpci

time ./pci_ids
