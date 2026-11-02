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
 * @brief What the port is asked for
 */
typedef struct
{
    uint32_t frequency_hz;   /**< Clock of the PSRAM (0 = the default of the port) */
} dmpsram_port_config_t;

/**
 * @brief What the port reports back after a successful configuration
 */
typedef struct
{
    void     *memory_start;        /**< Base address of the mapped memory (the data bus) */
    uint32_t  memory_size_bytes;   /**< Size of the mapped memory */
    uint32_t  chip_size_bytes;     /**< Size of the chip (more than memory_size_bytes when the port does not map all of it) */
    uint32_t  frequency_hz;        /**< Clock of the PSRAM that the port programmed */
    uint32_t  vendor_id;           /**< Vendor id read from the chip */
    void     *code_start;          /**< The same memory seen by the instruction bus (NULL when there is no such alias) */
} dmpsram_port_result_t;

#endif /* DMPSRAM_TYPES_H */
