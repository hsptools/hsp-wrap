#!/usr/bin/env python

import sys, re

def stripped(f):
    for l in f:
        yield l.strip()

####

mol2_in = open(sys.argv[1], 'r')
names_in = open(sys.argv[2], 'r')

names = set(stripped(names_in))

buf = ''
keep = False
lnum = 0
mnum = 0

for l in mol2_in:
    # New molecule
    if l.startswith('@<TRIPOS>MOLECULE'):
        mnum += 1
        # Last one was a keeper, print it out
        if keep:
            print buf
        # Starting a new buffer
        buf  = ''
        keep = False
        lnum = 0

    # Check if keeper
    if lnum==1 and l.strip() in names:
        keep = True

    # Append to buffer
    buf  += l
    lnum += 1

