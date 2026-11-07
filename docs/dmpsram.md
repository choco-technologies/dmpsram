# dmpsram

## Who does what

| | |
|---|---|
| **dmqspi** (and its port) | The controller: the pads, the clock, the commands on the bus (`DMQSPI_IOCTL_TRANSFER`), the window in the address space of the CPU (the cache and the MMU on the ESP32-S3: `DMQSPI_IOCTL_ENTER_MAPPED_RW`) |
| **dmpsram** | The chip: how to wake it up, how big it is, which commands read and write it; the self-test, the heap, `/dev/psram` |

The driver sends its commands with the function `dmqspi_ioctl(instance, ...)`, not through the node of the controller: the memory has to
be a heap before the drivers that follow it are created, and the file system of the devices is not there yet (a friend is told about at
mount time). The module requires dmqspi, so dmdevfs configures the controller first.

## Configuration

Two sections of the ini file of the board (`configs/board/`): the controller and the chip.

```ini
[psram_qspi]                    ; dmqspi: the controller (its keys: see the documentation of dmqspi)
driver_name=dmqspi
driver_order=-6
instance=1
bank=2                          ; the chip select 1 (0 is the flash of the ESP32-S3)
data_lines=4                    ; 4 (quad) or 8 (octal)
clock_hz=20000000
flash_size=8388608              ; the size of the chip: the window of the memory

[dmpsram]
driver_name=dmpsram
driver_order=-5
qspi_instance=1                 ; the `instance` of the controller section
mode=quad                       ; quad | octal (default: octal)
heap_usage=heap                 ; none | heap | tagged
heap_alignment=16
verify=on
```

`data_lines` of the controller and `mode` of the chip have to agree (4 - quad, 8 - octal).

## What the driver does

`dmpsram_dmdrvi_create()` (called by dmdevfs for the driver configuration) does, in this order:

1. Wakes the chip up with commands to the controller.
   - **quad** (APS6404L, ESP-PSRAM64H: 64 Mbit): leave the QPI mode (`0xF5`), READ ID (`0x9F`, SPI mode: the KGD `0x5D` and the density in
     the EID say it is a PSRAM and how big), reset (`0x66`, `0x99`), enter the QPI mode (`0x35`).
   - **octal** (AP Memory, 1.8 V; the OPI DTR mode, 8 lines, DDR): the mode registers - the latency and the fixed latency in MR0, then
     the vendor (MR1: `0x0D`) and the density (MR2). No answer: `-ENODEV`.
2. Asks the controller to map the chip (`DMQSPI_IOCTL_ENTER_MAPPED_RW`) with the commands it should send for a read (quad: `0xEB`, 6
   dummy cycles; octal: `0x00`, 10) and for a write (quad: `0x38`; octal: `0x80`, 5).
3. `verify` (default on): patterns written through the data cache, written back (`DMQSPI_IOCTL_INVALIDATE_MAPPED`), the cache dropped,
   read back through the data bus and through the instruction bus; 8-bit accesses.
4. `heap_usage`: `dmheap_init()` of the region, the tags `psram,external,slow`; `heap` leaves it on the default list (see below),
   `tagged` takes it off.
5. The device `/dev/psram`.

## The heap

`dmheap_init()` puts the new heap on the list of the default heaps, and the allocations that name no heap try **the newest first**.
With `heap_usage=heap` that is how it stays: once dmpsram is up, what is allocated goes to the PSRAM, and the internal RAM is
kept for what comes before it and for the callers that ask for it by tag (`main`). This includes the stacks and the control
blocks of the threads, the objects of the RTOS and the modules, which are loaded into it and executed there. `heap_usage=tagged`
takes the heap off the list.

## Memory map (ESP32-S3)

| | |
|---|---|
| Data bus (read/write) | `0x3D800000` .. `0x3E000000` (8 MB) |
| Instruction bus (execute, 32-bit reads) | `0x43800000` .. `0x44000000` |
| MMU entries | 384 .. 511 (the flash of the bank image uses the lower ones) |

## Boards

| Board | Chip | File |
|---|---|---|
| Waveshare ESP32-S3-Touch-LCD-2.8 (ESP32-S3R8 / FN16R8) | 8 MB octal | `configs/board/esp32s3-lcd28.ini` |
| LILYGO T-Deck Pro (ESP32-S3 + external chip) | 8 MB quad | `configs/board/t-deck-pro.ini` |

In `flash.dmd` of the board (the controller is a module of its own, it has to be listed too):

```text
dmqspi
dmpsram driver=board/t-deck-pro.ini
```

## Limits

See the README: 20 MHz, behind the cache, no atomic instructions on it.
