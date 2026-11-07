#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmpsram.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmheap.h"
#include "dmqspi.h"
#include "dmpsram_chip.h"
#include <errno.h>
#include <string.h>

/*
 * The PSRAM chip behind a dmqspi controller. This module knows the CHIP: how to wake it up, how big it is, which commands read
 * and write it. The controller - the pads, the clock, the commands on the bus, the window in the address space of the CPU - is
 * the business of dmqspi (and its port): the driver sends commands to its node (DMQSPI_IOCTL_*) and asks it for the window.
 *
 * The controller is reached by a function call (dmqspi_ioctl), not through its node: the memory has to be a heap before the drivers that
 * follow this one are created, and the file system of the devices is not there yet. The module requires dmqspi, so dmdevfs configures the
 * controller first; the number of the controller (qspi_instance) is the `instance` of its section.
 */

#define DMPSRAM_CONTEXT_MAGIC   0x4D505352  /* 'MPSR' */
#define DMPSRAM_DEVICE_NAME     "psram"

/* ---- Commands of the quad chips (APS6404L, ESP-PSRAM64H: 64 Mbit) ---- */
#define QUAD_CMD_FAST_READ_QUAD     0xEB
#define QUAD_CMD_QUAD_WRITE         0x38
#define QUAD_CMD_ENTER_QPI          0x35
#define QUAD_CMD_EXIT_QPI           0xF5
#define QUAD_CMD_RESET_ENABLE       0x66
#define QUAD_CMD_RESET              0x99
#define QUAD_CMD_READ_ID            0x9F
#define QUAD_READ_DUMMY_CYCLES      6

/* ---- Octal chips (AP Memory, 1.8 V; the OPI DTR mode: the byte of an instruction goes out twice, which the controller does) ---- */
#define OCTAL_CMD_SYNC_READ         0x00
#define OCTAL_CMD_SYNC_WRITE        0x80
#define OCTAL_CMD_MR_READ           0x40
#define OCTAL_CMD_MR_WRITE          0xC0
#define OCTAL_READ_DUMMY_CYCLES     10
#define OCTAL_WRITE_DUMMY_CYCLES    5
#define OCTAL_VENDOR_ID_AP          0xD
#define OCTAL_MR0_READ_LATENCY      2       /* the read latency code (10 cycles at the clocks up to 66 MHz) */

/**
 * @brief DMDRVI context structure
 */
struct dmdrvi_context
{
    uint32_t                magic;      /**< Magic number for validation */
    dmpsram_config_t        config;     /**< Configuration parameters */
    void                   *memory_start;       /**< Base address of the mapped memory (the data bus) */
    uint32_t                memory_size_bytes;  /**< Size of the mapped memory */
    uint32_t                chip_size_bytes;    /**< Size of the chip */
    uint32_t                frequency_hz;       /**< Clock the controller runs the chip at */
    void                   *code_start;         /**< The same memory seen by the instruction bus (NULL: no such alias) */
    dmheap_context_t       *heap_ctx;   /**< dmheap context (only when config.heap_usage requests it) */
    dmqspi_instance_t       qspi;       /**< The controller the chip is wired to */
};

static int is_valid_context(dmdrvi_context_t context)
{
    return (context != NULL && context->magic == DMPSRAM_CONTEXT_MAGIC);
}

static dmpsram_heap_usage_t string_to_heap_usage(const char *s)
{
    if (s != NULL && strcmp(s, "heap") == 0) return dmpsram_heap_usage_use_as_heap;
    if (s != NULL && strcmp(s, "tagged") == 0) return dmpsram_heap_usage_tagged;
    return dmpsram_heap_usage_none;
}

static void read_config_parameters(dmdrvi_context_t context, dmini_context_t config)
{
    const char *section = "dmpsram";
    const char *mode = dmini_get_string(config, section, "mode", "octal");
    context->config.mode           = strcmp(mode, "quad") == 0 ? dmpsram_mode_quad : dmpsram_mode_octal;
    context->config.heap_usage     = string_to_heap_usage(dmini_get_string(config, section, "heap_usage", "none"));
    context->qspi                  = (dmqspi_instance_t)dmini_get_int(config, section, "qspi_instance", 1);
    context->config.heap_alignment = (uint32_t)dmini_get_int(config, section, "heap_alignment", 16);
    context->config.verify         = strcmp(dmini_get_string(config, section, "verify", "on"), "off") != 0;
}

