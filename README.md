# Micropython build including epdiy

This repository is using the latest version of vrolands https://github.com/vroland/epdiy

Currently only works with the Lilygo T5-47 epaper display based on the ED047TC1 which is a 16 colour epaper display which supports partial refresh.
This repository currently only supports the old version using the ESP32-WROVER-E (ESP32-D0WQ6) with 8MB of PSRAM and 16MB of FLASH.

Other boards that are supported by the epdiy library should be easily addable.

It provides a micropython library to control the epaper display.

This tries to replace the outdated micropython fork by Lilygo https://github.com/Xinyuan-LilyGO/lilygo-micropython

## Prequesites

install the esp-idf 5.5.2 and esptool

## How to use

1. Clone this repo using `git clone --recurse-submodules $url` to fetch all the submodules
2. Run `prepare.sh` which sets up the micropython cross compiler and applies the neccessary patches
3. Run `build.sh [BOARD]` to build the firmware (defaults to `LILYGO_T5_47`)
4. Run `flash.sh` to use `esptool` to flash the firmware to the first connected board

### Supported Board Types

Pass the board name as the first argument to `build.sh`:

| Board argument    | Hardware                        | MCU        | Display   |
|-------------------|---------------------------------|------------|-----------|
| `LILYGO_T5_47`    | LilyGo T5 4.7" EPaper (default) | ESP32      | ED047TC1  |
| `LILYGO_S3`       | LilyGo T5 4.7" EPaper S3        | ESP32-S3   | ED047TC1  |
| `EPDIY_V2_V3`     | epdiy board V2/V3               | ESP32      | ED097TC2  |
| `EPDIY_V4`        | epdiy board V4                  | ESP32      | ED097TC2  |
| `EPDIY_V5`        | epdiy board V5                  | ESP32      | ED097TC2  |
| `EPDIY_V6`        | epdiy board V6                  | ESP32      | ED097TC2  |
| `EPDIY_V7`        | epdiy board V7                  | ESP32-S3   | ED097TC2  |
| `EPDIY_V7_103`    | epdiy board V7 (10.3")          | ESP32-S3   | ED103MC2  |
| `EPDIY_V7_RAW`    | epdiy board V7 Raw              | ESP32-S3   | ED097TC2  |

Examples:

```sh
./build.sh                  # build for LilyGo T5 4.7 (ESP32)
./build.sh LILYGO_S3        # build for LilyGo T5 4.7 S3
./build.sh EPDIY_V7         # build for epdiy V7 (ESP32-S3)
```

When the board type changes between builds, `build.sh` automatically runs `fullclean.sh` and `prepare.sh` to ensure the build directory is clean and patches are correctly applied for the new target.

> **Warning:** `fullclean.sh` resets the `micropython` and `epdiy` submodules with `git reset --hard` and `git clean -dfx`, which **permanently wipes any direct changes** made inside those directories. New board definitions must therefore be added as patch files under `patches/micropython/` rather than by editing the submodules directly. `prepare.sh` re-applies all patches after a clean.
>
> Custom fonts stored in `module/fonts/` are **not** affected by `fullclean.sh` and will be preserved across cleans.

## How to create a release

`git tag v1.0.0 && git push origin v1.0.0`

## Partition Size

The app partition holds the compiled firmware (including any fonts you add with `add_font.py`).
The remaining flash is available as a MicroPython filesystem for `.py` scripts and data files.
On boards with 16 MiB flash (e.g. `LILYGO_T5_47`) you can tune this tradeoff.

Copy `user.sdkconfig.example` to `user.sdkconfig` and uncomment one line:

```sh
cp user.sdkconfig.example user.sdkconfig
# then edit user.sdkconfig and uncomment your preferred layout
```

| Setting in `user.sdkconfig`                                        | App    | Filesystem |
|--------------------------------------------------------------------|--------|------------|
| `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-16MiB-large-app.csv"` | 4 MiB  | ~12 MiB    |
| `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions-16MiB-large-fs.csv"`  | 1.9 MiB | ~14 MiB  |

After changing `user.sdkconfig` run a full clean and rebuild:

```sh
./fullclean.sh && ./build.sh LILYGO_T5_47
```

`user.sdkconfig` is gitignored. Without it the default partition layout is used unchanged.

## Adding Custom Fonts

The firmware ships with **FiraSans** at sizes 12 and 20. Additional TTF/OTF fonts can be compiled in using `add_font.py` (single font) or `convert_fonts.py` (batch). Use `clean_userfonts.sh` to remove all user fonts, or `python add_font.py --regen-registry` to rebuild the font registry from existing headers. See [docs/fonts.md](docs/fonts.md) for full details.

## API Reference

Full constants, instance methods, text measurement utilities, and a complete usage example are in [docs/api.md](docs/api.md).

# License

The code in this repository is licensed under the LGPL-3.0 License - see the LICENSE file for details.

## Third-Party Assets & Submodules:

- MicroPython: MIT License (included as a submodule).

- epdiy: LGPL-3.0 License (included as a submodule).

- Fira Sans Font: SIL Open Font License (OFL) 1.1. The font header files located in the module/fonts/ directory are derivative works of Fira Sans. See module/fonts/OFL.txt for the full license text.

