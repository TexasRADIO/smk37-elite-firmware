# Building BB-37

BB-37 Build 800 was compiled with the Jieli PI32 toolchain identified in `board/BOARD-PINS.json`. The toolchain itself is not redistributed here.

Set `JIELI_TOOLCHAIN` to the unpacked toolchain directory and run:

```sh
make firmware MODEL=elite
```

On macOS the Makefile runs the Linux-only toolchain in the project's
`fm1-timing-runtime:bookworm-python` `linux/amd64` Docker image. On Linux
x86-64 it runs the toolchain directly.

Use `MODEL=pro` or `MODEL=mke` for the other packages. The build consumes the checked-in generated phrase data in `release-data/`; no private bank or recovery material is required.

The resulting package is written beneath `build/public-800-<model>/`. Run `make verify-release-tree` to check that the public tree contains no internal test, recovery or evidence paths.
