#define DMOD_ENABLE_REGISTRATION    ON
#include "dmod.h"
#include "dmpsram_port.h"
#include "esp32s3_psram.h"
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * The PSRAM of the ESP32-S3 is reached through the cache: the MMU maps 64 KB pages of it into a window of the address space
 * (the data bus at 0x3C000000, the instruction bus at 0x42000000 - the same page entries serve both). Flash lives in the lower
 * entries (the firmware, the bank image), so the PSRAM takes the entries at the end of the window.
 *
 * Everything between the "critical" marks runs with the interrupts masked and calls nothing but the ROM and this file: the
 * firmware itself is executed from the flash through the same cache that is reconfigured.
 */

#define DEFAULT_FREQUENCY_HZ    20000000U
#define APB_FREQUENCY_HZ        80000000U
#define MAX_MAPPED_PAGES        128U            /* 8 MB */

#define RD_DUMMY_BITLEN         (2 * (10 - 1))
#define WR_DUMMY_BITLEN         (2 * (5 - 1))
#define MR_READ                 0x4040U
#define MR_WRITE                0xC0C0U
#define SYNC_READ               0x0000U
#define SYNC_WRITE              0x8080U
#define VENDOR_ID_AP            0xD

static const struct { uint8_t gpio; uint8_t func; } s_bus_pads[] =
{
    { 26, 0 },  /* SPICS1: chip select of the PSRAM */
    { 27, 0 },  /* SPIHD */
    { 28, 0 },  /* SPIWP */
    { 31, 0 },  /* SPIQ */
    { 32, 0 },  /* SPID */
    { 33, 4 }, { 34, 4 }, { 35, 4 }, { 36, 4 },     /* SPIIO4-7 */
    { 37, 4 },  /* SPIDQS */
};

static bool     s_configured = false;
static uint32_t s_first_entry = 0;
static void unmap(uint32_t first_entry, uint32_t entries);
static uint32_t s_entries = 0;

typedef void (*opiflash_exec_cmd_t)(int spi_num, int mode, uint32_t cmd, int cmd_bit_len, uint32_t addr, int addr_bit_len,
                                    int dummy_bits, uint8_t *mosi_data, int mosi_bit_len, uint8_t *miso_data,
                                    int miso_bit_len, uint32_t cs_mask, bool is_write_erase_operation);

static inline volatile uint32_t *reg(uint32_t address)
{
    return (volatile uint32_t *)address;
}

static inline void reg_set_bits(uint32_t address, uint32_t mask)
{
    *reg(address) |= mask;
}

static inline void reg_clear_bits(uint32_t address, uint32_t mask)
{
    *reg(address) &= ~mask;
}

static inline void reg_set_field(uint32_t address, uint32_t mask, uint32_t shift, uint32_t value)
{
    *reg(address) = (*reg(address) & ~(mask << shift)) | ((value & mask) << shift);
}

/** The register of the chip, read into 'out' (16 bits at 'address': the register and the next one). */
static void mr_read(uint32_t address, uint8_t *out, int bits)
{
    ((opiflash_exec_cmd_t)ROM_OPIFLASH_EXEC_CMD)(1, ROM_SPIFLASH_OPI_DTR_MODE, MR_READ, 16, address, 32, RD_DUMMY_BITLEN,
                                                 NULL, 0, out, bits, BIT(1), false);
}

static void mr_write(uint32_t address, uint8_t *data)
{
    ((opiflash_exec_cmd_t)ROM_OPIFLASH_EXEC_CMD)(1, ROM_SPIFLASH_OPI_DTR_MODE, MR_WRITE, 16, address, 32, 0,
                                                 data, 16, NULL, 0, BIT(1), false);
}

static void set_psram_clock(int spi_num, uint32_t div)
{
    if (div == 1U)
        *reg(MSPI_SRAM_CLK_REG(spi_num)) = SRAM_CLK_EQU_SYSCLK;
    else
        *reg(MSPI_SRAM_CLK_REG(spi_num)) = ((div - 1U) << SRAM_CLK_CNT_N_S) | ((div / 2U - 1U) << SRAM_CLK_CNT_H_S) | ((div - 1U) << SRAM_CLK_CNT_L_S);
}

