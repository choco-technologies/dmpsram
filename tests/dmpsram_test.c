#define DMOD_ENABLE_REGISTRATION ON
#include "dmod_test.h"
#include "dmpsram.h"

static dmpsram_t g_handle = NULL;

void dmod_test_setup(void)
{
    g_handle = dmpsram_create();
}

void dmod_test_teardown(void)
{
    dmpsram_destroy(g_handle);
    g_handle = NULL;
}

DMOD_TEST_STEP(dmpsram_create)
{
    DMOD_TEST_EXPECT_NOT_NULL(g_handle);
}

DMOD_TEST_STEP(dmpsram_is_valid)
{
    DMOD_TEST_EXPECT_TRUE(dmpsram_is_valid(g_handle));
}

DMOD_TEST_STEP(dmpsram_destroy_null)
{
    /* Destroying NULL must not crash. */
    dmpsram_destroy(NULL);
}
