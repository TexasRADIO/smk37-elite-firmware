# Build BB-37 Elite 826

Use Python 3 and the Linux x86-64 JieLi PI32r3/FPU toolchain. Point the command
at the directory containing `pi32v2/` and `common/`. Its executable hashes are
recorded in `PROVENANCE.json`. The scoped platform source, retained stub and
loader, SDK packaging inputs and required notices are included under
`board/vendor/`.

The exact generated synth phrases, Band presets and percussion directory used
by this application are included.

```sh
python3 board/build.py \
  --toolchain /path/to/jieli-toolchain \
  --out build/elite-826-rebuild \
  --build-number 826 --model elite \
  --synth-presets build/synth-presets \
  --band-presets build/band-presets \
  --dillo --drums --single-808 \
  --percussion-plan build/percussion-727 \
  --render-profile --drum-render-in-ram --dillo-render-in-ram
python3 board/verify.py build/elite-826-rebuild --factory-only
```

Expected package SHA-256:

```text
e1fcf4ca4553dbc89c8a09a78b5b0ce8990fa14671eb724c931374d43baf30c2
```

The original build used a glibc runtime. On an ISO with the Clang resource
directory flattened from `4.0.1` to `401`, point `C_INCLUDE_PATH` at the actual
`pi32v2/lib/clang/401/include` directory. With an ordinary complete toolchain,
preserve its original names and use its normal header lookup.

Component identities, source hashes and original notices are retained in
PROVENANCE.json and the corresponding source directories.

The percussion directory refers to the separately provisioned matching sound
banks. Those bank bytes and attribution are included in `assets/layer727/`.
They must be installed through the product's checked content owner; the app
package alone does not provision that content on a fresh device.
