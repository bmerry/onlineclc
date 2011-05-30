top = '.'
out = 'build'

def options(opt):
    opt.load('compiler_c')
    opt.add_option('--cl-headers', action='store', default=None, help='Include path for OpenCL')

def configure(conf):
    conf.load('compiler_c')
    conf.env.append_value('LIB_OPENCL', ['OpenCL'])
    if conf.options.cl_headers:
        conf.env.append_value('INCLUDES_OPENCL', [conf.options.cl_headers])
    conf.check_cc(header_name = 'CL/cl.h', use = 'OPENCL')

def build(bld):
    bld.recurse('src')
