#!/usr/bin/env python
import os
import shutil
from waflib import Logs, Options, Utils

# Variables for 'waf dist'
APPNAME = 'lmms.lv2'
VERSION = '0.1.0'

# Mandatory variables
top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_c')

    opt.add_option('--debug', action='store_true', help='Build programs with debug flags enabled')

def configure(conf):
    conf.load('compiler_c')

    # Warn about almost anything
    conf.env.append_unique('CFLAGS', ['-Wall'])

    # Locate any programs needed for the configuration process
    mysql_config = conf.find_program('mysql_config', var='MYSQL_CONFIG', mandatory=False)

    # libxml2
    conf.check_cfg(package='libxml-2.0', atleast_version='2.7.0',
                   args=['--cflags', '--libs'])

    # MySQL
    if mysql_config:
        conf.env.append_unique('CFLAGS',    conf.cmd_and_log([mysql_config,'--include']).split())
        conf.env.append_unique('LINKFLAGS', conf.cmd_and_log([mysql_config,'--libs']).split())
        conf.env['MYSQL_VERSION'] = conf.cmd_and_log([mysql_config,'--version'])
        
        conf.check_cc(lib='mysqlclient', errmsg='Missing libmysqlclient')
    else:
        # TODO: Move to a summary after the configuration process
        Logs.warn('MySQL library could not be found.  Database related tools will not be built.');


