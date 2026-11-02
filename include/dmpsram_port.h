#ifndef DMPSRAM_PORT_H
#define DMPSRAM_PORT_H

#include "dmod_types.h"
#include "dmpsram_port_defs.h"
#include "dmpsram_types.h"

/**
 * Brings the PSRAM up and maps it into the address space of the CPU: pins, the controller, the registers of the chip, the MMU.
 * Returns 0, -ENODEV when the chip has no PSRAM (or it does not answer), -EBUSY when it is configured already, -ENOTSUP for
 * a clock the port cannot run it at.
 */
dmod_dmpsram_port_api(1.0, int, _configure, ( const dmpsram_port_config_t *config, dmpsram_port_result_t *out_result ));

/** Unmaps the memory. The caller must not use it afterwards. */
dmod_dmpsram_port_api(1.0, int, _unconfigure, ( void ));

#endif // DMPSRAM_PORT_H
