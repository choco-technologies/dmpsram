# dmpsram API Reference

## Device `/dev/psram`

| Call | |
|------|---|
| `read(offset)`, `write(offset)` | Access to the memory as a file; reads/writes past the end return 0 |
| `stat` | `size` is the size of the mapped memory |

## ioctl (`dmpsram.h`, from `DMDRVI_IOCTL_CUSTOM_BASE`)

| Command | arg | |
|---------|-----|---|
| `dmpsram_ioctl_cmd_get_memory_start` | `void **` | The mapped memory (data bus) |
| `dmpsram_ioctl_cmd_get_memory_size` | `uint32_t *` | Its size in bytes |
| `dmpsram_ioctl_cmd_get_frequency` | `uint32_t *` | Clock of the PSRAM in Hz |
| `dmpsram_ioctl_cmd_get_heap_context` | `dmheap_context_t **` | The heap, or NULL |
| `dmpsram_ioctl_cmd_get_code_start` | `void **` | The memory as the instruction bus sees it, or NULL |

Other commands (the generic ones that dmdevfs sends to every node) return `-ENOTTY`.

## Port API (`dmpsram_port.h`)

| Function | |
|----------|---|
| `int dmpsram_port_configure(const dmpsram_port_config_t *config, dmpsram_port_result_t *out_result)` | Brings the chip up and maps it. `-ENODEV` (no PSRAM or no answer), `-EBUSY` (already), `-ENOTSUP` (the clock), `-EIO` (the test of the cache) |
| `int dmpsram_port_unconfigure(void)` | Unmaps the memory |
