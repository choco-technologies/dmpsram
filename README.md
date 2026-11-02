# dmpsram

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

The driver of the external RAM (PSRAM) for [DMOD](https://github.com/choco-technologies/dmod): a driver for `dmdevfs` (DMDRVI) that
brings the chip up, maps it into the address space of the CPU and, when asked, hands it to [dmheap](https://github.com/choco-technologies/dmheap)
as an additional heap.

## Features

- **Octal PSRAM of the ESP32-S3** (ESP32-S3R8 / FN16R8: 8 MB, AP Memory): pins, MSPI controller, mode registers of the chip,
  the MMU of the cache. Mapped at `0x3D800000` (data bus), the instruction bus sees it at `0x43800000`.
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
frequency=20000000      ; Hz of the PSRAM clock, only 20 MHz for now
heap_usage=heap         ; none | heap | tagged
heap_alignment=16
verify=on
```

`configs/board/esp32s3-lcd28.ini` is the one for the ESP32-S3 2.8" display board. Install it with a line in `flash.dmd`:

```
dmpsram driver=board/esp32s3-lcd28.ini
```

The PSRAM takes its clock from the APB, so [dmclk](https://github.com/choco-technologies/dmclk) has to be started before.

## Building

```bash
cmake -DDMOD_CPU_FAMILY=esp32s3 -B build      # needs the xtensa-esp32s3-elf toolchain
cmake --build build
```

## Documentation

See [docs/](docs/README.md), or `dmf-man dmpsram`.

## Limits

- 20 MHz only: the faster clocks (40, 80 MHz) need the timing tuning of the MSPI.
- The memory is behind the cache of the CPU: DMA cannot use it, and it must not be touched with the cache disabled (the
  flash operations of the firmware).
- `s32c1i` (the atomic compare-and-set of the Xtensa) does not work on it (ESP-IDF works around it in software): do not use
  atomic operations on objects that live in the PSRAM.
- Only the first 8 MB are mapped (the mapping window is 8 MB at the end of the 32 MB window of the cache).
