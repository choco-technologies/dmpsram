#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmpsram.h"
#include "dmpsram_chip.h"

DMOD_TEST_STEP(quad_chip_size_from_the_id)
{
    /* the id as the chip answers: manufacturer (0x0D), KGD 0x5D, EID: the density in bits 7:5 */
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0x5D0D | (0x40U << 16) | 0x00U), 8U << 20);      /* 64 Mbit */
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0x5D0D | (0x20U << 16)), 4U << 20);               /* 32 Mbit */
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0x5D0D | (0x00U << 16)), 2U << 20);               /* 16 Mbit */
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0x5D0D | (0x26U << 16)), 8U << 20);               /* the 64 Mbit trial part */
}

DMOD_TEST_STEP(quad_chip_size_rejects_what_is_not_a_psram)
{
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0x000000U), 0U);
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0xFFFFFFU), 0U);
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0x5C0D | (0x40U << 16)), 0U);                    /* KGD is not 0x5D */
    DMOD_TEST_EXPECT_EQ(dmpsram_quad_chip_size(0x5D0D | (0x60U << 16)), 0U);                    /* a density it does not know */
}

DMOD_TEST_STEP(octal_chip_size_from_the_density)
{
    DMOD_TEST_EXPECT_EQ(dmpsram_octal_chip_size(0x1), 4U << 20);
    DMOD_TEST_EXPECT_EQ(dmpsram_octal_chip_size(0x3), 8U << 20);
    DMOD_TEST_EXPECT_EQ(dmpsram_octal_chip_size(0x5), 16U << 20);
    DMOD_TEST_EXPECT_EQ(dmpsram_octal_chip_size(0x7), 32U << 20);
    DMOD_TEST_EXPECT_EQ(dmpsram_octal_chip_size(0x0), 0U);
    DMOD_TEST_EXPECT_EQ(dmpsram_octal_chip_size(0x2), 0U);
}

DMOD_TEST_STEP(validate_accepts_both_modes)
{
    dmpsram_config_t c = { .mode = dmpsram_mode_octal, .heap_usage = dmpsram_heap_usage_use_as_heap, .heap_alignment = 16, .verify = true };
    DMOD_TEST_EXPECT_TRUE(dmpsram_config_valid(c.mode, c.heap_usage, c.heap_alignment));
    c.mode = dmpsram_mode_quad;
    DMOD_TEST_EXPECT_TRUE(dmpsram_config_valid(c.mode, c.heap_usage, c.heap_alignment));
}

DMOD_TEST_STEP(validate_rejects_bad_configurations)
{
    dmpsram_config_t c = { .mode = dmpsram_mode_quad, .heap_usage = dmpsram_heap_usage_use_as_heap, .heap_alignment = 16, .verify = true };
    c.heap_alignment = 24;                       /* not a power of two, and a heap uses it */
    DMOD_TEST_EXPECT_FALSE(dmpsram_config_valid(c.mode, c.heap_usage, c.heap_alignment));
    c.heap_usage = dmpsram_heap_usage_none;      /* no heap: the alignment does not matter */
    DMOD_TEST_EXPECT_TRUE(dmpsram_config_valid(c.mode, c.heap_usage, c.heap_alignment));
    c.mode = (dmpsram_mode_t)2;
    DMOD_TEST_EXPECT_FALSE(dmpsram_config_valid(c.mode, c.heap_usage, c.heap_alignment));
}