/** What the cache needs to know to read and write the chip: the commands, the dummy cycles, the octal DDR mode. */
static void config_cache_phases(void)
{
    reg_set_bits(MSPI_CACHE_SCTRL_REG(0), SCTRL_SRAM_USR_WCMD);
    reg_set_field(MSPI_SRAM_DWR_CMD_REG(0), 0xFU, SRAM_CMD_BITLEN_S, 16 - 1);
    reg_set_field(MSPI_SRAM_DWR_CMD_REG(0), 0xFFFFU, 0U, SYNC_WRITE);

    reg_set_bits(MSPI_CACHE_SCTRL_REG(0), SCTRL_SRAM_USR_RCMD);
    reg_set_field(MSPI_SRAM_DRD_CMD_REG(0), 0xFU, SRAM_CMD_BITLEN_S, 16 - 1);
    reg_set_field(MSPI_SRAM_DRD_CMD_REG(0), 0xFFFFU, 0U, SYNC_READ);

    reg_set_field(MSPI_CACHE_SCTRL_REG(0), 0x3FU, SCTRL_SRAM_ADDR_BITLEN_S, 32 - 1);
    reg_set_bits(MSPI_CACHE_SCTRL_REG(0), SCTRL_USR_SCMD_4BYTE);

    reg_set_bits(MSPI_CACHE_SCTRL_REG(0), SCTRL_USR_RD_SRAM_DUMMY | SCTRL_USR_WR_SRAM_DUMMY);
    reg_set_field(MSPI_CACHE_SCTRL_REG(0), 0x3FU, SCTRL_SRAM_RDUMMY_CYCLELEN_S, RD_DUMMY_BITLEN - 1);
    reg_set_bits(MSPI_SMEM_DDR_REG(0), SMEM_VAR_DUMMY);
    reg_set_field(MSPI_CACHE_SCTRL_REG(0), 0x3FU, SCTRL_SRAM_WDUMMY_CYCLELEN_S, WR_DUMMY_BITLEN - 1);

    reg_clear_bits(MSPI_SMEM_DDR_REG(0), SMEM_DDR_WDAT_SWP | SMEM_DDR_RDAT_SWP);
    reg_set_bits(MSPI_SMEM_DDR_REG(0), SMEM_DDR_EN);

    reg_set_bits(MSPI_SRAM_CMD_REG(0), SRAM_CMD_SDUMMY_OUT | SRAM_CMD_SCMD_OCT | SRAM_CMD_SADDR_OCT | SRAM_CMD_SDOUT_OCT | SRAM_CMD_SDIN_OCT);
    reg_set_bits(MSPI_CACHE_SCTRL_REG(0), SCTRL_SRAM_OCT);
}

/**
 * The part of the bring-up that must not be interrupted. Returns the density field of the chip (MR2) or a negative errno.
 * Marked noinline and kept free of calls into the firmware.
 */
static int __attribute__((noinline)) bring_up_critical(uint32_t div, uint8_t *mr_out)
{
    /*
     * The pads of the octal bus. They are the pads of the MSPI (what esp_rom_opiflash_pin_config() and ESP-IDF's
     * mspi_timing_set_pin_drive_strength() set up): the chip select CS1 (GPIO26) and the clock pad take function 0, as do
     * SPIHD/SPIWP (GPIO27/28, the data lines D3/D2 - the reset state is GPIO) and SPIQ/SPID (GPIO31/32, D1/D0); the other
     * four data lines and DQS (GPIO33-37) are the function 4. All with the strongest drive.
     */
    for (uint32_t i = 0; i < sizeof(s_bus_pads) / sizeof(s_bus_pads[0]); i++)
    {
        reg_set_field(IO_MUX_REG(s_bus_pads[i].gpio), 7U, IO_MUX_MCU_SEL_S, s_bus_pads[i].func);
        reg_set_field(IO_MUX_REG(s_bus_pads[i].gpio), 3U, IO_MUX_FUN_DRV_S, 3U);
    }
    reg_set_bits(MSPI_DATE_REG(0), DATE_SPICLK_PAD_DRV_CTL_EN);
    reg_set_field(MSPI_DATE_REG(0), 3U, DATE_FMEM_SPICLK_FUN_DRV_S, 3U);
    reg_set_field(MSPI_DATE_REG(0), 3U, DATE_SMEM_SPICLK_FUN_DRV_S, 3U);

    /* no ECC on the PSRAM (the ACE0 attribute of the SYSCON: bit 8 turns it on; the reset value has it on) */
    reg_clear_bits(SYSCON_SRAM_ACE0_ATTR_REG, SYSCON_SRAM_ECC_ENABLE);

    /* the timing of the chip select */
    reg_set_bits(MSPI_SMEM_AC_REG(0), SMEM_CS_HOLD | SMEM_CS_SETUP);
    reg_set_field(MSPI_SMEM_AC_REG(0), 0x1FU, SMEM_CS_HOLD_TIME_S, 3U);
    reg_set_field(MSPI_SMEM_AC_REG(0), 0x1FU, SMEM_CS_SETUP_TIME_S, 3U);
    reg_set_field(MSPI_SMEM_AC_REG(0), 0x3FU, SMEM_CS_HOLD_DELAY_S, 2U);

    /* the clock of the chip: the same for the cache (SPI0) and for the driver (SPI1) */
    set_psram_clock(0, div);        /* SPI1 follows: the two share the register */

    reg_set_bits(MSPI_DDR_REG(1), DDR_FMEM_VAR_DUMMY);
    ((void (*)(int, bool, bool))ROM_SPI_SET_DTR_SWAP_MODE)(1, false, false);

    /* read latency, fixed latency, drive strength: MR0 (the next byte of the 16 bits is MR1, read-only) */
    uint8_t mr[8] __attribute__((aligned(4))) = {0};     /* MR0, MR1, MR2, MR3, MR4, MR8 (the ROM stores words) */
    mr_read(0x0, &mr[0], 16);
    mr[0] = (uint8_t)((mr[0] & ~0x3FU) | (0U /* drive strength */) | (2U << 2 /* read latency */) | (1U << 5 /* fixed latency */));
    mr_write(0x0, &mr[0]);

    mr_read(0x0, &mr[0], 16);
    mr_read(0x2, &mr[2], 16);
    mr_read(0x4, &mr[4], 8);
    mr_read(0x8, &mr[5], 8);
    for (int i = 0; i < 6; i++) mr_out[i] = mr[i];

    if ((mr[1] & 0x1FU) != VENDOR_ID_AP)
        return -ENODEV;
    return mr[2] & 0x7U;
}