/* ---- The chips ---- */

/** One command of the quad chip on the bus of the controller: the instruction (and the address, the data) on one line while the chip
 *  is in the SPI mode, on four while it is in the QPI mode. */
static int quad_command(dmqspi_instance_t qspi, uint8_t instruction, bool qpi, uint32_t data_bytes, void *data)
{
    dmqspi_transfer_t t;
    memset(&t, 0, sizeof(t));
    t.cmd.instruction = instruction;
    t.cmd.instruction_lines = qpi ? dmqspi_lines_4 : dmqspi_lines_1;
    if (instruction == QUAD_CMD_READ_ID)
    {
        t.cmd.address = 0U;
        t.cmd.address_lines = dmqspi_lines_1;
        t.cmd.address_size = dmqspi_addr_24bit;
    }
    if (data_bytes != 0U)
    {
        t.cmd.data_lines = dmqspi_lines_1;
        t.cmd.data_size = data_bytes;
        t.dir = dmqspi_dir_read;
        t.data = data;
    }
    return dmqspi_ioctl(qspi, DMQSPI_IOCTL_TRANSFER, &t);
}

/** Wakes a quad chip up: out of the QPI mode it may have been left in, its id, the reset, into the QPI mode. Returns its size or a negative errno. */
static int quad_bring_up(dmqspi_instance_t qspi, uint32_t *id_out)
{
    uint8_t id_bytes[4] = { 0 };
    uint32_t id = 0;
    int ret = quad_command(qspi, QUAD_CMD_EXIT_QPI, true, 0U, NULL);
    if (ret != 0) return ret;

    for (int attempt = 0; attempt < 2; attempt++)       /* the first read of some 16 Mbit parts is wrong */
    {
        ret = quad_command(qspi, QUAD_CMD_READ_ID, false, 3U, id_bytes);
        if (ret != 0) return ret;
        id = (uint32_t)id_bytes[0] | ((uint32_t)id_bytes[1] << 8) | ((uint32_t)id_bytes[2] << 16);
        if (dmpsram_quad_chip_size(id) != 0U) break;
    }
    *id_out = id;
    uint32_t size = dmpsram_quad_chip_size(id);
    if (size == 0U)
        return -ENODEV;

    ret = quad_command(qspi, QUAD_CMD_RESET_ENABLE, false, 0U, NULL);
    if (ret == 0) ret = quad_command(qspi, QUAD_CMD_RESET, false, 0U, NULL);
    if (ret == 0) ret = quad_command(qspi, QUAD_CMD_ENTER_QPI, false, 0U, NULL);
    return ret != 0 ? ret : (int)size;
}

/** A mode register of the octal chip (the 32-bit address is the number of the register): `bytes` of it read or written. */
static int octal_register(dmqspi_instance_t qspi, bool write, uint32_t reg, uint8_t *data, uint32_t bytes)
{
    dmqspi_transfer_t t;
    memset(&t, 0, sizeof(t));
    t.cmd.instruction = write ? OCTAL_CMD_MR_WRITE : OCTAL_CMD_MR_READ;
    t.cmd.instruction_lines = dmqspi_lines_8;
    t.cmd.address = reg;
    t.cmd.address_lines = dmqspi_lines_8;
    t.cmd.address_size = dmqspi_addr_32bit;
    t.cmd.dummy_cycles = write ? 0 : OCTAL_READ_DUMMY_CYCLES;
    t.cmd.data_lines = dmqspi_lines_8;
    t.cmd.data_size = bytes;
    t.cmd.ddr = true;
    t.dir = write ? dmqspi_dir_write : dmqspi_dir_read;
    t.data = data;
    return dmqspi_ioctl(qspi, DMQSPI_IOCTL_TRANSFER, &t);
}

