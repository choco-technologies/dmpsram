# dmpsram Documentation

The driver of the external RAM (PSRAM): a DMDRVI driver for dmdevfs. It knows the CHIP (octal AP Memory, quad APS6404 / ESP-PSRAM64H);
the controller they are wired to is [dmqspi](https://github.com/choco-technologies/dmqspi). It has no port: it is built per architecture.

## Contents

- **[dmpsram.md](dmpsram.md)** - What the driver does, the configuration, the memory map
- **[api-reference.md](api-reference.md)** - ioctls

View documentation using `dmf-man`:

```bash
dmf-man dmpsram          # Main documentation
dmf-man dmpsram api      # API reference
```
