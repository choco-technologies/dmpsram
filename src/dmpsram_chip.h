#ifndef DMPSRAM_CHIP_H
#define DMPSRAM_CHIP_H

#include <stdint.h>
#include <stdbool.h>
#include "dmpsram_types.h"

/*
 * What is known about the chips without touching hardware: the sizes they report and the checks of a configuration. Header only, so the
 * tests of the host (where the module itself cannot run: there is no dmheap there) build them in.
 */

#define DMPSRAM_QUAD_ID_KGD             0x5D
#define DMPSRAM_QUAD_EID_64MBIT_TRIAL   0x26

/** The size of a quad PSRAM chip from the 3 bytes its READ ID (0x9F) answers, the first one in the lowest byte: manufacturer, KGD (0x5D when
 *  the chip is known good), EID (the density in bits 7:5; 0x26 is the 64 Mbit trial part). 0 when it is not a PSRAM of this family. */
static inline uint32_t dmpsram_quad_chip_size(uint32_t id)
{
    uint32_t kgd = (id >> 8) & 0xFFU;
    uint32_t eid = (id >> 16) & 0xFFU;
    if (kgd != DMPSRAM_QUAD_ID_KGD)
        return 0U;
    if (eid == DMPSRAM_QUAD_EID_64MBIT_TRIAL)
        return 8U << 20;
    switch ((eid >> 5) & 0x7U)      /* 16, 32, 64 Mbit */
    {
        case 0x0: return 2U << 20;
        case 0x1: return 4U << 20;
        case 0x2: return 8U << 20;
        default:  return 0U;
    }
}

/** The size of an octal PSRAM chip (AP Memory) from the density field of its MR2 (0x1: 4 MB, 0x3: 8 MB, 0x5: 16 MB, 0x7: 32 MB); 0 for a
 *  field that is not a density. */
static inline uint32_t dmpsram_octal_chip_size(uint32_t density)
{
    switch (density)
    {
        case 0x1: return 4U << 20;
        case 0x3: return 8U << 20;
        case 0x5: return 16U << 20;
        case 0x7: return 32U << 20;
        default:  return 0U;
    }
}

/** Whether the mode, the heap usage and its alignment make a configuration the driver can run. */
static inline bool dmpsram_config_valid(dmpsram_mode_t mode, dmpsram_heap_usage_t heap_usage, uint32_t heap_alignment)
{
    if (mode != dmpsram_mode_octal && mode != dmpsram_mode_quad)
        return false;
    if (heap_usage > dmpsram_heap_usage_tagged)
        return false;
    if (heap_usage != dmpsram_heap_usage_none && (heap_alignment == 0U || (heap_alignment & (heap_alignment - 1U)) != 0U))
        return false;
    return true;
}

#endif /* DMPSRAM_CHIP_H */
