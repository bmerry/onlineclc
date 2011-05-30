/*  onlineclc: Front-end to online OpenCL C compiler
 *  Copyright (C) 2011  Bruce Merry
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _ISOC99_SOURCE
# define _ISOC99_SOURCE 1
#endif
#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif
#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE 500
#endif

#if HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stddef.h>
#include <stdarg.h>
#include <CL/cl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>

typedef struct
{
    char *options;
    size_t len;
    size_t size;

    const char *machine;
    const char *output_filename;
    const char *source_filename;
} compiler_options;

typedef struct
{
    cl_device_id device;
    cl_context ctx;
    cl_program program;
} state;

static void die(int exitcode, const char *msg, ...)
{
    va_list ap;

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(exitcode);
}

static void pdie(int exitcode, const char *msg, ...)
{
    va_list ap;

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fputs(": ", stderr);
    perror(NULL);
    exit(exitcode);
}

static const char *error_to_string(cl_int error)
{
#define ERROR_CASE(name) case name: return #name
    switch (error)
    {
        ERROR_CASE(CL_DEVICE_NOT_AVAILABLE);
        ERROR_CASE(CL_DEVICE_NOT_FOUND);
        ERROR_CASE(CL_INVALID_BINARY);
        ERROR_CASE(CL_INVALID_BUILD_OPTIONS);
        ERROR_CASE(CL_INVALID_DEVICE);
        ERROR_CASE(CL_INVALID_DEVICE_TYPE);
        ERROR_CASE(CL_INVALID_OPERATION);
        ERROR_CASE(CL_INVALID_PLATFORM);
        ERROR_CASE(CL_INVALID_PROGRAM);
        ERROR_CASE(CL_INVALID_VALUE);
        ERROR_CASE(CL_OUT_OF_HOST_MEMORY);
    default:
        return "unknown error";
    }
#undef ERROR_CASE
}

static void die_cl(cl_int status, int exitcode, const char *msg, ...)
{
    va_list ap;

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fprintf(stderr, ": Error code %d (%s)\n",
            (int) status, error_to_string(status));
    exit(exitcode);
}

static cl_device_id find_device(const char *device_name)
{
    size_t size;
    cl_int status;

    cl_uint num_platforms, i;
    cl_platform_id *platforms;

    cl_device_id ans = NULL;
    cl_uint total_devices = 0;
    cl_uint match_devices = 0;

    /* Get number of available platforms */
    status = clGetPlatformIDs(0, NULL, &num_platforms);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get platform ID count");
    if (num_platforms == 0)
        die_cl(status, 1, "No OpenCL platforms found");

    size = sizeof(cl_platform_id) * num_platforms;
    platforms = (cl_platform_id *) malloc(size);
    if (platforms == NULL)
        die(1, "Could not allocate %zu bytes for platform IDs", size);
    status = clGetPlatformIDs(num_platforms, platforms, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get platform IDs");

    for (i = 0; i < num_platforms; i++)
    {
        cl_device_id *devices;
        cl_uint num_devices, j;

        /* Get number of available devices */
        status = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
        if (status != CL_SUCCESS)
            die_cl(status, 1, "Failed to get device ID count");
        total_devices += num_devices;
        if (num_devices == 0)
            continue;

        size = sizeof(cl_device_id) * num_devices;
        devices = (cl_device_id *) malloc(size);
        if (devices == NULL)
            die(1, "Could not allocate %zu bytes for device IDs", size);
        status = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, num_devices, devices, NULL);
        if (status != CL_SUCCESS)
            die_cl(status, 1, "Failed to get device IDs");

        for (j = 0; j < num_devices; j++)
        {
            size_t name_len;
            char *name;
            status = clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 0, NULL, &name_len);
            if (status != CL_SUCCESS)
                die_cl(status, 1, "Failed to query device name length");
            name = (char *) malloc(name_len);
            if (name == NULL)
                die(1, "Could not allocate %zu bytes for device name", name_len);
            status = clGetDeviceInfo(devices[j], CL_DEVICE_NAME, name_len, name, NULL);
            if (status != CL_SUCCESS)
                die_cl(status, 1, "Failed to query device name");

            if (device_name == NULL || strcmp(name, device_name) == 0)
            {
                /* Match found */
                /* TODO: check that the device supports online compilation */
                if (match_devices == 0)
                    ans = devices[j];
                match_devices++;
            }
            free(name);
        }
        free(devices);
    }
    free(platforms);

    if (total_devices == 0)
        die(1, "No OpenCL devices found");
    else if (match_devices == 0)
    {
        assert(device_name != NULL);
        die(1, "No OpenCL device called `%s' found", device_name);
    }

    if (match_devices > 1)
    {
        fprintf(stderr, "Warning: multiple devices match, using the first one\n");
    }
    return ans;
}

