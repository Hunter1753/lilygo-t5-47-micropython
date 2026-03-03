#!/bin/sh

cd micropython/ports/esp32
python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset write_flash --flash_mode dio --flash_size 16MB --flash_freq 80m 0x1000 ../../../build/bootloader/bootloader.bin 0x8000 ../../../build/partition_table/partition-table.bin 0x10000 ../../../build/micropython.bin