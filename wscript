#!/usr/bin/env python
import os
import shutil
from waflib import Context, Logs, Options, Utils
from pprint import pprint

# Variables for 'waf dist'
APPNAME = 'hsp'
VERSION = '0.1.0'

# Mandatory variables
top = '.'
out = 'build'
# Optional variables
tool_dir = 'waf-tools'

def options(opt):
    opt.load('compiler_c compiler_cxx')
    opt.load('compiler_mpi_c', tooldir=tool_dir)

    opt.add_option('--debug', action='store_true', help='Build programs with debug flags enabled')

def configure(conf):
    conf.load('compiler_c compiler_cxx')
    conf.load('compiler_mpi_c', tooldir=tool_dir)

    # Warn about almost anything
    conf.env.append_unique('CFLAGS', ['-Wall'])

    # Locate any programs needed for the configuration process
    mysql_config = conf.find_program('mysql_config', var='MYSQL_CONFIG', mandatory=False)

    # zlib
    conf.check_cfg(package='zlib', atleast_version='1.2.3',
            args=['--cflags', '--libs'])

    # std Math
    conf.check_cc(lib=['m'], uselib_store='M',
            msg="Checking for 'libm' (math library)")

    # libexpat1
    conf.check_cc(lib='expat', header_name='expat.h', uselib_store='EXPAT',
            msg="Checking for 'Expat'")

    # MySQL
    if mysql_config:
        conf.check_cfg(path=mysql_config, args='--include --libs',
                package='', uselib_store='MYSQL', msg="Checking for 'MySQL'")

        conf.env['VERSION_MYSQL'] = conf.cmd_and_log([mysql_config,'--version'])
    else:
        # TODO: Move to a summary after the configuration process
        Logs.warn('MySQL library could not be found.  Database related tools will not be built.')

    conf.write_config_header('src/config.h')

def build(bld):
    bld.recurse('src')

