#! /usr/bin/env python
# encoding: utf-8

# Author: Paul Giblock
# Forked from official compiler_c.py

import os,sys,imp,types
from waflib.Tools import ccroot
from waflib import Utils,Configure
from waflib.Logs import debug
mpi_c_compilers=['mpicc']
def configure(conf):
    try:test_for_compiler=conf.options.check_mpi_c_compiler
    except AttributeError:conf.fatal("Add options(opt): opt.load('compiler_mpi_c')")
    for compiler in test_for_compiler.split():
        conf.env.stash()
        conf.start_msg('Checking for %r (MPI c compiler)'%compiler)
        try:
            conf.load(compiler)
        except conf.errors.ConfigurationError ,e:
            conf.env.revert()
            conf.end_msg(False)
            debug('compiler_mpi_c: %r'%e)
        else:
            if conf.env['MPI_CC']:
                conf.end_msg(conf.env.get_flat('MPI_CC'))
                conf.env['COMPILER_MPI_CC']=compiler
                break
            conf.end_msg(False)
    else:
        conf.fatal('could not configure an MPI c compiler!')
def options(opt):
    global mpi_c_compiler
    build_platform=Utils.unversioned_sys_platform()
    test_for_compiler=' '.join(mpi_c_compilers)
    mpicc_compiler_opts=opt.add_option_group("MPI C Compiler Options")
    mpicc_compiler_opts.add_option('--check-mpi-c-compiler',default="%s"%test_for_compiler,help='On this platform (%s) the following MPI C-Compiler will be checked by default: "%s"'%(build_platform,test_for_compiler),dest="check_mpi_c_compiler")
    for x in test_for_compiler.split():
        opt.load('%s'%x, tooldir='waf-tools')
