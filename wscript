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
    conf.check_cc(header_name = 'CUnit/CUnit.h', function = 'CU_initialize_registry', lib = 'cunit',
            uselib_store = 'CUNIT', mandatory = False)

def build(bld):
    do_cov = False
    if bld.env['CC_NAME'] == 'gcc':
        if not bld.env['CFLAGS']:
            bld.env['CFLAGS'] = ['-std=c89', '-Wall', '-Wextra']
        bld.env['CFLAGS_OPT'] = ['-O2']
        bld.env['LINKFLAGS_OPT'] = ['-O2', '-s']

        bld.env['CFLAGS_TEST'] = ['-Wno-unused', '-g']

        bld.env['CFLAGS_COV'] = ['-fprofile-arcs', '-ftest-coverage']
        bld.env['LINKFLAGS_COV'] = ['-fprofile-arcs', '-ftest-coverage']
        do_cov = True

    bld(
            features = 'c cprogram',
            source = 'onlineclc.c',
            target = 'onlineclc',
            defines = ['ONLINECLC_CUNIT=0'],
            use = ['OPENCL', 'OPT']
       )

    # TODO: make the gcov output files a dependency
    if do_cov:
        bld(
                features = 'c cprogram',
                source = 'onlineclc.c',
                target = 'onlineclc-cov',
                defines = ['ONLINECLC_CUNIT=0'],
                use = ['OPENCL', 'COV']
            )

    if bld.env['HAVE_CUNIT_CUNIT_H']:
        bld(
                features = 'c cprogram',
                source = 'onlineclc.c',
                target = 'onlineclc-test',
                defines = ['ONLINECLC_CUNIT=1'],
                use = ['OPENCL', 'CUNIT', 'TEST']
            )
