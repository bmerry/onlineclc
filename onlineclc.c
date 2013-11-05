/*  OnlineCLC: Front-end to online OpenCL C compiler
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
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <assert.h>
#include <ctype.h>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#else
#include <CL/cl.h>
#endif

/* Holds state associated with a compilation */
typedef struct
{
    /* Options to pass to clBuildProgram - dynamically allocated */
    char *options;
    /* Current length of options (excluding NUL) */
    size_t len;
    /* Space allocated for options */
    size_t size;

    /* -b command-line option, or NULL if not given
     * A shallow copy from argv, do not free.
     */
    const char *machine;
    /* -o command-line option, or NULL if not given
     * A shallow copy from argv, do not free.
     */
    const char *output_filename;
    /* Source filename from command line (cannot be NULL)
     * A shallow copy from argv, do not free.
     */
    const char *source_filename;
} compiler_options;

/* Assorted CL objects */
typedef struct
{
    cl_device_id device;
    cl_context ctx;
    cl_program program;
} state;

/* Prints msg (printf-style) and kills the process */
static void die(int exitcode, const char *msg, ...)
{
    va_list ap;

    va_start(ap, msg);
    vfprintf(stderr, msg, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    exit(exitcode);
}

/* Prints msg (printf-style) followed by perror(), and kills the process */
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

static void *onlineclc_malloc(size_t size, const char *purpose)
{
    void *ptr = malloc(size);
    if (ptr == NULL)
        die(1, "Failed to allocate %zu bytes for %s", size, purpose);
    return ptr;
}

/* Returns the string form of an OpenCL error code,
 * as a static string.
 */
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

/* Prints msg (printf-style) and an explanation of a CL error code, and
 * kills the process.
 */
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

/* Finds the device ID for a device with the given name. If device_name is
 * NULL, matches any device. If the device could not be found, kills the
 * process.
 */
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

    /* Get a list of platforms */
    size = sizeof(cl_platform_id) * num_platforms;
    platforms = (cl_platform_id *) onlineclc_malloc(size, "platform IDs");
    status = clGetPlatformIDs(num_platforms, platforms, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get platform IDs");

    for (i = 0; i < num_platforms; i++)
    {
        cl_device_id *devices;
        cl_uint num_devices, j;

        /* Get number of available devices for the platform */
        status = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
        if (status != CL_SUCCESS)
            die_cl(status, 1, "Failed to get device ID count");
        total_devices += num_devices;

        /* Early-out to avoid allocating zero bytes, which doesn't always work
         * on all platforms.
         */
        if (num_devices == 0)
            continue;

        size = sizeof(cl_device_id) * num_devices;
        devices = (cl_device_id *) onlineclc_malloc(size, "device IDs");
        status = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, num_devices, devices, NULL);
        if (status != CL_SUCCESS)
            die_cl(status, 1, "Failed to get device IDs");

        for (j = 0; j < num_devices; j++)
        {
            size_t name_len;
            char *name;

            /* Determine space needed for the name */
            status = clGetDeviceInfo(devices[j], CL_DEVICE_NAME, 0, NULL, &name_len);
            if (status != CL_SUCCESS)
                die_cl(status, 1, "Failed to query device name length");
            name = (char *) onlineclc_malloc(name_len * sizeof(char), "device names");
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

/* Create an OpenCL context for device, and kill the process on failure.
 */
static cl_context create_context(cl_device_id device)
{
    cl_context ctx;
    cl_int status;

    ctx = clCreateContext(0, 1, &device, NULL, NULL, &status);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to create OpenCL context");
    return ctx;
}

/* Writes the build log to the output. Does not check for errors on the
 * output stream.
 */
static void dump_build_log(FILE *out, cl_program program, cl_device_id device)
{
    cl_int status;
    char *build_log;
    size_t len;

    status = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to get length of build log");

    /* Early-out to avoid dealing with malloc(0) */
    if (len == 0)
        return;

    build_log = (char *) onlineclc_malloc(len * sizeof(char), "the build log");

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

/* Checks whether c may appear unencoded in a string literal. According to C99,
 * string literals may contain characters from the source character set except
 * for double-quote, backslash or newline. To be safe, we also exclude '?'
 * since it can form trigraphs.
 *
 * C99 does not specify the extended source character set, so we consider only
 * the basic character set.
 */
static int safe_for_string_literal(char c)
{
    if (isalnum(c))
        return 1;
    else switch(c)
    {
    case '!':
    case '#':
    case '%':
    case '&':
    case '\'':
    case '(':
    case ')':
    case '*':
    case '+':
    case ',':
    case '-':
    case '.':
    case '/':
    case ':':
    case ';':
    case '<':
    case '=':
    case '>':
    case '[':
    case ']':
    case '^':
    case '_':
    case '{':
    case '|':
    case '}':
    case '~':
    case ' ':
    case '\t':
    case '\v':
    case '\f':
        return 1;
    default:
        return 0;
    }
}

/* Escapes a string so that it may appear between double quotes in OpenCL C.
 * The return value is dynamically allocated, and must be freed by the caller.
 *
 * Unsafe characters (e.g. double quotes) are escaped using an octal escape.
 * Hex escapes are avoided since they can gobble up following numbers.
 */
static char *escape_c_string(const char *str)
{
    const size_t src_len = strlen(str);
    size_t i;
    size_t dst_len = 0;
    char *dst, *cur;

    /* First pass: determine memory requirement */
    for (i = 0; i < src_len; i++)
    {
        if (safe_for_string_literal(str[i]))
            dst_len++;
        else
            dst_len += 4; /* for an octal escape */
    }

    dst = (char *) onlineclc_malloc((dst_len + 1) * sizeof(char), "string");

    /* Second pass: fill in the string */
    cur = dst;
    for (i = 0; i < src_len; i++)
    {
        if (safe_for_string_literal(str[i]))
            *cur++ = str[i];
        else
        {
            unsigned char value = (unsigned char) str[i];
            *cur++ = '\\';
            *cur++ = '0' + ((value / 64) % 8);
            *cur++ = '0' + ((value / 8) % 8);
            *cur++ = '0' + (value % 8);
        }
    }
    *cur = '\0';
    return dst;
}

/* This function does the heavy lifting. It loads the source, builds the
 * program and writes the build log. On failure, the process is terminated.
 *
 * Currently the source file is loaded with mmap(), since that is easier to
 * implement than streaming. However, it will prevent compiling from a pipe and
 * is not very portable, so it should be replaced in future.
 */
static cl_program create_program(
    cl_context ctx,
    cl_device_id device,
    const char *source_filename,
    const char *options)
{
    void *addr;              /* mmap address for the source file */
    char *escaped_filename;  /* Soruce filename with quotes etc escaped */
    const char *srcs[4];     /* pointers to fragments of source */
    size_t src_lens[4];      /* lengths for source fragments */
    struct stat sb;          /* stat info on the file, to determine its size */
    cl_int status;
    int fd;                  /* file descriptor for the source file */
    size_t len;              /* length of the source file */
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

    /* Inject a line of the form
     * #line 1 "filename"
     * so that the build log can show the correct filename in error messages
     * (depending on the OpenCL implementation)
     */
    escaped_filename = escape_c_string(source_filename);
    srcs[0] = "#line 1 \"";                         src_lens[0] = 0;
    srcs[1] = escaped_filename;                     src_lens[1] = 0;
    srcs[2] = "\"\n";                               src_lens[2] = 0;
    srcs[3] = (const char *) addr;                  src_lens[3] = len;

    program = clCreateProgramWithSource(ctx, 4, srcs, src_lens, &status);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to load source from `%s'", source_filename);
    free(escaped_filename);

    /* If len == 0 then we didn't use mmap */
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

/* Print usage information and exit with exitcode.
 * If message is not NULL, it is displayed first.
 */
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
          "   -h | --help         Show usage\n"
          "\n"
          "Other options are passed to the online compiler\n"
          "NB: exactly one source file must be given, as the last argument.\n",
          message != NULL ? stderr : stdout
         );
    exit(exitcode);
}

/* Determine whether a command-line option is expected to be followed by an
 * argument, which should not be parsed as an option. This is necessarily
 * approximate since there may be vendor-specific options, but it reduces the
 * chance of accidentally processing an argument as an option.
 */
static int option_has_argument(const char *option)
{
    return (0 == strcmp(option, "-I"))
        || (0 == strcmp(option, "-D"))
        || (0 == strcmp(option, "-b"))
        || (0 == strcmp(option, "-o"));
}

/* Adds option to the compiler options, and appends a trailing space so that
 * options will be space-separated. It will dynamically resize the memory if
 * needed, and kill the process if that fails.
 */
static void append_compiler_option(compiler_options *options, const char *option)
{
    size_t len = strlen(option);
    /* options->len + len + 2 bytes are needed:
     * options->len + len for options text, one for trailing space, one for NUL
     */
    while (options->len + len + 1 >= options->size)
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

/* Parse the command-line options into a structure. The options structure
 * does not need to be pre-initialized.
 */
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

    /* First look for --help, and show help, even if there is no source file. */
    for (i = 1; i < argc; i++)
        if (0 == strcmp(argv[i], "-h") || 0 == strcmp(argv[i], "--help"))
            usage(0, NULL);
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

/* Extract the binary from program and write it to output_filename.
 *
 * This currently uses mmap(), so is not very portable.
 */
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

    /* mmap() doesn't like it if we use O_WRONLY, since in some cases
     * PROT_WRITE implies PROT_READ.
     */
    fd = open(output_filename, O_RDWR | O_CREAT, 0666);
    if (fd < 0)
        pdie(1, "Failed to open `%s'", output_filename);

    if (ftruncate(fd, sizes[0]) != 0)
        pdie(1, "Failed to resize `%s' to %zu bytes", output_filename, sizes[0]);

    binaries[0] = (unsigned char *) mmap(NULL, sizes[0], PROT_WRITE, MAP_SHARED, fd, 0);
    if (binaries[0] == MAP_FAILED)
        pdie(1, "Failed to map `%s'", output_filename);

    status = clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(unsigned char *), binaries, NULL);
    if (status != CL_SUCCESS)
        die_cl(status, 1, "Failed to query the program binary");

    munmap((void *) binaries[0], sizes[0]);
    close(fd);
}

#if !ONLINECLC_CUNIT
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
    free(options.options);

    return 0;
}
#endif /* !ONLINECLC_CUNIT */

#if ONLINECLC_CUNIT

#include <CUnit/CUnit.h>
#include <CUnit/Basic.h>

static void test_escape_c_string(const char *orig, const char *expected)
{
    char *escaped = escape_c_string(orig);
    CU_ASSERT_STRING_EQUAL(escaped, expected);
    free(escaped);
}

static void test_escape_c_string_empty(void)
{
    test_escape_c_string("", "");
}

static void test_escape_c_string_simple(void)
{
    test_escape_c_string("hello world", "hello world");
}

static void test_escape_c_string_quotes(void)
{
    test_escape_c_string("\"quotes\"", "\\042quotes\\042");
}

static void test_escape_c_string_trigraph(void)
{
    test_escape_c_string("trigraph\?\?/", "trigraph\\077\\077/");
}

static void test_escape_c_string_backslash(void)
{
    test_escape_c_string("backslash\\", "backslash\\134");
}

int main(int argc, const char * const *argv)
{
    int ret;
    static CU_TestInfo escape_c_string_tests[] =
    {
        { "empty", test_escape_c_string_empty },
        { "simple", test_escape_c_string_simple },
        { "quotes", test_escape_c_string_quotes },
        { "trigraph", test_escape_c_string_trigraph },
        { "backslash", test_escape_c_string_backslash },
        CU_TEST_INFO_NULL
    };
    static CU_SuiteInfo suites[] =
    {
        { "escape_c_string", NULL, NULL, escape_c_string_tests },
        CU_SUITE_INFO_NULL
    };

    CU_set_error_action(CUEA_ABORT);
    CU_initialize_registry();
    CU_register_suites(suites);
    CU_basic_set_mode(CU_BRM_VERBOSE);
    CU_basic_run_tests();
    ret = CU_get_number_of_failure_records() > 0 ? 1 : 0;
    CU_cleanup_registry();
    return ret;
}

#endif /* ONLINECLC_CUNIT */