/** Wakes an octal chip up: the latency and the fixed latency in MR0, then what it says about itself (the vendor in MR1, the density in
 *  MR2). Returns its size or a negative errno; `mr` gets MR0..MR4 and MR8 (the registers read, for the log). */
static int octal_bring_up(dmqspi_instance_t qspi, uint8_t mr[6])
{
    uint8_t reg[2] = { 0 };
    int ret = octal_register(qspi, false, 0x0U, reg, 2U);                   /* MR0, MR1 */
    if (ret != 0) return ret;
    /* read latency, fixed latency, drive strength: MR0 (the next byte of the 16 bits is MR1, read-only) */
    reg[0] = (uint8_t)((reg[0] & ~0x3FU) | (0U /* drive strength */) | (OCTAL_MR0_READ_LATENCY << 2) | (1U << 5 /* fixed latency */));
    ret = octal_register(qspi, true, 0x0U, reg, 2U);
    if (ret != 0) return ret;

    memset(mr, 0, 6);
    ret = octal_register(qspi, false, 0x0U, &mr[0], 2U);                    /* MR0, MR1 */
    if (ret == 0) ret = octal_register(qspi, false, 0x2U, &mr[2], 2U);      /* MR2, MR3 */
    if (ret == 0) ret = octal_register(qspi, false, 0x4U, &mr[4], 1U);      /* MR4 */
    if (ret == 0) ret = octal_register(qspi, false, 0x8U, &mr[5], 1U);      /* MR8 */
    if (ret != 0) return ret;

    if ((mr[1] & 0x1FU) != OCTAL_VENDOR_ID_AP)
        return -ENODEV;
    uint32_t size = dmpsram_octal_chip_size(mr[2] & 0x7U);
    return size != 0U ? (int)size : -ENODEV;
}

static int octal_map(dmqspi_instance_t qspi, dmqspi_mapped_rw_t *mapped)
{
    memset(mapped, 0, sizeof(*mapped));
    dmqspi_command_t *rd = &mapped->request.read_cmd;
    rd->instruction = OCTAL_CMD_SYNC_READ;
    rd->instruction_lines = dmqspi_lines_8;
    rd->address_lines = dmqspi_lines_8;
    rd->address_size = dmqspi_addr_32bit;
    rd->dummy_cycles = OCTAL_READ_DUMMY_CYCLES;
    rd->data_lines = dmqspi_lines_8;
    rd->ddr = true;

    dmqspi_command_t *wr = &mapped->request.write_cmd;
    wr->instruction = OCTAL_CMD_SYNC_WRITE;
    wr->instruction_lines = dmqspi_lines_8;
    wr->address_lines = dmqspi_lines_8;
    wr->address_size = dmqspi_addr_32bit;
    wr->dummy_cycles = OCTAL_WRITE_DUMMY_CYCLES;
    wr->data_lines = dmqspi_lines_8;
    wr->ddr = true;
    mapped->request.writable = true;
    return dmqspi_ioctl(qspi, DMQSPI_IOCTL_ENTER_MAPPED_RW, mapped);
}

/** The window the CPU reads and writes the chip through: the commands the controller sends for the accesses. */
static int quad_map(dmqspi_instance_t qspi, dmqspi_mapped_rw_t *mapped)
{
    memset(mapped, 0, sizeof(*mapped));
    dmqspi_command_t *rd = &mapped->request.read_cmd;
    rd->instruction = QUAD_CMD_FAST_READ_QUAD;
    rd->instruction_lines = dmqspi_lines_4;
    rd->address_lines = dmqspi_lines_4;
    rd->address_size = dmqspi_addr_24bit;
    rd->dummy_cycles = QUAD_READ_DUMMY_CYCLES;
    rd->data_lines = dmqspi_lines_4;

    dmqspi_command_t *wr = &mapped->request.write_cmd;
    wr->instruction = QUAD_CMD_QUAD_WRITE;
    wr->instruction_lines = dmqspi_lines_4;
    wr->address_lines = dmqspi_lines_4;
    wr->address_size = dmqspi_addr_24bit;
    wr->data_lines = dmqspi_lines_4;
    mapped->request.writable = true;
    return dmqspi_ioctl(qspi, DMQSPI_IOCTL_ENTER_MAPPED_RW, mapped);
}