static cl_context create_context(cl_device_id device)
{
    cl_context ctx;
    cl_int status;

    ctx = clCreateContext(0, 1, &device, NULL, NULL, &status);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to create OpenCL context");
    return ctx;
}

static void dump_build_log(FILE *out, cl_program program, cl_device_id device)
{
    cl_int status;
    char *build_log;
    size_t len;

    status = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get length of build log");
    if (len == 0)
        return;

    build_log = (char *) malloc(len * sizeof(char));
    if (build_log == NULL)
        die(1, "Failed to allocate %zu bytes for the build log", len);

    status = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, len, build_log, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get build log");
    /* The CL implementation should null-terminate itself; this is just to
     * protect against bugs.
     */
    build_log[len - 1] = '\0';

    fputs(build_log, out);
    /* Add a trailing newline if required */
    if (len >= 2 && build_log[len - 2] != '\n')
        fputs("\n", out);
    free(build_log);
}

static cl_program create_program(
    cl_context ctx,
    cl_device_id device,
    const char *source_filename,
    const char *options)
{
    void *addr;
    const char *srcs[4];
    size_t src_lens[4];
    struct stat sb;
    cl_int status;
    int fd;
    size_t len;
    cl_program program;

    fd = open(source_filename, O_RDONLY);
    if (fd < 0)
        pdie(1, "Failed to open `%s'", source_filename);

    if (fstat(fd, &sb) == -1)
        pdie(1, "Failed to stat `%s'", source_filename);

    len = sb.st_size;
    if (len == 0)
    {
        /* Can't portably mmap 0 bytes */
        addr = "";
    }
    else
    {
        addr = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
        if (addr == MAP_FAILED)
            pdie(1, "Failed to map `%s'", source_filename);
    }

    srcs[0] = "#line 1 \"";        src_lens[0] = 0;
    /* TODO: escape quotes in the filename */
    srcs[1] = source_filename;     src_lens[1] = 0;
    srcs[2] = "\"\n";              src_lens[2] = 0;
    srcs[3] = (const char *) addr; src_lens[3] = len;

    program = clCreateProgramWithSource(ctx, 4, srcs, src_lens, &status);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to load source from `%s'", source_filename);

    if (len != 0)
        munmap(addr, sb.st_size);
    close(fd);

    if (options == NULL)
        options = "";
    status = clBuildProgram(program, 1, &device, options, NULL, NULL);
    switch (status)
    {
    case CL_SUCCESS:
        break;
    case CL_BUILD_PROGRAM_FAILURE:
        dump_build_log(stderr, program, device);
        exit(1);
    default:
        die_cl(status, 1, "Failed to build `%s'", source_filename);
    }
    return program;
}

static void usage(int exitcode, const char *message)
{
    if (message != NULL)
    {
        fprintf(stderr, "%s\n\n", message);
    }
    fputs("Usage: onlineclc [<options>] [-b <machine>] [-o <outfile>] <source>\n"
          "\n"
          "   -b machine          Specify device to use\n"
          "   -o outfile          Specify output file\n"
          "   --help              Show usage\n"
          "\n"
          "Other options are passed to the online compiler\n"
          "NB: exactly one source file must be given, as the last argument.\n",
          stderr
         );
    exit(exitcode);
}

