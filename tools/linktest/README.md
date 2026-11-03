# LINKTEST

Step 1 of tiboyce-link: checks that two TI-84 Plus CE calculators can talk to
each other over USB, and measures how fast. Doesn't touch the emulator.

## What you need

- Two TI-84 Plus CE / CE-T / TI-83 Premium CE calculators that can run
  assembly programs (if TI-Boy CE runs, this will too)
- A USB **mini-A ↔ mini-B** cable (a mini-B ↔ mini-B cable will not work)
- `LINKTEST.8xp` and `clibs.8xg` (CE C libraries, from
  https://github.com/CE-Programming/libraries/releases)

## Install (on both calculators)

1. Send `clibs.8xg` and `LINKTEST.8xp` with TI Connect CE.
2. Run it the same way you start TI-Boy CE (e.g. via arTIfiCE/Cesium, or
   `Asm(prgmLINKTEST` on older OS versions).

## Run

1. Connect the cable and start LINKTEST on **both** calculators.
2. The calc with the **mini-A** plug becomes HOST, the other DEVICE.
3. When the HOST says "Connected", press **ENTER** on the HOST.
4. Wait a few seconds; results appear on the HOST. CLEAR quits.

## Tests

- **Ping**: 500 one-byte round trips → min/avg/max latency in µs.
- **Exchange**: 1000 Game Boy-style byte swaps, both sides verify every byte
  → µs per byte and error count.

A real Game Boy link cable at normal speed moves about one byte per ~1000 µs.

## Build

Needs the [CE C toolchain](https://github.com/CE-Programming/toolchain):

    make
