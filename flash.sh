#!/bin/sh

BOARD=${1:-LILYGO_T5_47}

cd micropython/ports/esp32
idf.py -B ../../../build/$BOARD flash