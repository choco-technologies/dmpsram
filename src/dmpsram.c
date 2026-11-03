#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmpsram.h"
#include "dmpsram_port.h"
#include "dmdrvi.h"
#include "dmini.h"
#include "dmheap.h"
#include <errno.h>
#include <string.h>

#define DMPSRAM_CONTEXT_MAGIC   0x4D505352  /* 'MPSR' */
#define DMPSRAM_DEVICE_NAME     "psram"

/**
 * @brief DMDRVI context structure
 */
struct dmdrvi_context
{
    uint32_t                magic;      /**< Magic number for validation */
    dmpsram_config_t        config;     /**< Configuration parameters */
    dmpsram_port_result_t   result;     /**< Result reported back by the port layer */
    dmheap_context_t       *heap_ctx;   /**< dmheap context (only when config.heap_usage requests it) */
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
    context->config.frequency_hz   = (uint32_t)dmini_get_int(config, section, "frequency", 0);
    context->config.heap_usage     = string_to_heap_usage(dmini_get_string(config, section, "heap_usage", "none"));
    context->config.heap_alignment = (uint32_t)dmini_get_int(config, section, "heap_alignment", 16);
    context->config.verify         = strcmp(dmini_get_string(config, section, "verify", "on"), "off") != 0;
}

/**
 * @brief Verify the freshly-configured PSRAM actually holds data.
 *
 * Writes and reads back patterns with 32-bit, 16-bit and 8-bit accesses at the start, in the middle and at the end of the
 * mapped region, and checks that the three places do not alias to the same cell. A controller that is set up wrongly (the
 * timing, the dummy cycles) gives memory that reads back, but not what was written - and the heap that is built on it would
 * only fail much later, as corrupted data.
 */
static int verify_access(const dmpsram_port_result_t *result)
{
    volatile uint32_t *base = (volatile uint32_t *)result->memory_start;
    size_t words = result->memory_size_bytes / sizeof(uint32_t);
    size_t at[3] = { 0, words / 2, words - 16 };
    int ok = 1;

    for (int pass = 0; pass < 2; pass++)
    {
        uint32_t pattern = pass == 0 ? 0xA5A5A5A5U : 0x5A5A5A5AU;
        for (int p = 0; p < 3; p++)
            for (size_t i = 0; i < 16; i++)
                base[at[p] + i] = pattern ^ (uint32_t)((p + 1) * 0x01010101U) ^ (uint32_t)(i * 0x00000F11U);
        for (int p = 0; p < 3; p++)
            for (size_t i = 0; i < 16; i++)
                if (base[at[p] + i] != (pattern ^ (uint32_t)((p + 1) * 0x01010101U) ^ (uint32_t)(i * 0x00000F11U)))
                    ok = 0;
    }

    volatile uint8_t *b8 = (volatile uint8_t *)result->memory_start;
    for (int i = 0; i < 16; i++) b8[i + 3] = (uint8_t)(0x30 + i);
    for (int i = 0; i < 16; i++) if (b8[i + 3] != (uint8_t)(0x30 + i)) ok = 0;

    if (!ok)
        DMOD_LOG_ERROR("PSRAM: access verification FAILED at %p (%u bytes) - the controller does not read back what was written\n",
            result->memory_start, (unsigned)result->memory_size_bytes);
    return ok ? 0 : -EIO;
}

static int configure(dmdrvi_context_t context)
{
    dmpsram_config_t *c = &context->config;
    dmpsram_port_config_t port_config = { .frequency_hz = c->frequency_hz };

    int ret = dmpsram_port_configure(&port_config, &context->result);
    if (ret != 0)
    {
        DMOD_LOG_ERROR("PSRAM: the port could not configure the memory (%d)\n", ret);
        return ret;
    }

    if (c->verify)
    {
        ret = verify_access(&context->result);
        if (ret != 0)
        {
            dmpsram_port_unconfigure();
            return ret;
        }
    }

    context->heap_ctx = NULL;
    if (c->heap_usage != dmpsram_heap_usage_none)
    {
        context->heap_ctx = dmheap_init(context->result.memory_start, context->result.memory_size_bytes, c->heap_alignment);
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

    DMOD_LOG_INFO("PSRAM: %u bytes @ %p of %u bytes, %u Hz\n", (unsigned)context->result.memory_size_bytes,
        context->result.memory_start, (unsigned)context->result.chip_size_bytes, (unsigned)context->result.frequency_hz);
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

    if (configure(context) != 0)
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

    dmpsram_port_unconfigure();
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
    if (!is_valid_context(context) || buffer == NULL || (dmdrvi_size_t)offset >= (dmdrvi_size_t)context->result.memory_size_bytes)
        return 0;

    dmdrvi_size_t available = (dmdrvi_size_t)context->result.memory_size_bytes - (dmdrvi_size_t)offset;
    size_t to_copy = (size < available) ? size : (size_t)available;
    memcpy(buffer, (const uint8_t *)context->result.memory_start + offset, to_copy);
    return (dmdrvi_ssize_t)to_copy;
}

dmod_dmdrvi_dif_api_declaration(2.0, dmpsram, dmdrvi_ssize_t, _write, ( dmdrvi_context_t context, void* handle, const void* buffer, size_t size, dmdrvi_offset_t offset ))
{
    (void)handle;
    if (offset < 0) return -EINVAL;
    if (size > (size_t)INT64_MAX) return -EOVERFLOW;
    if (!is_valid_context(context) || buffer == NULL || (dmdrvi_size_t)offset >= (dmdrvi_size_t)context->result.memory_size_bytes)
        return 0;

    dmdrvi_size_t available = (dmdrvi_size_t)context->result.memory_size_bytes - (dmdrvi_size_t)offset;
    size_t to_copy = (size < available) ? size : (size_t)available;
    memcpy((uint8_t *)context->result.memory_start + offset, buffer, to_copy);
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
            *(void **)arg = context->result.memory_start;
            return 0;
        case dmpsram_ioctl_cmd_get_memory_size:
            *(uint32_t *)arg = context->result.memory_size_bytes;
            return 0;
        case dmpsram_ioctl_cmd_get_frequency:
            *(uint32_t *)arg = context->result.frequency_hz;
            return 0;
        case dmpsram_ioctl_cmd_get_heap_context:
            *(dmheap_context_t **)arg = context->heap_ctx;
            return 0;
        case dmpsram_ioctl_cmd_get_code_start:
            *(void **)arg = context->result.code_start;
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

    stat->size = context->result.memory_size_bytes;
    stat->mode = 0666;
    return 0;
}