static uint32_t density_to_bytes(uint32_t density)
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

/** The mapping and the cache, once the chip answers. In its own function for the same reason as the bring-up. */
static void __attribute__((noinline)) map_critical(uint32_t first_entry, uint32_t entries)
{
    uint32_t autoload = ((uint32_t (*)(void))ROM_CACHE_SUSPEND_DCACHE)();

    /* The data cache mode (size, ways, line) must match what the PSRAM serves: the ROM leaves the lines at 16 bytes, ESP-IDF
       sets 32 KB, 8 ways, 32-byte lines for the octal PSRAM (CONFIG_ESP32S3_DATA_CACHE_*). With the 16-byte lines the data
       that comes back from the chip is wrong from time to time (a NULL where a pointer was stored). */
    ((void (*)(uint32_t, uint32_t, uint32_t))ROM_CONFIG_DATA_CACHE_MODE)(DCACHE_SIZE_BYTES, DCACHE_WAYS, DCACHE_LINE_BYTES);

    config_cache_phases();

    for (uint32_t i = 0; i < entries; i++)
        *reg(MMU_TABLE + 4U * (first_entry + i)) = MMU_ACCESS_SPIRAM | i;       /* valid, the page i of the PSRAM */

    ((void (*)(void))ROM_CACHE_INVALIDATE_DCACHE_ALL)();
    ((void (*)(uint32_t))ROM_CACHE_RESUME_DCACHE)(autoload);
}

/**
 * Writes patterns through the data cache, pushes them out to the chip, throws the cache away and reads them back (through the
 * data bus and through the instruction bus). Without the write-back and the invalidate the cache alone could serve the reads.
 * Words only: the instruction bus does not read bytes. Own code and the ROM, like the sequences above.
 */
static int __attribute__((noinline)) cache_selftest_critical(volatile uint32_t *data, volatile uint32_t *code, uint32_t words)
{
    static const uint32_t offsets[3] = { 0U, 1U, 2U };      /* start, the middle, the end - scaled below */
    uint32_t at[3] = { offsets[0], words / 2U, words - 32U };
    int ok = 1;

    for (int p = 0; p < 3; p++)
        for (uint32_t i = 0; i < 32U; i++)
            data[at[p] + i] = 0xC3A5F00DU ^ (at[p] * 0x9E3779B1U) ^ (i * 0x01010101U);

    ((void (*)(void))ROM_CACHE_WRITEBACK_ALL)();
    ((void (*)(void))ROM_CACHE_INVALIDATE_DCACHE_ALL)();

    for (int p = 0; p < 3; p++)
        for (uint32_t i = 0; i < 32U; i++)
        {
            uint32_t expected = 0xC3A5F00DU ^ (at[p] * 0x9E3779B1U) ^ (i * 0x01010101U);
            if (data[at[p] + i] != expected) ok = 0;
            if (code[at[p] + i] != expected) ok = 0;
        }
    return ok ? 0 : -EIO;
}

