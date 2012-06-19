#!/usr/bin/env python

import sys, re

f = open(sys.argv[1], 'r')

re_mol  = re.compile('Molecule: (ZINC[0-9]*)')
re_fail = re.compile('ERROR:  Could not complete growth.')

mol = 'None'

for l in f:
	m_mol = re_mol.search(l)
	if m_mol:
		mol   = m_mol.group(1)
	
	m_fail = re_fail.search(l)
	if m_fail and mol:
		print mol
