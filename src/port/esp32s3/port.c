#define DMOD_ENABLE_REGISTRATION    ON
#include "dmpsram_port.h"
#include "dmod.h"

/* ---- DMOD lifecycle ---- */

int dmod_init(const Dmod_Config_t *Config)
{
    Dmod_Printf("dmpsram port module initialized (esp32s3)\n");
    return 0;
}

int dmod_deinit(void)
{
    Dmod_Printf("dmpsram port module deinitialized (esp32s3)\n");
    return 0;
}

/* ---- API implementation ----
 *
 * Implement the dmod_dmpsram_port_api_declaration(...) functions
 * declared in include/dmpsram_port.h here. Register an interrupt
 * handler if needed, e.g.:
 *
 *   DMOD_IRQ_HANDLER(SOME_IRQn)
 *   {
 *       // handle interrupt
 *   }
 */
