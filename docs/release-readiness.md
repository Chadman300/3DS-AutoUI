# Release readiness for 3DS AutoUI

## Current status

The Python prototype is working and the app is demonstrably running in demo mode:

- `py -3 app.py --demo` succeeds
- The JSON payload and Subaru WRX dashboard render correctly

The actual 3DS homebrew build is not yet ready in this environment because the 3DS toolchain is missing.

## Required build environment

A real release/test build requires a machine with:

- devkitPro / devkitARM
- libctru
- citro2d
- `make`
- a working 3DS homebrew toolchain

## Blocker observed in this environment

The build failed because the machine does not have the required toolchain installed, and the official devkitPro repository could not be reached from this session. This is an environment/network issue, not a project logic issue.

## Release gate before shipping

1. Install devkitPro on a dedicated Windows or Linux 3DS dev machine.
2. Ensure `DEVKITPRO` and `DEVKITARM` are exported in the environment.
3. Build the homebrew project with `make -C 3ds_app`.
4. Verify the binary boots on a 3DS console or emulator.
5. Test wiring to the OBD2 bridge and actual car sensor values.
6. Validate warning/critical thresholds on real Subaru data.
7. Package a stable release candidate.

## Recommended next machine setup

Use a devkitPro-supported setup such as:

- Windows + MSYS2 + devkitPro
- Linux + devkitPro

Then run:

```bash
make -C 3ds_app
```

and confirm the generated homebrew artifact is usable on a target 3DS device.
