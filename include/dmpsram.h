#ifndef DMPSRAM_H
#define DMPSRAM_H

#include <stdint.h>
#include <stdbool.h>
#include "dmpsram_defs.h"
#include "dmpsram_types.h"
#include "dmdrvi_ioctl.h"

/**
 * @brief PSRAM driver configuration (the [dmpsram] section of the driver's ini)
 */
typedef struct
{
    dmpsram_mode_t        mode;           /**< The bus of the chip: octal (default) or quad */
    dmpsram_heap_usage_t  heap_usage;     /**< Whether to register the memory with dmheap */
    uint32_t              heap_alignment; /**< Alignment of the dmheap context (only with heap_usage=heap) */
    bool                  verify;         /**< Check that the memory holds data before it is used (default on) */
} dmpsram_config_t;

/**
 * @brief IOCTL commands for the DMPSRAM device
 */
typedef enum
{
    /* Private commands start at DMDRVI_IOCTL_CUSTOM_BASE (dmdrvi_ioctl.h) so they never collide with the standard
     * DMDRVI_IOCTL_* commands that dmdevfs and other generic clients send to any node. */
    dmpsram_ioctl_cmd_get_memory_start = DMDRVI_IOCTL_CUSTOM_BASE, /**< arg = void** - the mapped memory */
    dmpsram_ioctl_cmd_get_memory_size,          /**< arg = uint32_t* - size in bytes of the mapped memory */
    dmpsram_ioctl_cmd_get_frequency,            /**< arg = uint32_t* - clock of the PSRAM in Hz */
    dmpsram_ioctl_cmd_get_heap_context,         /**< arg = dmheap_context_t** - heap context, or NULL if not used as heap */
    dmpsram_ioctl_cmd_get_code_start,           /**< arg = void** - the memory seen by the instruction bus, or NULL */

    dmpsram_ioctl_cmd_max
} dmpsram_ioctl_cmd_t;

#endif /* DMPSRAM_H */
