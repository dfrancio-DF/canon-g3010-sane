#define _GNU_SOURCE
#include <sane/sane.h>
#include <sane/saneopts.h>
#include <stddef.h>
#include <stdio.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define sane_init              sane_canon_chmp_init
#define sane_exit              sane_canon_chmp_exit
#define sane_get_devices       sane_canon_chmp_get_devices
#define sane_open              sane_canon_chmp_open
#define sane_close             sane_canon_chmp_close
#define sane_get_option_descriptor sane_canon_chmp_get_option_descriptor
#define sane_control_option    sane_canon_chmp_control_option
#define sane_get_parameters    sane_canon_chmp_get_parameters
#define sane_start             sane_canon_chmp_start
#define sane_read              sane_canon_chmp_read
#define sane_cancel            sane_canon_chmp_cancel
#define sane_set_io_mode       sane_canon_chmp_set_io_mode
#define sane_get_select_fd     sane_canon_chmp_get_select_fd

#define BACKEND_NAME "canon_chmp"
#define SCRIPT_PATH "/usr/lib/canon-chmp/chmp_scan.py"
#define MAX_HOSTS 16

enum { OPT_NUM_OPTIONS = 0, OPT_MODE, OPT_RESOLUTION, NUM_OPTIONS };

typedef struct {
    char host[256];
    char name[320];
    SANE_Device device;
} Device;

typedef struct {
    char host[256];
    SANE_Option_Descriptor options[NUM_OPTIONS];
    SANE_Word num_options;
    SANE_Word resolution;
    char mode[16];
    SANE_Parameters params;
    unsigned char *image;
    size_t image_size;
    size_t offset;
} Handle;

static Device devices[MAX_HOSTS];
static const SANE_Device *device_ptrs[MAX_HOSTS + 1];
static size_t ndevices;
static SANE_Word resolutions[] = { 3, 150, 300, 600 };
static const SANE_String_Const modes[] = { "Gray", "Color", NULL };
static const char *backend_conf = "/etc/sane.d/canon-chmp.conf";

struct jpeg_error_state {
    struct jpeg_error_mgr pub;
    jmp_buf jump;
};

static void jpeg_fail(j_common_ptr cinfo)
{
    struct jpeg_error_state *err = (struct jpeg_error_state *) cinfo->err;
    longjmp(err->jump, 1);
}

static void add_device(const char *host)
{
    size_t i;
    if (!host || !*host || ndevices >= MAX_HOSTS)
        return;
    for (i = 0; i < ndevices; ++i)
        if (strcmp(devices[i].host, host) == 0)
            return;
    memset(&devices[ndevices], 0, sizeof(devices[ndevices]));
    snprintf(devices[ndevices].host, sizeof(devices[ndevices].host), "%s", host);
    snprintf(devices[ndevices].name, sizeof(devices[ndevices].name), "%s", host);
    devices[ndevices].device.name = devices[ndevices].name;
    devices[ndevices].device.vendor = "Canon";
    devices[ndevices].device.model = "PIXMA G3010 series (CHMP)";
    devices[ndevices].device.type = "flatbed scanner";
    device_ptrs[ndevices] = &devices[ndevices].device;
    ++ndevices;
}

static void load_devices(void)
{
    char line[512];
    FILE *fp;
    const char *override = getenv("CANON_CHMP_HOST");
    ndevices = 0;
    if (override && *override) {
        add_device(override);
        device_ptrs[ndevices] = NULL;
        return;
    }
    fp = fopen(backend_conf, "r");
    if (fp) {
        while (fgets(line, sizeof(line), fp)) {
            char *p = line;
            while (*p == ' ' || *p == '\t') ++p;
            if (*p == '#' || *p == '\n' || *p == '\0') continue;
            p[strcspn(p, "\r\n \t")] = '\0';
            add_device(p);
        }
        fclose(fp);
    }
    device_ptrs[ndevices] = NULL;
}

SANE_Status sane_init(SANE_Int *version_code, SANE_Auth_Callback authorize)
{
    (void) authorize;
    if (version_code)
        *version_code = SANE_VERSION_CODE(SANE_CURRENT_MAJOR, SANE_CURRENT_MINOR, 1);
    load_devices();
    return SANE_STATUS_GOOD;
}

void sane_exit(void) { }

SANE_Status sane_get_devices(const SANE_Device ***list, SANE_Bool local_only)
{
    (void) local_only;
    if (!list) return SANE_STATUS_INVAL;
    load_devices();
    *list = device_ptrs;
    return SANE_STATUS_GOOD;
}

