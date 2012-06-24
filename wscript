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
    conf.env.NAME = 'default'

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



    # set up the variants
    v_debug = conf.env.copy()
    v_release = conf.env.copy()

    # release specific options
    v_release.set_variant('release')
    conf.set_env_name('release', v_release)
    conf.setenv('release')
    conf.env = v_release # seems to be necessary
    conf.env.CFLAGS = ['-Wall', '-O3', '-std=C99']
    conf.env.NAME = 'release'

    # debug variant
    v_debug.set_variant('debug')
    conf.set_env_name('debug', v_debug)
    conf.setenv('debug')
    conf.env = v_debug  # seems to be necessary
    conf.env.CFLAGS = ['-Wall', '-Wextra', '-O0', '-DDEBUG=1', '-std=C99']
    conf.env.NAME = 'debug'



def build(bld):
    if not bld.variant:
        print('To build the project call "waf build_debug" or "waf build_release", and try "waf --help"')

    additional_includes = [
        'src'    # add build directory for generated code
    ]

    bld.objects(source    = ['src/proto/rhizofs.proto'] +
                    glob.glob('src/*.c') +
                    glob.glob('src/util/*.c') +
                    glob.glob('src/util/*.rl'),
        #features  = 'includes',
        includes  = additional_includes,
        target    = 'rhizcommon',
        install_path = None
    )

    bld.program(
        source    = glob.glob('src/server/*.c'),
        target    = 'rhizosrv',
        includes  = additional_includes,
        use       = ['ZMQ', 'PROTOBUFC', 'PTHREAD', 'rhizcommon']
    )


    bld.program(
        source    = glob.glob('src/fs/*.c'),
        target    = 'rhizofs',
        includes  = additional_includes,
        use       = ['ZMQ', 'PROTOBUFC', 'PTHREAD', 'FUSE', 'rhizcommon']
    )


# build variants
for x in ['debug', 'release']:
    for y in (BuildContext, CleanContext, InstallContext, UninstallContext):
        name = y.__name__.replace('Context','').lower()
        class tmp(y):
            cmd = name + '_' + x
            variant = x
