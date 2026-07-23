# Linux trigger modes

The Linux feedback task trigger modes selects how the feedback task waits.

## Mode summary

| Value | Name | Linux backend | Rate setting |
| ---: | --- | --- | --- |
| `0` | Hardware | periodic `timerfd` using `CLOCK_MONOTONIC` | `RATE` / `fbSetRate()` |
| `1` | Software | absolute `clock_nanosleep()` deadlines | `SOFTTRIGGERRATE` / `fbSetSoftTriggerRate()` |
| `2` | Manual | EPICS event signalled by `fbTrigger()` | no periodic rate |

`RATE` and `SOFTTRIGGERRATE` have to be set indipendantly according to the desired mode.

## Hardware mode: Linux timerfd

Linux Hardware mode creates a periodic timer with:

```c
timerfd_create(CLOCK_MONOTONIC, ...)
timerfd_settime(..., TFD_TIMER_ABSTIME, ...)
```

The `RATE` value is converted to the timer interval. 
The feedback task blocks and waits for timer descriptor or a stop event.

The feedback task runs one feedback cycle and counts hardware-trigger overruns. Missed cycles are not replayed as a burst.

Calling `fbSetRate()` while Hardware mode is running re-arms the timer and resets the hardware-overrun counter. `fbClose()` signals stop, so a blocked Hardware-mode wait exits immediately.

The term *Hardware* is retained for compatibility with the existing trigger-mode interface. On Linux this mode is driven by the kernel timerfd/high-resolution timer. The timerfd API has got better diagnostics and fast shutdown handling.

The manual trigger mode can be used to trigger from real hardware interrupt contexts on Linux.

## Software mode: preserved absolute wait

Linux Software mode is based on:

```c
clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, ...)
```

Absolute deadlines avoid timing drift. When processing takes longer than one period, expired deadlines are skipped and counted by the software-overrun counter.

A Software-mode `fbClose()` is observed when the current absolute wait finishes, so shutdown latency is at most approximately one software period. This timer probably has got slightly lower jitter.

## Manual mode

Manual mode waits on an EPICS event. Each successful `fbTrigger()` call releases one feedback cycle.

On Linux, `fbTrigger()` is accepted only in Manual mode.

## IOC shell examples

### Hardware timerfd at 1000 Hz

```iocsh
fbClose 1
fbSetTriggerMode 0
fbSetRate 1000
fbOpen 1
```

### Absolute software trigger at 250.5 Hz

```iocsh
fbClose 1
fbSetSoftTriggerRate 250.5
fbSetTriggerMode 1
fbOpen 1
```

### Manual one-shot cycles

```iocsh
fbClose 1
fbSetTriggerMode 2
fbOpen 1
fbTrigger 1
fbTrigger 1
fbClose 1
```

Trigger mode may be changed only while the feedback loop is closed. Software mode additionally requires a positive software rate before it can be selected or opened.

## Debug

Use:

```iocsh
fbpr 0
```

To print the feedbcak report.

The C API provides:

```c
int fbGetHardwareTriggerOverruns(unsigned long *overruns);
int fbGetSoftTriggerOverruns(unsigned long *overruns);
```