dmod_dmpsram_port_api_declaration(1.0, int, _configure, ( const dmpsram_port_config_t *config, dmpsram_port_result_t *out_result ))
{
    if (config == NULL || out_result == NULL) return -EINVAL;
    if (s_configured) return -EBUSY;

    uint32_t frequency = config->frequency_hz != 0U ? config->frequency_hz : DEFAULT_FREQUENCY_HZ;
    /* Only the low clock (no timing tuning of the MSPI needed) for now */
    if (frequency != DEFAULT_FREQUENCY_HZ)
        return -ENOTSUP;
    uint32_t div = APB_FREQUENCY_HZ / frequency;

    uint32_t efuse = *reg(EFUSE_RD_MAC_SPI_SYS_4_REG);
    if (((efuse >> EFUSE_PSRAM_CAP_S) & 0x3U) == 0U)
        return -ENODEV;     /* no PSRAM in this package */

    uint8_t mr[8] __attribute__((aligned(4)));
    Dmod_EnterCritical();
    int density = bring_up_critical(div, mr);
    Dmod_ExitCritical();
    if (density < 0)
    {
        DMOD_LOG_ERROR("PSRAM: no answer of the chip (vendor id 0x%02X, expected 0x%02X)\n", (unsigned)(mr[1] & 0x1FU), (unsigned)VENDOR_ID_AP);
        return density;
    }

    uint32_t chip_size = density_to_bytes((uint32_t)density);
    if (chip_size == 0U)
    {
        DMOD_LOG_ERROR("PSRAM: unknown density 0x%02X\n", (unsigned)density);
        return -ENODEV;
    }
    DMOD_LOG_INFO("PSRAM: vendor 0x%02X, density 0x%02X (%u MB), MR0 0x%02X, MR3 0x%02X\n", (unsigned)(mr[1] & 0x1FU),
        (unsigned)density, (unsigned)(chip_size >> 20), (unsigned)mr[0], (unsigned)mr[3]);

    uint32_t pages = chip_size / MMU_PAGE_SIZE;
    if (pages > MAX_MAPPED_PAGES) pages = MAX_MAPPED_PAGES;
    s_first_entry = MMU_ENTRIES - pages;        /* the end of the window */
    s_entries = pages;

    Dmod_EnterCritical();
    map_critical(s_first_entry, s_entries);
    Dmod_ExitCritical();

    volatile uint32_t *data = (volatile uint32_t *)(MMU_DBUS_BASE + s_first_entry * MMU_PAGE_SIZE);
    volatile uint32_t *code = (volatile uint32_t *)(MMU_IBUS_BASE + s_first_entry * MMU_PAGE_SIZE);
    Dmod_EnterCritical();
    int selftest = cache_selftest_critical(data, code, pages * MMU_PAGE_SIZE / sizeof(uint32_t));
    Dmod_ExitCritical();
    if (selftest != 0)
    {
        DMOD_LOG_ERROR("PSRAM: the memory does not hold what was written through the cache\n");
        unmap(s_first_entry, s_entries);
        return selftest;
    }

    out_result->memory_start      = (void *)(MMU_DBUS_BASE + s_first_entry * MMU_PAGE_SIZE);
    out_result->memory_size_bytes = pages * MMU_PAGE_SIZE;
    out_result->chip_size_bytes   = chip_size;
    out_result->frequency_hz      = frequency;
    out_result->vendor_id         = mr[1] & 0x1FU;
    out_result->code_start        = (void *)(MMU_IBUS_BASE + s_first_entry * MMU_PAGE_SIZE);
    s_configured = true;
    return 0;
}

/** The MMU entries invalid and the cache without the lines of them. */
static void unmap(uint32_t first_entry, uint32_t entries)
{
    Dmod_EnterCritical();
    for (uint32_t i = 0; i < entries; i++)
        *reg(MMU_TABLE + 4U * (first_entry + i)) = MMU_INVALID;
    ((void (*)(void))ROM_CACHE_INVALIDATE_DCACHE_ALL)();
    Dmod_ExitCritical();
}

dmod_dmpsram_port_api_declaration(1.0, int, _unconfigure, ( void ))
{
    if (!s_configured) return 0;

    unmap(s_first_entry, s_entries);
    s_configured = false;
    return 0;
}

int dmod_init(const Dmod_Config_t *Config)
{
    DMOD_LOG_INFO("DMPSRAM port module initialized (ESP32-S3)\n");
    return 0;
}

int dmod_deinit(void)
{
    DMOD_LOG_INFO("DMPSRAM port module deinitialized (ESP32-S3)\n");
    return 0;
}
