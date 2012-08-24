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
    opt.add_option('--num-cores', action='store', type='int', default=12, help='Number of cores to utilize in wrapper')
    opt.add_option('--result-buffer-size', action='store', type='int', default=(1<<27), help='Size of wrapper output buffer')

def configure(conf):
    conf.load('compiler_c compiler_cxx')
    conf.load('compiler_mpi_c', tooldir=tool_dir)

    # Warn about almost anything
    conf.env.append_unique('CFLAGS', ['-Wall'])

    # Locate any programs needed for the configuration process
    mysql_config = conf.find_program('mysql_config', var='MYSQL_CONFIG', mandatory=False)

    # zlib
    try:
        conf.check_cfg(package='zlib', atleast_version='1.2.3',
                args=['--cflags', '--libs'])
    except:
        conf.check_cc(lib='z', header_name='zlib.h', function_name='inflate',
                uselib_store='ZLIB', define_name='HAVE_ZLIB',
                msg="Checking for any 'zlib'")

    # std Math
    conf.check_cc(lib='m', header_name='math.h', function_name='sinf',
            uselib_store='M', define_name='HAVE_MATH',
            msg="Checking for 'libm' (math library)")

    # libexpat1
    conf.check_cc(lib='expat', header_name='expat.h', function_name='XML_ParserCreate',
            uselib_store='EXPAT', define_name='HAVE_EXPAT',
            msg="Checking for 'Expat'")

    # MySQL
    if mysql_config:
        conf.check_cfg(path=mysql_config, args='--include --libs',
                package='', uselib_store='MYSQL', msg="Checking for 'MySQL'")

        conf.env['VERSION_MYSQL'] = conf.cmd_and_log([mysql_config,'--version'])
    else:
        # TODO: Move to a summary after the configuration process
        Logs.warn('MySQL library could not be found.  Database related tools will not be built.')

    # nftw from ftw.h (File Tree Walk)
    conf.env.stash()
    try:
        conf.env.DEFINES = ['_XOPEN_SOURCE=500']
        conf.check_cc(header_name='ftw.h', function_name='nftw',
                uselib_store='FTW')
    finally:
        conf.env.revert()

    # Defines
    conf.define('MCW_NCORES',          conf.options.num_cores)
    conf.define('MCW_RESULTBUFF_SIZE', conf.options.result_buffer_size)
    conf.define('HSP_VERSION',         VERSION)
    conf.write_config_header('hsp-config.h')

def build(bld):
    bld.recurse('src')

