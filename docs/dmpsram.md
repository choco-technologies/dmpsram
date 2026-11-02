# dmpsram

## What the driver does

`dmpsram_dmdrvi_create()` (called by dmdevfs for the driver configuration) does, in this order:

1. `dmpsram_port_configure()`: brings the chip up and maps it. On the ESP32-S3:
   - the pads of the octal bus (GPIO26 CS1, GPIO27/28 SPIHD/SPIWP, GPIO31/32 SPIQ/SPID: function 0; GPIO33-37 SPIIO4-7 and DQS:
     function 4; the strongest drive), and the drive of the clock pads;
   - no ECC (`SYSCON_SRAM_ACE0_ATTR` bit 8), the timing of the chip select, the clock of the chip (APB 80 MHz / 4 = 20 MHz);
   - the mode registers of the chip over SPI1 (the ROM `esp_rom_opiflash_exec_cmd`, octal DDR): fixed latency, read latency, drive;
     the vendor id (0x0D, AP Memory) and the density tell the size. No answer: `-ENODEV`;
   - the commands, the dummy cycles and the octal DDR mode for the cache (SPI0);
   - the pages of the chip into the MMU entries at the end of the 32 MB window of the cache: 8 MB at `0x3D800000` (data bus),
     the same entries are seen by the instruction bus at `0x43800000`;
   - a test of the cache: patterns written through the data cache, written back, the cache dropped, read back through both buses.
2. `verify` (default on): patterns of 32 and 8 bits at the start, in the middle and at the end.
3. `heap_usage`: `dmheap_init()` of the region, the tags `psram,external,slow`; for `heap` the heap goes to the end of the default
   list (see below), for `tagged` it is taken off the list.
4. The device `/dev/psram`.

## The heap and the order of the default heaps

dmheap puts every new heap on its list of the default heaps and tries **the newest first**. For the PSRAM that is the wrong way
round (it is the slowest memory, and the stacks and the objects of the RTOS should stay in the internal RAM), so the driver
re-orders the list: the PSRAM is the last one tried. What does not fit the internal RAM - the decompression buffer of a big module,
the module itself - lands in it. A module in the PSRAM is executed there.

## Memory map (ESP32-S3)

| | |
|---|---|
| Data bus (read/write) | `0x3D800000` .. `0x3E000000` (8 MB) |
| Instruction bus (execute, 32-bit reads) | `0x43800000` .. `0x44000000` |
| MMU entries | 384 .. 511 (the flash of the bank image uses the lower ones) |

## Limits

See the README: 20 MHz, behind the cache, no atomic instructions on it.
