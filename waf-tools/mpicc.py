#! /usr/bin/env python
# encoding: utf-8

import os,sys
from waflib import Configure,Options,Utils
from waflib.Tools import ccroot,ar
from waflib.Configure import conf

mpi_test = '''
#include <mpi.h>
int main (int argc, char **argv) {
  MPI_Init(&argc, &argv);
  MPI_Finalize();
}
'''

def find_mpi_cc(conf):
	cc=conf.find_program(['mpicc','cc'],var='MPI_CC')
	cc=conf.cmd_to_list(cc)
	
	# Verify actual MPI implementation
	conf.env.stash()
	conf.env.CC = cc
	conf.env.LINK_CC = cc
	conf.check_cc(fragment=mpi_test)
	conf.env.revert()

	# MPI variables in global ConfigSet
	conf.env.MPI_CC_NAME='mpicc'
	conf.env.MPI_CC=cc

	# An MPI-Specific ConfigSet
	conf.setenv('mpicc', conf.env)
	conf.env.CC = cc
	conf.env.LINK_CC = cc
	conf.setenv('')
	conf.env.CFLAGS = '-Wextra'

def configure(conf):
	conf.find_mpi_cc()

conf(find_mpi_cc)
