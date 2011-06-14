top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_c')
    opt.load('gnu_dirs')
    opt.add_option('--cl-headers', action='store', default=None, help='Include path for OpenCL')

def configure(conf):
    conf.load('compiler_c')
    conf.load('gnu_dirs')
    conf.env.append_value('LIB_OPENCL', ['OpenCL'])
    if conf.options.cl_headers:
        conf.env.append_value('INCLUDES_OPENCL', [conf.options.cl_headers])
    conf.check_cc(header_name = 'CL/cl.h', use = 'OPENCL')

def build(bld):
    cflags = []
    test_cflags = []
    if bld.env['CC_NAME'] == 'gcc':
        cflags = ['-std=c89', '-Wall', '-Wextra', '-O2', '-s']
        test_cflags = ['-std=c89', '-Wall', '-Wextra', '-g']

    bld(
            features = 'c cprogram',
            source = 'onlineclc.c',
            target = 'onlineclc',
            cflags = cflags,
            defines = ['ONLINECLC_CUNIT=0'],
            use = 'OPENCL'
       )

    bld(
            features = 'c cprogram',
            source = 'onlineclc.c',
            target = 'onlineclc-test',
            cflags = test_cflags,
            defines = ['ONLINECLC_CUNIT=1'],
            use = 'OPENCL',
            lib = 'cunit'
        )
