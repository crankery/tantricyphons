# Lama firmware

The complete **Lamarama hardware and firmware overview** is in the repository-root [README](../../README.md).

It covers the main/control boards, analog and digital signal paths, GPIO map, two-core architecture, PIO/DMA operation, source-file responsibilities, menus, calibration, persistence, console, debugging, and hardware bring-up.

## Working in this directory

This is the current Pico 2 C firmware project. C sources are grouped by responsibility under `src/`, matching headers under `include/`; PIO programs live in `pio/`. Run builds from here:

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2 -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

The Pico VS Code extension can supply the installed SDK, compiler and build-tool paths. The primary build uses 44.1 kHz and 256-sample buffers. USB console and probe UART diagnostics are enabled by default.

- Upload `build/lama.uf2` through BOOTSEL, or use picotool/Run Project once USB-enabled firmware is running.
- Open the Pico USB serial port and type `help`; `note 69`, `resetstats`, and `status` are useful for timing checks.
- Use the matching `build/lama.elf` with the SWD probe for source debugging.

## Documentation

- [Module overview and hardware](../../README.md#hardware-organization)
- [GPIO and physical pin assignments](../../README.md#signals-and-pin-assignments)
- [Firmware architecture](../../README.md#firmware-architecture)
- [Source-code map](../../README.md#source-code-map)
- [Console commands](../../README.md#usb-console)
- [Calibration and saved settings](../../README.md#calibration-and-saved-settings)
- [Host tests and board bring-up](../../README.md#tests-and-bring-up)
- [Detailed implementation notes](notes.md)
- [Packed asset format and provenance](assets/README.md)

The root README is the primary overview; `notes.md` keeps the more detailed implementation and diagnostic explanations.