static void init_options(Handle *h)
{
    static const SANE_Range dpi_range = { 150, 600, 150 };
    memset(h->options, 0, sizeof(h->options));
    h->num_options = NUM_OPTIONS;
    h->options[OPT_NUM_OPTIONS].name = SANE_NAME_NUM_OPTIONS;
    h->options[OPT_NUM_OPTIONS].title = SANE_TITLE_NUM_OPTIONS;
    h->options[OPT_NUM_OPTIONS].desc = SANE_DESC_NUM_OPTIONS;
    h->options[OPT_NUM_OPTIONS].type = SANE_TYPE_INT;
    h->options[OPT_NUM_OPTIONS].size = sizeof(SANE_Word);
    h->options[OPT_NUM_OPTIONS].cap = SANE_CAP_SOFT_DETECT;

    h->options[OPT_MODE].name = SANE_NAME_SCAN_MODE;
    h->options[OPT_MODE].title = SANE_TITLE_SCAN_MODE;
    h->options[OPT_MODE].desc = SANE_DESC_SCAN_MODE;
    h->options[OPT_MODE].type = SANE_TYPE_STRING;
    h->options[OPT_MODE].unit = SANE_UNIT_NONE;
    h->options[OPT_MODE].size = 16;
    h->options[OPT_MODE].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    h->options[OPT_MODE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    h->options[OPT_MODE].constraint.string_list = modes;

    h->options[OPT_RESOLUTION].name = SANE_NAME_SCAN_RESOLUTION;
    h->options[OPT_RESOLUTION].title = SANE_TITLE_SCAN_RESOLUTION;
    h->options[OPT_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
    h->options[OPT_RESOLUTION].type = SANE_TYPE_INT;
    h->options[OPT_RESOLUTION].unit = SANE_UNIT_DPI;
    h->options[OPT_RESOLUTION].size = sizeof(SANE_Word);
    h->options[OPT_RESOLUTION].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    h->options[OPT_RESOLUTION].constraint_type = SANE_CONSTRAINT_WORD_LIST;
    h->options[OPT_RESOLUTION].constraint.word_list = resolutions;
    (void) dpi_range;
}

SANE_Status sane_open(SANE_String_Const name, SANE_Handle *out)
{
    Handle *h;
    const char *host;
    if (!name || !out) return SANE_STATUS_INVAL;
    /* dll-backend passes only the device-specific suffix after "backend:". */
    host = strncmp(name, BACKEND_NAME ":", strlen(BACKEND_NAME) + 1) == 0
           ? name + strlen(BACKEND_NAME) + 1 : name;
    if (!*host || strlen(host) >= sizeof(h->host)) return SANE_STATUS_INVAL;
    h = calloc(1, sizeof(*h));
    if (!h) return SANE_STATUS_NO_MEM;
    strcpy(h->host, host);
    strcpy(h->mode, "Color");
    h->resolution = 300;
    init_options(h);
    *out = h;
    return SANE_STATUS_GOOD;
}

void sane_close(SANE_Handle handle)
{
    Handle *h = handle;
    if (!h) return;
    free(h->image);
    free(h);
}

const SANE_Option_Descriptor *sane_get_option_descriptor(SANE_Handle handle, SANE_Int option)
{
    Handle *h = handle;
    if (!h || option < 0 || option >= NUM_OPTIONS) return NULL;
    return &h->options[option];
}

SANE_Status sane_control_option(SANE_Handle handle, SANE_Int option, SANE_Action action,
                                void *value, SANE_Int *info)
{
    Handle *h = handle;
    if (info) *info = 0;
    if (!h || !value) return SANE_STATUS_INVAL;
    if (option == OPT_NUM_OPTIONS && action == SANE_ACTION_GET_VALUE) {
        *(SANE_Word *) value = h->num_options;
        return SANE_STATUS_GOOD;
    }
    if (action == SANE_ACTION_GET_VALUE) {
        if (option == OPT_MODE) strcpy((char *) value, h->mode);
        else if (option == OPT_RESOLUTION) *(SANE_Word *) value = h->resolution;
        else return SANE_STATUS_INVAL;
        return SANE_STATUS_GOOD;
    }
    if (action != SANE_ACTION_SET_VALUE) return SANE_STATUS_UNSUPPORTED;
    if (option == OPT_MODE) {
        const char *mode = value;
        if (strcmp(mode, "Gray") && strcmp(mode, "Color")) return SANE_STATUS_INVAL;
        strcpy(h->mode, mode);
    } else if (option == OPT_RESOLUTION) {
        SANE_Word dpi = *(SANE_Word *) value;
        if (dpi != 150 && dpi != 300 && dpi != 600) return SANE_STATUS_INVAL;
        h->resolution = dpi;
    } else return SANE_STATUS_INVAL;
    if (info) *info = SANE_INFO_RELOAD_PARAMS;
    return SANE_STATUS_GOOD;
}

SANE_Status sane_get_parameters(SANE_Handle handle, SANE_Parameters *p)
{
    Handle *h = handle;
    SANE_Int width, height, channels;
    if (!h || !p) return SANE_STATUS_INVAL;
    channels = strcmp(h->mode, "Gray") == 0 ? 1 : 3;
    width = ((SANE_Int) ((210.0 / 25.4) * h->resolution) + 31) & ~31;
    height = (SANE_Int) ((297.0 / 25.4) * h->resolution);
    if (h->image) {
        width = h->params.pixels_per_line;
        height = h->params.lines;
    }
    memset(p, 0, sizeof(*p));
    p->format = channels == 1 ? SANE_FRAME_GRAY : SANE_FRAME_RGB;
    p->last_frame = SANE_TRUE;
    p->pixels_per_line = width;
    p->lines = height;
    p->depth = 8;
    p->bytes_per_line = width * channels;
    if (h->image) h->params = *p;
    return SANE_STATUS_GOOD;
}

static int decode_jpeg(const char *path, Handle *h)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_state jerr;
    FILE *fp = fopen(path, "rb");
    size_t rowbytes, size;
    if (!fp) return -1;
    memset(&cinfo, 0, sizeof(cinfo));
    cinfo.err = jpeg_std_error(&jerr.pub);
    jerr.pub.error_exit = jpeg_fail;
    if (setjmp(jerr.jump)) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }
    jpeg_create_decompress(&cinfo);
    jpeg_stdio_src(&cinfo, fp);
    jpeg_read_header(&cinfo, TRUE);
    cinfo.out_color_space = strcmp(h->mode, "Gray") == 0 ? JCS_GRAYSCALE : JCS_RGB;
    jpeg_start_decompress(&cinfo);
    rowbytes = (size_t) cinfo.output_width * cinfo.output_components;
    size = rowbytes * cinfo.output_height;
    h->image = malloc(size);
    if (!h->image) {
        jpeg_destroy_decompress(&cinfo);
        fclose(fp);
        return -1;
    }
    while (cinfo.output_scanline < cinfo.output_height) {
        JSAMPROW row = h->image + (size_t) cinfo.output_scanline * rowbytes;
        jpeg_read_scanlines(&cinfo, &row, 1);
    }
    h->image_size = size;
    h->offset = 0;
    memset(&h->params, 0, sizeof(h->params));
    h->params.format = cinfo.output_components == 1 ? SANE_FRAME_GRAY : SANE_FRAME_RGB;
    h->params.last_frame = SANE_TRUE;
    h->params.pixels_per_line = cinfo.output_width;
    h->params.lines = cinfo.output_height;
    h->params.depth = 8;
    h->params.bytes_per_line = (SANE_Int) rowbytes;
    jpeg_finish_decompress(&cinfo);
    jpeg_destroy_decompress(&cinfo);
    fclose(fp);
    return 0;
}

