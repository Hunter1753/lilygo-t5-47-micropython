#!/bin/sh

cd micropython/ports/esp32
idf.py -D MICROPY_BOARD=LILYGO_T5_47 -D USER_C_MODULES=/home/aguttrof/esp32_epaper_module/module/micropython.cmake -B /home/aguttrof/esp32_epaper_module/build build