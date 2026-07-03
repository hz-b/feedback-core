# Feedback common support device modules

| File | Purpose |
| ---- | ------- |
| `devFeedback.c` | Common parser  |
| `devFeedback.h` | Shared constants, enum values, etc. |
| `devLiFeedback.c` | `longin` support |
| `devLoFeedback.c` | `longout` support  |
| `devMbbioFeedback.c` | `mbbi`/`mbbo` support with dynamic enum strings. |
| `devAiFeedback.c` | `ai` support for cached analog feedback input channels. |

## Record address syntax

Examples:

```db
field(INP, "@RATE")
field(OUT, "@INODE")
field(OUT, "@PROC_ONODE")
field(OUT, "@PLUGIN")
```

## Compatibility notes

* C code stays compatible with old GCC/C89-style builds used by VxWorks 5.4.
* The parser avoids undefined behavior for non-ASCII byte values.
* EPICS 3.14.12 and EPICS 7 supported

## Adding a new support token

To add a new device-support token:

1. Add a `FB_*_STRING` macro in `devFeedback.h`.
2. Add the matching enum value to `enum enumFbCDevice`.
3. Add one row to `fbDeviceNameTable[]` in `devFeedback.c`.
4. Implement record-specific behavior in the relevant `dev*Feedback.c` module.
