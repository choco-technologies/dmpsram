#ifndef ESP32S3_PSRAM_H
#define ESP32S3_PSRAM_H

#include <stdint.h>

/*
 * ESP32-S3, octal PSRAM (ESP32-S3R8 / FN16R8: 8 MB, AP Memory, 1.8 V) behind the cache of the CPU. The sequence is the one of
 * ESP-IDF's esp_psram_impl_octal.c, at the low clock of the MSPI (APB / 4 = 20 MHz), where no timing tuning is needed.
 */

/* ---- MSPI registers: SPI0 (the cache) and SPI1 (the driver of the flash and PSRAM chips) ---- */
#define MSPI_BASE(i)                    (0x60003000UL - (i) * 0x1000UL)
#define MSPI_CLOCK_REG(i)               (MSPI_BASE(i) + 0x14UL)
#define MSPI_CACHE_SCTRL_REG(i)         (MSPI_BASE(i) + 0x40UL)
#define MSPI_SRAM_CMD_REG(i)            (MSPI_BASE(i) + 0x44UL)
#define MSPI_SRAM_DRD_CMD_REG(i)        (MSPI_BASE(i) + 0x48UL)
#define MSPI_SRAM_DWR_CMD_REG(i)        (MSPI_BASE(i) + 0x4CUL)
#define MSPI_SRAM_CLK_REG(i)            (MSPI_BASE(i) + 0x50UL)
#define MSPI_SMEM_AC_REG(i)             (MSPI_BASE(i) + 0xDCUL)
#define MSPI_DDR_REG(i)                 (MSPI_BASE(i) + 0xE0UL)
#define MSPI_SMEM_DDR_REG(i)            (MSPI_BASE(i) + 0xE4UL)
#define MSPI_DATE_REG(i)                (MSPI_BASE(i) + 0x3FCUL)

#define BIT(n)                          (1UL << (n))

/* MSPI_SMEM_AC_REG */
#define SMEM_CS_SETUP                   BIT(0)
#define SMEM_CS_HOLD                    BIT(1)
#define SMEM_CS_SETUP_TIME_S            2U
#define SMEM_CS_HOLD_TIME_S             7U
#define SMEM_CS_HOLD_DELAY_S            25U
/* MSPI_DATE_REG: the drive strength of the clock pads is set by this register, not by the IO MUX */
#define DATE_SMEM_SPICLK_FUN_DRV_S      0U
#define DATE_FMEM_SPICLK_FUN_DRV_S      2U
#define DATE_SPICLK_PAD_DRV_CTL_EN      BIT(4)
/* MSPI_DDR_REG(1) */
#define DDR_FMEM_VAR_DUMMY              BIT(1)
/* MSPI_SMEM_DDR_REG */
#define SMEM_DDR_EN                     BIT(0)
#define SMEM_VAR_DUMMY                  BIT(1)
#define SMEM_DDR_RDAT_SWP               BIT(2)
#define SMEM_DDR_WDAT_SWP               BIT(3)
/* MSPI_CACHE_SCTRL_REG */
#define SCTRL_USR_SCMD_4BYTE            BIT(0)
#define SCTRL_USR_WR_SRAM_DUMMY         BIT(3)
#define SCTRL_USR_RD_SRAM_DUMMY         BIT(4)
#define SCTRL_SRAM_USR_RCMD             BIT(5)
#define SCTRL_SRAM_RDUMMY_CYCLELEN_S    6U
#define SCTRL_SRAM_ADDR_BITLEN_S        14U
#define SCTRL_SRAM_USR_WCMD             BIT(20)
#define SCTRL_SRAM_OCT                  BIT(21)
#define SCTRL_SRAM_WDUMMY_CYCLELEN_S    22U
/* MSPI_SRAM_CMD_REG */
#define SRAM_CMD_SDIN_OCT               BIT(18)
#define SRAM_CMD_SDOUT_OCT              BIT(19)
#define SRAM_CMD_SADDR_OCT              BIT(20)
#define SRAM_CMD_SCMD_OCT               BIT(21)
#define SRAM_CMD_SDUMMY_OUT             BIT(22)
/* MSPI_SRAM_DRD_CMD_REG / MSPI_SRAM_DWR_CMD_REG: [31:28] the length of the command - 1, [15:0] the command */
#define SRAM_CMD_BITLEN_S               28U
/* MSPI_SRAM_CLK_REG */
#define SRAM_CLK_CNT_L_S               0U
#define SRAM_CLK_CNT_H_S               8U
#define SRAM_CLK_CNT_N_S               16U
#define SRAM_CLK_EQU_SYSCLK            BIT(31)

/* ---- SYSCON: the attributes of the address range 0 of the PSRAM (ECC) ---- */
#define SYSCON_SRAM_ACE0_ATTR_REG       0x60026058UL
#define SYSCON_SRAM_ECC_ENABLE          BIT(8)

/* ---- The pad of the chip select of the PSRAM and the IO MUX ---- */
#define PSRAM_CS1_GPIO                  26U
#define IO_MUX_REG(n)                   (0x60009004UL + 4UL * (n))
#define IO_MUX_MCU_SEL_S                12U
#define IO_MUX_MCU_SEL_M                (7UL << IO_MUX_MCU_SEL_S)
#define IO_MUX_FUN_DRV_S                10U
#define IO_MUX_FUN_DRV_M                (3UL << IO_MUX_FUN_DRV_S)

/* ---- eFuse: what the chip is said to have inside (EFUSE_RD_MAC_SPI_SYS_4_REG) ---- */
#define EFUSE_RD_MAC_SPI_SYS_4_REG      0x60007054UL
#define EFUSE_PSRAM_CAP_S               3U      /* 2 bits: 0 none, 1 8 MB, 2 2 MB */
#define EFUSE_PSRAM_VENDOR_S            7U      /* 2 bits: 1 AP Memory */

/* ---- The memory management unit of the cache: 512 entries of 64 KB pages, shared by both buses ---- */
#define MMU_TABLE                       0x600C5000UL
#define MMU_ENTRIES                     512U
#define MMU_PAGE_SIZE                   0x10000UL
#define MMU_INVALID                     BIT(14)
#define MMU_ACCESS_SPIRAM               BIT(15)
#define MMU_DBUS_BASE                   0x3C000000UL    /* the window of the data bus, 32 MB */
#define MMU_IBUS_BASE                   0x42000000UL    /* the same window seen by the instruction bus */
#define MMU_WINDOW_SIZE                 0x02000000UL

/* ---- ROM ---- */
#define ROM_OPIFLASH_EXEC_CMD           0x400008b8UL
#define ROM_SPI_SET_DTR_SWAP_MODE       0x4000093cUL
#define ROM_CACHE_SUSPEND_DCACHE        0x400018b4UL
#define ROM_CACHE_RESUME_DCACHE         0x400018c0UL
#define ROM_CACHE_INVALIDATE_DCACHE_ALL 0x400016e0UL
#define ROM_CACHE_WRITEBACK_ALL         0x400016f8UL
#define ROM_CONFIG_DATA_CACHE_MODE      0x40001a28UL    /* (size, ways, line size) */
#define ROM_SPIFLASH_OPI_DTR_MODE       7

/* The data cache as ESP-IDF sets it up for the octal PSRAM */
#define DCACHE_SIZE_BYTES               (32U * 1024U)
#define DCACHE_WAYS                     8U
#define DCACHE_LINE_BYTES               32U

#endif /* ESP32S3_PSRAM_H */
