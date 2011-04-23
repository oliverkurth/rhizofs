import os.path
import glob

from waflib import Task
from waflib.TaskGen import extension
from waflib.Build import BuildContext, CleanContext, \
    InstallContext, UninstallContext


class protocc(Task.Task):
    """compile proto files to c"""
    color   = 'BLUE'
    run_str = '${PROTOCC} --c_out=./src/proto -I ../../src/proto ${SRC}'
    ext_out = ['.pb-c.c', '.pb-c.h']


@extension('.proto')
def process_proto(self, node):
    c_node = node.change_ext('.pb-c.c')
    header_node = node.change_ext('.pb-c.h')
    obj_node = c_node.change_ext('.o')
    tsk = self.create_task('protocc', node, [c_node, header_node])

    # and the directory to the include path
    #self.includes.append(os.path.dirname(header_node.srcpath()))

    # add the cpp code to the files to compile
    self.source.append(c_node)


def options(opt):
    opt.load('compiler_c')


def configure(conf):
    # default variant
    conf.setenv('release')

    conf.load('compiler_c')

    conf.find_program('protoc-c',
        errmsg='You need to install the protobuf-c compiler',
        var='PROTOCC'
    )

    for header in ('stdlib.h', 'string.h', 'errno.h', 'sys/stat.h', 'stdio.h',
            'dirent.h', 'limits.h'):
        conf.check(header_name=header)

    # libs
    conf.check(package='protobufc', lib='protobuf-c',
                header_name='google/protobuf-c/protobuf-c.h',
                uselib_store='PROTOBUFC')
    conf.check(lib='zmq', header_name='zmq.h', uselib_store='ZMQ')
    conf.check(lib='pthread', header_name='pthread.h', uselib_store='PTHREAD')
    conf.check_cfg(package='fuse', args=['--cflags', '--libs'], uselib_store='FUSE')

    #conf.define('DEBUG', 0)
    conf.env.CFLAGS = ['-Wall', '-O3']
    #conf.write_config_header('release/config.h', remove=False) # disable remove to keep
                                                               # the values for the debug
                                                               # variant

    # debug variant
    conf.set_env_name ('debug', env=conf.env.derive()) # start with a copy of release
    #conf.define('DEBUG', 1)
    conf.env.CFLAGS = ['-Wall', '-Wextra', '-O0', '-DDEBUG']
    #conf.write_config_header('debug/config.h')



def build(bld):
    if not bld.variant:
        print('To build the project call "waf build_debug" or "waf build_release", and try "waf --help"')

    additional_includes = [
        '.',    # add build directory for config.h
        './src'
    ]

    bld(source    = 'src/proto/rhizofs.proto',
        features  = 'includes',
        target    = 'proto-obj',
        install_path = None
    )

    bld.program(
        source    = glob.glob('src/*.c') +
                    glob.glob('src/server/*.c') +
                    glob.glob('src/proto/*.c') +
                    glob.glob('src/util/*.c'),
        target    = 'rhizosrv',
        includes  = additional_includes,
        use       = ['ZMQ', 'PROTOBUFC', 'PTHREAD', 'proto-obj']
    )


    bld.program(
        source    = glob.glob('src/*.c') +
                    glob.glob('src/fs/*.c') +
                    glob.glob('src/proto/*.c') +
                    glob.glob('src/util/*.c'),
        target    = 'rhizofs',
        includes  = additional_includes,
        use       = ['ZMQ', 'PROTOBUFC', 'PTHREAD', 'FUSE', 'proto-obj']
    )


# build variants
for x in ['debug', 'release']:
    for y in (BuildContext, CleanContext, InstallContext, UninstallContext):
        name = y.__name__.replace('Context','').lower()
        class tmp(y):
            cmd = name + '_' + x
            variant = x