/**
 * Writes patterns through the data cache, pushes them out to the chip, throws the cache away and reads them back (through the
 * data bus and, when there is one, through the instruction bus). A chip that is set up wrongly (the timing, the dummy cycles)
 * gives memory that reads back, but not what was written - and the heap built on it would only fail much later, as corrupted
 * data. Words only: the instruction bus does not read bytes.
 */
static int verify_access(dmdrvi_context_t context)
{
    volatile uint32_t *data = (volatile uint32_t *)context->memory_start;
    volatile uint32_t *code = (volatile uint32_t *)context->code_start;
    uint32_t words = context->memory_size_bytes / sizeof(uint32_t);
    uint32_t at[3] = { 0U, words / 2U, words - 32U };
    int ok = 1;

    for (int p = 0; p < 3; p++)
        for (uint32_t i = 0; i < 32U; i++)
            data[at[p] + i] = 0xC3A5F00DU ^ (at[p] * 0x9E3779B1U) ^ (i * 0x01010101U);

    for (int p = 0; p < 3; p++)
    {
        uint32_t range[2] = { at[p] * (uint32_t)sizeof(uint32_t), 32U * (uint32_t)sizeof(uint32_t) };
        if (dmqspi_ioctl(context->qspi, DMQSPI_IOCTL_INVALIDATE_MAPPED, range) != 0)
            ok = 0;
    }

    for (int p = 0; p < 3; p++)
        for (uint32_t i = 0; i < 32U; i++)
        {
            uint32_t expected = 0xC3A5F00DU ^ (at[p] * 0x9E3779B1U) ^ (i * 0x01010101U);
            if (data[at[p] + i] != expected) ok = 0;
            if (code != NULL && code[at[p] + i] != expected) ok = 0;
        }

    volatile uint8_t *b8 = (volatile uint8_t *)context->memory_start;
    for (int i = 0; i < 16; i++) b8[i + 3] = (uint8_t)(0x30 + i);
    for (int i = 0; i < 16; i++) if (b8[i + 3] != (uint8_t)(0x30 + i)) ok = 0;

    if (!ok)
        DMOD_LOG_ERROR("PSRAM: access verification FAILED at %p (%u bytes) - the memory does not read back what was written\n",
            context->memory_start, (unsigned)context->memory_size_bytes);
    return ok ? 0 : -EIO;
}