SANE_Status sane_start(SANE_Handle handle)
{
    Handle *h = handle;
    const char *script = getenv("CANON_CHMP_SCRIPT");
    char path[] = "/tmp/canon-chmp-sane-XXXXXX.jpg";
    char dpi[16];
    int fd, status;
    pid_t pid;
    if (!h) return SANE_STATUS_INVAL;
    free(h->image); h->image = NULL; h->image_size = h->offset = 0;
    fd = mkstemps(path, 4);
    if (fd < 0) return SANE_STATUS_IO_ERROR;
    close(fd);
    snprintf(dpi, sizeof(dpi), "%d", h->resolution);
    if (!script || !*script) script = "/usr/lib/canon-chmp/chmp_scan.py";
    pid = fork();
    if (pid < 0) { unlink(path); return SANE_STATUS_IO_ERROR; }
    if (pid == 0) {
        int nullfd = open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            dup2(nullfd, STDOUT_FILENO);
            close(nullfd);
        }
        if (strcmp(h->mode, "Gray") == 0)
            execlp("python3", "python3", script, "--host", h->host, "--dpi",
                   dpi, "--gray", "--out", path, (char *) NULL);
        else
            execlp("python3", "python3", script, "--host", h->host, "--dpi",
                   dpi, "--out", path, (char *) NULL);
        _exit(127);
    }
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(path);
        return SANE_STATUS_IO_ERROR;
    }
    status = decode_jpeg(path, h);
    unlink(path);
    return status == 0 ? SANE_STATUS_GOOD : SANE_STATUS_IO_ERROR;
}

SANE_Status sane_read(SANE_Handle handle, SANE_Byte *data, SANE_Int max_length, SANE_Int *length)
{
    Handle *h = handle;
    size_t remaining, amount;
    if (!h || !data || !length || max_length < 0) return SANE_STATUS_INVAL;
    *length = 0;
    remaining = h->image_size - h->offset;
    if (!remaining) return SANE_STATUS_EOF;
    amount = remaining < (size_t) max_length ? remaining : (size_t) max_length;
    memcpy(data, h->image + h->offset, amount);
    h->offset += amount;
    *length = (SANE_Int) amount;
    return SANE_STATUS_GOOD;
}

void sane_cancel(SANE_Handle handle)
{
    Handle *h = handle;
    if (!h) return;
    free(h->image);
    h->image = NULL;
    h->image_size = h->offset = 0;
}

SANE_Status sane_set_io_mode(SANE_Handle handle, SANE_Bool non_blocking)
{
    (void) handle; (void) non_blocking;
    return SANE_STATUS_UNSUPPORTED;
}

SANE_Status sane_get_select_fd(SANE_Handle handle, SANE_Int *fd)
{
    (void) handle; (void) fd;
    return SANE_STATUS_UNSUPPORTED;
}
