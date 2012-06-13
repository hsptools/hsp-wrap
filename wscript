#!/usr/bin/env python
import os
import shutil
from waflib import Logs, Options, Utils
from pprint import pprint

# Variables for 'waf dist'
APPNAME = 'hsp'
VERSION = '0.1.0'

# Mandatory variables
top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_c compiler_cxx')

    opt.add_option('--debug', action='store_true', help='Build programs with debug flags enabled')

def configure(conf):
    conf.load('compiler_c compiler_cxx')

    # Warn about almost anything
    conf.env.append_unique('CFLAGS', ['-Wall'])

    # Locate any programs needed for the configuration process
    mysql_config = conf.find_program('mysql_config', var='MYSQL_CONFIG', mandatory=False)

    # zlib
    conf.check_cfg(package='zlib', atleast_version='1.2.3',
            args=['--cflags', '--libs'])

    # libexpat1
    conf.check_cc(lib='expat', header_name='expat.h', uselib_store='EXPAT',
            msg="Checking for 'Expat'")

    # MySQL
    if mysql_config:
        mysql_cflags  = conf.cmd_and_log([mysql_config,'--include']).split()
        mysql_lflags  = conf.cmd_and_log([mysql_config,'--libs']).split()
        mysql_version = conf.cmd_and_log([mysql_config,'--version'])

        conf.env['MYSQL_VERSION'] = mysql_version
        conf.check_cc(lib='mysqlclient', header_name='mysql.h', uselib_store='MYSQL',
                cflags=mysql_cflags, linkflags=mysql_lflags,
                msg="Checking for 'MySQL'")

    else:
        # TODO: Move to a summary after the configuration process
        Logs.warn('MySQL library could not be found.  Database related tools will not be built.')
        
    conf.write_config_header('src/config.h')

def build(bld):
    bld.recurse('src')