static int start(dmdrvi_context_t context)
{
    dmpsram_config_t *c = &context->config;

    dmqspi_instance_t qspi = context->qspi;
    int chip = -ENOTSUP;
    dmqspi_mapped_rw_t mapped;
    uint32_t id = 0;
    if (c->mode == dmpsram_mode_quad)
    {
        chip = quad_bring_up(qspi, &id);
        if (chip == -ENODEV)
        {
            DMOD_LOG_ERROR("PSRAM: no answer of the chip (id 0x%06X: not a quad PSRAM of this family)\n", (unsigned)id);
        }
        else if (chip > 0)
        {
            DMOD_LOG_INFO("PSRAM: quad chip, id 0x%06X, %u MB\n", (unsigned)id, (unsigned)((uint32_t)chip >> 20));
        }
    }
    else
    {
        uint8_t mr[6];
        chip = octal_bring_up(qspi, mr);
        if (chip == -ENODEV)
        {
            DMOD_LOG_ERROR("PSRAM: no answer of the chip (vendor id 0x%02X, expected 0x%02X)\n", (unsigned)(mr[1] & 0x1FU), (unsigned)OCTAL_VENDOR_ID_AP);
        }
        else if (chip > 0)
        {
            DMOD_LOG_INFO("PSRAM: octal chip, vendor 0x%02X, density 0x%02X (%u MB), MR0 0x%02X, MR3 0x%02X\n", (unsigned)(mr[1] & 0x1FU),
                (unsigned)(mr[2] & 0x7U), (unsigned)((uint32_t)chip >> 20), (unsigned)mr[0], (unsigned)mr[3]);
        }
    }
    if (chip <= 0)
        return chip < 0 ? chip : -ENODEV;

    int ret = c->mode == dmpsram_mode_quad ? quad_map(qspi, &mapped) : octal_map(qspi, &mapped);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("PSRAM: the controller could not map the chip (%d)\n", ret);
        return ret;
    }

    context->memory_start = (void *)mapped.info.base;
    context->memory_size_bytes = mapped.info.size;
    context->chip_size_bytes = (uint32_t)chip;
    context->frequency_hz = mapped.info.clock_hz;
    context->code_start = (void *)mapped.info.code_base;

    if (c->verify && verify_access(context) != 0)
    {
        dmqspi_ioctl(qspi, DMQSPI_IOCTL_EXIT_MAPPED, NULL);
        return -EIO;
    }

    context->heap_ctx = NULL;
    if (c->heap_usage != dmpsram_heap_usage_none)
    {
        context->heap_ctx = dmheap_init(context->memory_start, context->memory_size_bytes, c->heap_alignment);
        if (context->heap_ctx == NULL)
        {
            DMOD_LOG_ERROR("PSRAM: failed to register the memory as an additional heap\n");
        }
        else
        {
            /* The tags let the memory be asked for by name (Dmod_MallocTagged). */
            dmheap_set_context_name(context->heap_ctx, "psram,external,slow");
            /* dmheap_init() has put the heap on the default list as the newest one, which the allocations that name no heap try
             * first. A 'tagged' heap is taken off the list. */
            if (c->heap_usage == dmpsram_heap_usage_tagged)
                dmheap_remove_default_context(context->heap_ctx);
        }
    }

    DMOD_LOG_INFO("PSRAM: %u bytes @ %p of %u bytes, %u Hz\n", (unsigned)context->memory_size_bytes,
        context->memory_start, (unsigned)context->chip_size_bytes, (unsigned)context->frequency_hz);
    return 0;
}

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    DMOD_LOG_INFO("DMPSRAM interface module initialized\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMPSRAM interface module deinitialized\n");
    return 0;
}

