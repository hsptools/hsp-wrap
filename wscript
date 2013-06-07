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
    default_num_cores = 12
    default_buffer_size = (1<<27)

    opt.load('compiler_c compiler_cxx')
    opt.load('compiler_mpi_c', tooldir=tool_dir)

    opt.add_option('--debug', action='store_true',
                   help='Build programs with debugging symbols')

    opt.add_option('--num-cores', action='store', type='int',
                   default=default_num_cores,
                   help='Number of cores to utilize in wrapper [default: %s]' % default_num_cores)

    opt.add_option('--result-buffer-size', action='store', type='int',
                   default=default_buffer_size,
                   help='Size of wrapper output buffer [default: %s]' % default_buffer_size)

def configure(conf):
    conf.load('compiler_c compiler_cxx')
    conf.load('compiler_mpi_c', tooldir=tool_dir)

    # Warn about almost anything
    conf.env.append_unique('CFLAGS', ['-std=gnu99', '-Wall', '-ggdb']) #, '-Werror'])

    # Locate any programs needed for the configuration process
    mysql_config = conf.find_program('mysql_config', var='MYSQL_CONFIG', mandatory=False)

    # Additional environment for modernish xopen/gnu features
    xopen500env = conf.env.derive()
    xopen500env.DEFINES += ['_GNU_SOURCE=1']

    #### Check for Libraries ####

    # std Math
    conf.check_cc(lib='m', uselib_store='M', mandatory=True,
            msg="Checking for 'libm' (math library)")
    # libev wants to know if we have floor()
    # FIXME: should be able to do use='M' instead of lib='m' here??
    conf.check_cc(lib='m', header_name='math.h', function_name='floor',
            mandatory=False)

    # POSIX semaphores
    conf.check_cc(lib='pthread', header_name='semaphore.h', function_name='sem_init',
	    uselib_store='PTHREAD', mandatory=True)
    conf.check_cc(lib='pthread', header_name='semaphore.h', function_name='sem_post',
	    uselib_store='PTHREAD', mandatory=True)
    conf.check_cc(lib='pthread', header_name='semaphore.h', function_name='sem_wait',
	    uselib_store='PTHREAD', mandatory=True)

    # libexpat1
    foo = conf.check_cc(lib='expat', header_name='expat.h',
            function_name='XML_ParserCreate', uselib_store='EXPAT',
            define_name='HAVE_EXPAT', mandatory=False,
            msg="Checking for 'Expat'")
    conf.env['HAVE_LIBEXPAT'] = foo

    """
    # libnih
    conf.check_cfg(package='libnih', atleast_version='1.0.3',
            args='--cflags --libs', uselib_store='NIH',
            mandatory=True)
    """

    # libYAML
    #conf.check_cfg(package='yaml-0.1', atleast_version='0.1.2',
    #        args='--cflags --libs', uselib_store='YAML',
    #        mandatory=True)

    #conf.check_cfg(package='glib-2.0', atleast_version='2.16',
    #        args='--cflags --libs', uselib_store='GLIB2', mandatory=True)

    # zlib
    try:
        conf.check_cfg(package='zlib', atleast_version='1.2.3',
                args='--cflags --libs', uselib_store='ZLIB')
    except:
        conf.check_cc(lib='z', header_name='zlib.h', function_name='inflate',
                uselib_store='ZLIB',
                msg="Checking for any 'zlib'")

    # MySQL
    if mysql_config:
        conf.check_cfg(path=mysql_config, args='--include --libs',
                package='', uselib_store='MYSQL', msg="Checking for 'MySQL'")

        conf.env['VERSION_MYSQL'] = conf.cmd_and_log([mysql_config,'--version'])

    #### Check for Headers and Functions ####
    funcs = [
            ('sys/inotify.h',   'inotify_init'),
            ('sys/epoll.h',     'epoll_ctl'),
            ('sys/event.h',     'kqueue'),
            ('port.h',          'port_create'),
            ('poll.h',          'poll'),
            ('sys/select.h',    'select'),
            ('sys/eventfd.h',   'eventfd'),
            ('sys/signalfd.h',  'signalfd')
    ]

    for h, f in funcs:
        conf.check_cc(header_name=h, mandatory=False)
        conf.check_cc(header_name=h, function_name=f, mandatory=False)

    #### Only worried about Mandatory Functions ####

    # GNU extensions
    conf.check_cc(header_name='stdio.h', function_name='getline',
            uselib_store='GETLINE', env=xopen500env)
    conf.check_cc(header_name='stdio.h', function_name='asprintf',
            uselib_store='ASPRINTF', env=xopen500env)

    # nftw from ftw.h (File Tree Walk)
    conf.check_cc(header_name='ftw.h', function_name='nftw',
            uselib_store='NFTW', env=xopen500env)

    # figure out how to get clock_gettime
    # Attempt 1: Built-in
    found = conf.check_cc(header_name='time.h', function_name='clock_gettime', mandatory=False)
    # Attempt 2: Syscall
    if not found:
        found = conf.check_cc(fragment='''
            #include <unistd.h>
            #include <sys/syscall.h>
            #include <time.h>

            int main() {
                struct timespec ts;
                int status = syscall (SYS_clock_gettime, CLOCK_REALTIME, &ts);
                return 0;
            }''',
            execute=True, define_name='HAVE_CLOCK_SYSCALL', mandatory=False,
            msg='Checking for clock_gettime syscall', errmsg='not found')
    # Attempt 3: Try function from librt
    if not found:
        found = conf.check_cc(lib='rt', header_name='time.h', function_name='clock_gettime',
                    mandatory=False, msg='Checking for function clock_gettime from librt')
        conf.define_cond('HAVE_LIBRT', found)

    # find nanosleep. Built-in, then librt
    found = conf.check_cc(header_name='time.h', function_name='nanosleep', mandatory=False)
    if not found:
        found = conf.check_cc(lib='rt', header_name='time.h', function_name='nanosleep',
                    mandatory=False, msg='Checking for function nanosleep from librt')
        conf.define_cond('HAVE_LIBRT', found)

    #### Stuff that doesn't belong in configuration environment ####

    # Debug vs Release
    conf.env.append_unique('CFLAGS', ['-O0', '-g'] if conf.options.debug else ['-O3'])

    #### Additional Configuration ####

    # Defines
    conf.define_cond('DEBUG',          conf.options.debug)
    conf.define('MCW_NCORES',          conf.options.num_cores)
    conf.define('MCW_RESULTBUFF_SIZE', conf.options.result_buffer_size)
    conf.define('HSP_VERSION',         VERSION)
    conf.write_config_header('hsp-config.h')

    # Summary
    if not mysql_config:
        Logs.warn('MySQL library could not be found.  Database related tools will not be built.')
    if not foo:
        Logs.warn('Expat library could not be found.  XML related tools will not be built.')

def build(bld):
    bld.recurse('hspwrap lib tools tests')