static int option_has_argument(const char *option)
{
    return (0 == strcmp(option, "-I"))
        || (0 == strcmp(option, "-D"))
        || (0 == strcmp(option, "-b"))
        || (0 == strcmp(option, "-o"));
}

static void append_compiler_option(compiler_options *options, const char *option)
{
    size_t len = strlen(option);
    if (options->len + len + 1 >= options->size)
    {
        size_t new_size = 2 * options->size;
        if (new_size == 0)
            new_size = 64;
        options->options = realloc(options->options, new_size);
        if (options->options == NULL)
            die(1, "Out of memory trying to allocate %zu bytes", new_size);

        options->size = new_size;
    }
    memcpy(options->options + options->len, option, len);
    options->len += len;
    options->options[options->len] = ' ';
    options->options[options->len + 1] = '\0';
    options->len++;
}

static void process_options(compiler_options *options, int argc, const char * const *argv)
{
    int i;
    if (argc <= 1)
        usage(2, "Source file not specified");

    options->options = NULL;
    options->size = 0;
    options->len = 0;
    options->machine = NULL;
    options->output_filename = NULL;
    options->source_filename = NULL;

    for (i = 1; i < argc - 1; i++)
    {
        if (0 == strcmp(argv[i], "-b"))
        {
            if (i == argc - 2)
                usage(2, "Source file not specified");
            if (options->machine != NULL)
                die(2, "-b option specified twice");
            options->machine = argv[i + 1];
            i++;
        }
        else if (0 == strcmp(argv[i], "-o"))
        {
            if (i == argc - 2)
                usage(2, "Source file not specified");
            if (options->output_filename != NULL)
                die(2, "-o option specified twice");
            options->output_filename = argv[i + 1];
            i++;
        }
        else if (0 == strcmp(argv[i], "--help"))
        {
            usage(0, NULL);
        }
        else
        {
            append_compiler_option(options, argv[i]);
            if (option_has_argument(argv[i]) && i < argc - 2)
            {
                append_compiler_option(options, argv[i + 1]);
                i++;
            }
        }
    }
    options->source_filename = argv[argc - 1];

    /* Strip trailing space after last option */
    if (options->len > 0)
    {
        options->options[options->len - 1] = '\0';
        options->len--;
    }
}

static void write_program(const char *output_filename, cl_program program)
{
    cl_int status;
    cl_uint num_devices;
    size_t sizes[1];
    unsigned char *binaries[1];
    int fd;

    /* Verify that there is only one device */
    status = clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &num_devices, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to query number of devices from program");
    if (num_devices != 1)
        die(1, "Expected one device but found %u", (unsigned int) num_devices);

    status = clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t), sizes, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to obtain binary size");

    if (sizes[0] == 0)
        die(1, "No binary was produced by the compiler");

    fd = open(output_filename, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
        pdie(1, "Could not open `%s'", output_filename);

    if (ftruncate(fd, sizes[0]) != 0)
        pdie(1, "Could not resize `%s' to %zu bytes", output_filename, sizes[0]);

    binaries[0] = (unsigned char *) mmap(NULL, sizes[0], PROT_WRITE, MAP_SHARED, fd, 0);
    if (binaries[0] == MAP_FAILED)
        pdie(1, "Could not map `%s'", output_filename);

    status = clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(unsigned char *), binaries, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to query the program binary");

    munmap((void *) binaries[0], sizes[0]);
    close(fd);
}

int main(int argc, const char * const *argv)
{
    compiler_options options;
    state s;

    process_options(&options, argc, argv);
    s.device = find_device(options.machine);
    s.ctx = create_context(s.device);
    s.program = create_program(s.ctx, s.device, options.source_filename, options.options);
    dump_build_log(stderr, s.program, s.device);
    if (options.output_filename != NULL)
        write_program(options.output_filename, s.program);

    clReleaseProgram(s.program);
    clReleaseContext(s.ctx);

    return 0;
}
