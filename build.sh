#!/bin/bash

CC=`which clang`
DIS=`which llvm-dis`
OPT=`which opt`

CFLAG="-g -O2 -Wall -Wextra"
$CC $CFLAG reference.c -c -o reference.o
$CC $CFLAG pci_ids.c -c -emit-llvm -o pci_ids.bc
$OPT -strip-debug pci_ids.bc -S -o pci_ids.ll

$CC pci_ids.bc reference.o -o pci_ids -lpci

time ./pci_ids