/* ---- DMDRVI interface ---- */

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, dmdrvi_context_t, _create, ( dmini_context_t config, dmdrvi_dev_num_t* dev_num ))
{
    if (config == NULL || dev_num == NULL)
    {
        DMOD_LOG_ERROR("Invalid parameters to dmpsram_dmdrvi_create\n");
        return NULL;
    }

    dmdrvi_context_t context = Dmod_Malloc(sizeof(struct dmdrvi_context));
    if (context == NULL)
        return NULL;

    memset(context, 0, sizeof(*context));
    context->magic = DMPSRAM_CONTEXT_MAGIC;
    read_config_parameters(context, config);

    if (!dmpsram_config_valid(context->config.mode, context->config.heap_usage, context->config.heap_alignment))
    {
        DMOD_LOG_ERROR("Invalid configuration of the PSRAM (heap_alignment must be a power of two)\n");
        Dmod_Free(context);
        return NULL;
    }

    if (start(context) != 0)
    {
        DMOD_LOG_ERROR("Failed to create DMDRVI context with provided configuration\n");
        Dmod_Free(context);
        return NULL;
    }

    /* One PSRAM per chip: /dev/psram */
    dev_num->flags = DMDRVI_NUM_MAJOR | DMDRVI_NUM_ALT_NAME;
    dev_num->major = 0;
    memcpy(dev_num->alt_name, DMPSRAM_DEVICE_NAME, sizeof(DMPSRAM_DEVICE_NAME));

    return context;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, void, _free, ( dmdrvi_context_t context ))
{
    if (!is_valid_context(context))
        return;

    /* Off the default list before the memory goes away, so a NULL-context allocation racing with this cannot land in it.
     * dmheap has no way to destroy the context itself. */
    if (context->heap_ctx != NULL)
    {
        dmheap_remove_default_context(context->heap_ctx);
        context->heap_ctx = NULL;
    }

    dmqspi_ioctl(context->qspi, DMQSPI_IOCTL_EXIT_MAPPED, NULL);
    context->magic = 0;
    Dmod_Free(context);
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, void*, _open, ( dmdrvi_context_t context, int flags, const dmdrvi_dev_num_t* dev_num ))
{
    (void)flags; (void)dev_num;
    return is_valid_context(context) ? (void *)context : NULL;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, void, _close, ( dmdrvi_context_t context, void* handle ))
{
    (void)context; (void)handle;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, dmdrvi_ssize_t, _read, ( dmdrvi_context_t context, void* handle, void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void)handle;
    if (offset < 0) return -EINVAL;
    if (size > (size_t)INT64_MAX) return -EOVERFLOW;
    if (!is_valid_context(context) || buffer == NULL || (dmdrvi_size_t)offset >= (dmdrvi_size_t)context->memory_size_bytes)
        return 0;

    dmdrvi_size_t available = (dmdrvi_size_t)context->memory_size_bytes - (dmdrvi_size_t)offset;
    size_t to_copy = (size < available) ? size : (size_t)available;
    memcpy(buffer, (const uint8_t *)context->memory_start + offset, to_copy);
    return (dmdrvi_ssize_t)to_copy;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, dmdrvi_ssize_t, _write, ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void)handle;
    if (offset < 0) return -EINVAL;
    if (size > (size_t)INT64_MAX) return -EOVERFLOW;
    if (!is_valid_context(context) || buffer == NULL || (dmdrvi_size_t)offset >= (dmdrvi_size_t)context->memory_size_bytes)
        return 0;

    dmdrvi_size_t available = (dmdrvi_size_t)context->memory_size_bytes - (dmdrvi_size_t)offset;
    size_t to_copy = (size < available) ? size : (size_t)available;
    memcpy((uint8_t *)context->memory_start + offset, buffer, to_copy);
    return (dmdrvi_ssize_t)to_copy;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, int, _ioctl, ( dmdrvi_context_t context, void* handle, int command, void* arg ))
{
    (void)handle;
    if (!is_valid_context(context))
    {
        DMOD_LOG_ERROR("Invalid DMDRVI context in dmpsram_dmdrvi_ioctl\n");
        return -EINVAL;
    }

    /* Not an error: generic clients (e.g. dmdevfs probing every node for the block/monitor classes) send standard
     * DMDRVI_IOCTL_* commands the PSRAM does not implement - answer -ENOTTY quietly, as dmdrvi expects. */
    if (command < dmpsram_ioctl_cmd_get_memory_start || command >= dmpsram_ioctl_cmd_max)
        return -ENOTTY;
    if (arg == NULL)
        return -EINVAL;

    switch (command)
    {
        case dmpsram_ioctl_cmd_get_memory_start:
            *(void **)arg = context->memory_start;
            return 0;
        case dmpsram_ioctl_cmd_get_memory_size:
            *(uint32_t *)arg = context->memory_size_bytes;
            return 0;
        case dmpsram_ioctl_cmd_get_frequency:
            *(uint32_t *)arg = context->frequency_hz;
            return 0;
        case dmpsram_ioctl_cmd_get_heap_context:
            *(dmheap_context_t **)arg = context->heap_ctx;
            return 0;
        case dmpsram_ioctl_cmd_get_code_start:
            *(void **)arg = context->code_start;
            return 0;
        default:
            return -ENOTTY;
    }
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, int, _flush, ( dmdrvi_context_t context, void* handle ))
{
    (void)handle;
    return is_valid_context(context) ? 0 : -EINVAL;     /* memory-mapped, nothing buffered by this driver */
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, int, _stat, ( dmdrvi_context_t context, const char* path, dmdrvi_stat_t* stat ))
{
    (void)path;
    if (!is_valid_context(context) || stat == NULL)
        return -EINVAL;

    stat->size = context->memory_size_bytes;
    stat->mode = 0666;
    return 0;
}
