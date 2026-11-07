# dmpsram

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

The driver of the external RAM (PSRAM) for [DMOD](https://github.com/choco-technologies/dmod): a driver for `dmdevfs` (DMDRVI) that
brings the chip up, maps it into the address space of the CPU and, when asked, hands it to [dmheap](https://github.com/choco-technologies/dmheap)
as an additional heap. The driver knows the CHIP; the controller it is wired to (the pads, the clock, the window in the address space) is
[dmqspi](https://github.com/choco-technologies/dmqspi), whose port for the ESP32-S3 drives the memory SPI.

## Features

- **Octal PSRAM** (ESP32-S3R8 / FN16R8: 8 MB, AP Memory; 8 lines, DDR): the mode registers of the chip. **Quad PSRAM** (APS6404L,
  ESP-PSRAM64H: 8 MB, e.g. on the LILYGO T-Deck Pro): ID, reset, the QPI mode. Both mapped at `0x3D800000` (data bus) by the controller
  (the instruction bus sees them at `0x43800000`: the loader of dmod runs modules from there).
- **dmheap integration**: `heap_usage=heap` registers the memory with dmheap, which tries the newest heap first: the PSRAM is
  used first and the (faster, scarcer) internal RAM is what is left; `heap_usage=tagged` keeps it off the default list (only
  `Dmod_MallocTagged("psram", ...)` gets memory from it).
- **Modules run from it**: the loader of dmod writes a module there and executes it through the instruction bus (dmod
  `Dmod_SyncCode` writes the data cache back and invalidates the instruction cache).
- **Checks itself** before it is used: patterns through the data cache, pushed out to the chip, read back through both buses.
- **A device**: `/dev/psram` (`read`/`write` at an offset, `ioctl`s for the address, the size, the clock, the heap).

## Configuration

```ini
[main]
driver_order=-5

[dmpsram]
qspi_instance=1         ; the controller (dmqspi) the chip is wired to
mode=octal              ; octal | quad
heap_usage=heap         ; none | heap | tagged
heap_alignment=16
verify=on
```

The ini file of a board holds the section of the controller too (`dmqspi`, whose clock is the clock of the chip). `configs/board/esp32s3-lcd28.ini`
is the one for the ESP32-S3 2.8" display board (octal), `configs/board/t-deck-pro.ini` for the LILYGO T-Deck Pro (quad). Install it with lines
in `flash.dmd` (the controller is a module of its own):

```
dmqspi
dmpsram driver=board/t-deck-pro.ini
```

The PSRAM takes its clock from the APB, so [dmclk](https://github.com/choco-technologies/dmclk) has to be started before.

## Building

The code does not depend on the CPU, so there is no port: the module is built per architecture.

```bash
cmake -S . -B build && cmake --build build && ctest --test-dir build                       # the host: the tests
cmake -S . -B build-esp -DDMOD_TOOLS_NAME=arch/xtensa/esp32s3 && cmake --build build-esp   # an architecture
```

## Documentation

See [docs/](docs/README.md), or `dmf-man dmpsram`.

## Limits

- 20 MHz for the octal chip, up to 40 MHz for the quad one: the faster clocks need the timing tuning of the MSPI (dmqspi refuses them).
- The memory is behind the cache of the CPU: DMA cannot use it, and it must not be touched with the cache disabled (the
  flash operations of the firmware).
- `s32c1i` (the atomic compare-and-set of the Xtensa) does not work on it (ESP-IDF works around it in software): do not use
  atomic operations on objects that live in the PSRAM.
- Only the first 8 MB are mapped (the mapping window is 8 MB at the end of the 32 MB window of the cache).
