# Feedback Core Module

Minimal EPICS support module with a small plugin interface for RT feedback tasks with high update rates.
Common device-support records, and an analog feedback example for I/O Intr support.

## Structure

- Core feedback task and plugin jump table interface
- Common EPICS device-support records for feedback configuration and control
- Generic analog feedback plugin with cached analog input channels
- Example EPICS database for analog feedback records

## Documentation

See the documentation in `docs/`:

- [docs/api.md](https://github.com/hz-b/feedback-core/blob/main/docs/api.md) - short C/C++, IOC shell, and database interface reference
- [docs/plugin.md](https://github.com/hz-b/feedback-core/blob/main/docs/plugin.md) - implementing a feedback plugin with the `tfbdrvset` jump table
- [docs/support.md](https://github.com/hz-b/feedback-core/blob/main/docs/support.md) - common feedback device-support modules and record address syntax
- [docs/analog_feedback.md](https://github.com/hz-b/feedback-core/blob/main/docs/analog_feedback.md) - analog feedback plugin startup and callback integration
- `CHANGELOG.md` - notable changes for releases and publishing

## Getting Started

### Build 
```
# Create configure/RELEASE file
# This sets default path to EPICS base:
./configure.pl
# build the module
make -j
```
### Set up the IOC using the built-in analog example support

In the startup script, after loading the application DBD, but before `iocInit()`, add:

```iocsh
dbLoadRecords("db/feedback_analog_example.db", "IOC_NAME=example")
dbLoadRecords("db/feedback_control_example.db", "IOC_NAME=example")

fbAnalogInitPlugin
fbInit 0,0,0,4,0,0,100,100
```

After `iocInit()`, start the feedback loop from the IOC shell:

```iocsh
fbOpen 1
```

Alternatively, start it through EPICS records:

```sh
# set soft sample rate
caput example:setSoftTaskRate 100

# start the feedback task
caput example:open 1

# monitor the generated example output
camonitor example:AI0 example:AI1 example:AI2 example:AI3
```

`fbAnalogInitPlugin` registers and activates the generic analog feedback plugin.
The example `fbInit` call configures analog channels `0..3`, input node `0`, rate `100 Hz`.

See `examples/feedback_analog_example.db` for a minimal analog feedback input database example.

See `examples/feedback_control_example.db` for a minimal feedback control database example.


## Compatibility

This module is intended to stay compatible with Linux, VxWorks 5.4, EPICS 3.14.12.x, and EPICS 7.

## Dependencies

EPICS Base 3.14.12.x or higher and VxWorks 5.4 only when building for legacy VxWorks targets.

## Contributing

Contributions are welcome. Please keep changes small, focused, and compatible with the supported EPICS and VxWorks versions.

Before submitting a change:

- build the module on the platforms available to you;
- keep C/C++ code compatible with old GCC/C89-style VxWorks builds;
- update the relevant documentation in `docs/`;

Any feedback is welcome for larger changes.

## References

- ** STATUS OF THE CONTINUOUS MODE SCAN FOR UNDULATOR BEAMLINES AT BESSY II **
  A. Balzer, E. Schierle, E. Suljoti, M. Witt, HZB, Berlin, Germany R. Follath, PSI, Villigen, Switzerland
  *Proceedings of ICALEPCS 2015*  
  https://www.helmholtz-berlin.de/media/media/forschung/wi/optik-strahlrohre/mono/icalepcs2015-thha3o02.pdf

## License

MIT License. See `LICENSE`.
