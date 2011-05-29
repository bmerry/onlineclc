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

static const char *machine = NULL;
static const char *output_filename = NULL;

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
        ERROR_CASE(CL_INVALID_DEVICE_LIST);
        ERROR_CASE(CL_INVALID_OPERATION);
        ERROR_CASE(CL_INVALID_PROGRAM);
        ERROR_CASE(CL_INVALID_VALUE);
        ERROR_CASE(CL_OUT_OF_HOST_MEMORY);
    default:
        return "(unknown error)";
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

static cl_device find_device(const char *device_name)
{
    cl_int status;
    cl_uint num_devices, i;
    cl_device_id *devices;
    size_t size;
    char *current_device_name;

    /* Get number of available devices */
    status = clGetDeviceIDs(CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get device ID count");
    if (num_devices == 0)
        die(1, "No OpenCL devices found");

    size = sizeof(cl_device_id) * num_devices;
    devices = (cl_device_id *) malloc(size);
    if (devices == NULL)
        die("Could not allocate %zu bytes for device IDs", size);
    status = clGetDeviceIDs(CL_DEVICE_TYPE_ALL, num_devices, devices, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get device IDs");

    for (i = 0; i < num_devices; i++)
    {
        size_t name_len;
        char *name;
        status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, 0, NULL, &name_len);
        if (status != CL_SUCCESS)
            die_cl(status, 1, "Failed to query device name length");
        name = (char *) malloc(name_len);
        if (name == NULL)
            die("Could not allocate %zu bytes for device name", name_len);
        status = clGetDeviceInfo(devices[i], CL_DEVICE_NAME, name_len, name, NULL);
        if (status != CL_SUCCESS)
            die_cl(status, 1, "Failed to query device name");

        if (device_name == NULL || strcmp(name, device_name) == 0)
        {
            /* Match found */
            /* TODO: check that the device supports online compilation */
            cl_device_id ans = device_ids[i];

            if (device_name == NULL && num_devices > 1)
            {
                fprintf(stderr, "Warning: multiple devices found. Defaulting to `%s'\n", name);
            }
            free(name);
            free(device_ids);
            return ans;
        }
    }

    free(device_ids);
    die(1, "No OpenCL device called `%s' found", device_name);
    return 0;
}

static cl_context create_context(cl_device device)
{
    cl_context ctx;
    cl_int status;

    ctx = clCreateContext(0, 1, &device, NULL, NULL, &status);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to create OpenCL context");
    return ctx;
}

static cl_program create_program(
    cl_context ctx,
    cl_device device,
    const char *source_filename,
    const char *options)
{
    char *src;
    struct stat fb;
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
    src = (char *) mmap(NULL, len, PROT_READ, fd, 0);
    if (addr == NULL)
        pdie(1, "Failed to map `%s'", source_filename);

    program = clCreateProgramWithSource(ctx, 1, &src, &len, &status);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to load source from `%s'", source_filename);

    munmap(src, sb.st_size);
    close(fd);

    if (options == NULL)
        options = "";
    status = clBuildProgram(program, 1, &device, options, NULL, NULL);
    switch (status)
    {
    case CL_SUCCESS:
        break;
    case CL_BUILD_PROGRAM_FAILURE:
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
            if (machine != NULL)
                die(2, "-b option specified twice");
            machine = argv[i + 1];
            i++;
        }
        else if (0 == strcmp(argv[i], "-o"))
        {
            if (i == argc - 2)
                usage(2, "Source file not specified");
            if (output_filename != NULL)
                die(2, "-o option specified twice");
            output_filename = argv[i + 1];
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

int main(int argc, const char * const *argv)
{
    compiler_options options;
    state s;

    process_options(&options, argc, argv);
    s.device = find_device(options.machine);
    s.ctx = create_context(s.device);
    s.program = create_program(s.ctx, s.device, options.source_filename, options.options);

    return 0;
}
