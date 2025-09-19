# RuuviAir MCUboot Bootloader

The *RuuviAir MCUboot* is based on [MCUboot](https://github.com/nrfconnect/sdk-mcuboot) as included in the nRF Connect SDK — a secure, second-stage bootloader.  
This repository provides **Ruuvi-specific extensions** to MCUboot using the available extension hooks.

## Key Features

- **File-based firmware updates**  
  Install updates directly from files stored in the LittleFS partition on external flash memory.

- **Self-update capability**  
  Supports updating both the primary and secondary MCUboot partitions.

- **Enhanced security**  
  Verifies image signatures before overwriting partitions, ensuring firmware authenticity.

- **Boot mode switching**  
  Switch between the primary and secondary partitions (e.g., main application vs. firmware loader)  
  via button press or by command from the main application.

- **Full compatibility**  
  Preserves MCUboot’s secure boot flow while extending its functionality for RuuviAir devices.
