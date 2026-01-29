# Runtime for native backend of MoonBit

This repository maintains the runtime for the native backend of MoonBit, including:

- type definitions for runtime layout of MoonBit object
- fundamental runtime operations for memory management and builtin data type, such as array
- some native API used by `moonbitlang/core`

## File hierarchy

- `include/moonbit-fundamental.h`: used to avoid including system header in some pipeline
- `include/moonbit.h`: type definitions for runtime layout of MoonBit object and public macro/functions for C FFI stub
- `runtime.c`: implementation of the runtime

## Contribution
This repository is a mirror of the runtime in MoonBit compiler, and is updated regularly.
External contributions and bug reports are welcomed.
Patches will be manually merged to the MoonBit compiler.
