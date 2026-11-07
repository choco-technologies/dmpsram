#ifndef DMPSRAM_TYPES_H
#define DMPSRAM_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Whether the PSRAM is also registered as an additional dmheap context
 */
typedef enum
{
    dmpsram_heap_usage_none = 0,     /**< Do not use the memory as a heap */
    dmpsram_heap_usage_use_as_heap,  /**< Register the mapped region with dmheap_init() and the default heap list */
    dmpsram_heap_usage_tagged,       /**< Register the mapped region with dmheap_init(), but not on the default list: only the callers that ask for the tags ("psram") get memory from it */
} dmpsram_heap_usage_t;

/**
 * @brief How the chip is wired to the controller (the lines of its bus)
 */
typedef enum
{
    dmpsram_mode_octal = 0,          /**< 8 data lines, DDR (AP Memory, ESP32-S3R8 / FN16R8) */
    dmpsram_mode_quad,               /**< 4 data lines, the chip in the QPI mode (APS6404, ESP-PSRAM64H, 64 Mbit) */
} dmpsram_mode_t;

#endif /* DMPSRAM_TYPES_H */
