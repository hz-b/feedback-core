# API

Short API overview for `feedback_common`; C/C++ functions return `OK` or `ERROR` unless noted otherwise.

## C/C++ interface

### Core feedback task (`fbCore.h`)

| Function | Comment |
| --- | --- |
| `fbInit(inpcard, inpsignal, outcard, outsignal, inode, onode, rate, priority)` | Initialize the active feedback plugin and task parameters. |
| `fbConfigure(inpcard, inpsignal, outcard, outsignal, inode, onode, rate, priority)` | Reconfigure cards, signals, nodes, rate, and priority for the active plugin. |
| `fbOpen()` | Start or resume the feedback loop. |
| `fbClose()` | Request the feedback loop to stop. |
| `fbTrigger()` | Trigger one feedback-loop iteration when soft triggering is used. |
| `fbActivate(id)` | Activate a registered plugin by registry id. |
| `fbDeactivate()` | Deactivate the current plugin when the loop is not running. |
| `fbSetNodes(inode, onode)` | Select active input and output node callbacks. |
| `fbGetNodes(inode, onode)` | Read the active input and output node indexes. |
| `fbGetNodeName(index, name, length)` | Copy a plugin node name into a caller buffer. |
| `fbHome(flags)` | Store home flags and call the plugin home callback if present. |
| `fbGetStatus(status)` | Read the active plugin status value. |
| `fbGetFlags(flags)` | Read the active plugin flags. |
| `fbSetFlags(flags)` | Set the active plugin flags. |
| `fbGetRate(rate)` | Read the feedback-loop rate. |
| `fbSetRate(rate)` | Set the feedback-loop rate. |
| `fbSetSoftTriggerRate(rate)` | Configure the software trigger rate. |
| `fbGetSoftTriggerRate(rate)` | Read the calculated software trigger rate. |
| `fbGetPriority(priority)` | Read the feedback task priority. |
| `fbSetPriority(priority)` | Set the feedback task priority where supported. |

#### Debugging

| Function | Comment |
| --- | --- |
| `fbpr()` | Print feedback core and active plugin status. |

### Plugin registry (`fbRegistry.h`)

| Function | Comment |
| --- | --- |
| `feedbackInit(plugin)` | Register a `tfbdrvset` plugin and assign its id. |
| `feedbackGetPluginById(id, feedbackPlugin)` | Return a registered plugin pointer by id. |
| `feedbackGetPluginNameById(id, name, n)` | Copy a registered plugin name into a caller buffer. |
| `feedbackGetPluginId(id)` | Return the id of the active plugin. |

### Analog feedback plugin (`fbAnalog.h`)

| Function | Comment |
| --- | --- |
| `fbAnalogInit()` | Initialize cached analog channels and I/O Intr scan state. |
| `fbAnalogFilterInit()` | Reset all analog filter states. |
| `fbAnalogSet(channel, value)` | Publish a cached analog value and request I/O Intr processing. |
| `fbAnalogGet(channel, value)` | Read a cached analog value. |
| `fbAnalogFilterApply(f, x)` | Apply one first-order filter step and return the filtered value. |
| `fbAnalogSetFilterDelay(delaySamples, channel)` | Configure filter delay for one analog channel. |
| `fbAnalogGetFilterDelay(delaySamples, channel)` | Read filter delay for one analog channel. |
| `fbSetAnalogInputUpdateCallback(callback)` | Install the analog input provider callback. |
| `fbGetAnalogInputUpdateCallback()` | Return the active analog input provider callback. |
| `fbSetAnalogFilterUpdateCallback(callback)` | Install the optional filtered-value callback. |
| `fbGetAnalogFilterUpdateCallback()` | Return the optional filtered-value callback. |
| `fbSetAnalogPluginInitCallback(callback)` | Install the optional analog plugin init hook. |
| `fbGetAnalogPluginInitCallback()` | Return the optional analog plugin init hook. |
| `fbSetAnalogPluginOpenCallback(callback)` | Install the optional analog plugin open hook. |
| `fbGetAnalogPluginOpenCallback()` | Return the optional analog plugin open hook. |
| `fbSetAnalogPluginCloseCallback(callback)` | Install the optional analog plugin close hook. |
| `fbGetAnalogPluginCloseCallback()` | Return the optional analog plugin close hook. |
| `fbAnalogPluginInstantiate()` | Register and activate the built-in analog feedback plugin. |

### Callback types and plugin structures

| Interface | Comment |
| --- | --- |
| `INTSUPFUN` | Generic plugin callback function pointer type. |
| `fbAnalogInputUpdateCallback` | Callback that supplies one raw analog input value. |
| `fbAnalogFilterUpdateCallback` | Callback that receives raw and filtered analog values. |
| `fbAnalogPluginHookCallback` | Callback type for analog plugin lifecycle hooks. |
| `tfbdrvpar` | Runtime parameter block shared by the core and active plugin. |
| `tfbdrvset` | Plugin jump table containing lifecycle, input-node, and output-node callbacks. |
| `fbAnalogFilterState` | Per-channel state for the analog first-order filter. |

## EPICS IOC Shell Interface

| Command | Comment |
| --- | --- |
| `fbAnalogPluginInstantiate` | Register and activate the built-in analog feedback plugin. |
| `fbAnalogInitPlugin` | Alias for `fbAnalogPluginInstantiate`. |
| `fbAnalogSetFilterDelay delaySamples, channel` | Set analog filter delay for one channel. |
| `fbAnalogSet channel, value` | Manually publish an analog channel value. |
| `fbAnalogGet channel` | Print the cached value for an analog channel. |
| `fbInit inpcard, inpsignal, outcard, outsignal, inode, onode, rate, priority` | Initialize the feedback loop configuration. |
| `fbOpen arg` | Start or resume the feedback loop. |
| `fbClose arg` | Request the feedback loop to stop. |
| `fbTrigger arg` | Trigger one software-driven feedback-loop iteration. |
| `fbpr arg` | Print feedback core and plugin status. |
| `fbActivate id` | Activate a registered plugin by id. |
| `fbDeactivate` | Deactivate the active plugin. |
| `fbListPlugins` | List registered plugins and mark the active plugin. |

## EPICS device support / database interface

### Device support names

| DTYP | Record types | Comment |
| --- | --- | --- |
| `Feedback Analog Input` | `ai` | Read cached analog channels with I/O Intr support. |
| `devLiFBCommon` | `longin` | Read common feedback state and execute read-side commands. |
| `devLoFBCommon` | `longout` | Write common feedback parameters and execute write-side commands. |
| `devFbGetMbbi` | `mbbi` | Read active plugin/node selections with dynamic enum strings. |
| `devFbSetMbbo` | `mbbo` | Select active plugin or nodes with dynamic enum strings. |

### INP/OUT field tokens 

| Token | Comment |
| --- | --- |
| `@AIN,<channel>` | Analog `ai` input channel, valid range `0..FB_ANALOG_MAX_CHANNELS-1`. |
| `@INIT` | Initialize common feedback support from a record. |
| `@OPEN` | Open/start the feedback loop. |
| `@CLOSE` | Close/stop the feedback loop. |
| `@ICARD` | Input card selector token. |
| `@ISIG` | Input signal selector token. |
| `@OCARD` | Output card selector token. |
| `@OSIG` | Output signal selector token. |
| `@RATE` | Feedback-loop rate token. |
| `@PRIORITY` | Feedback task priority token. |
| `@STATUS` | Active plugin status token. |
| `@VAL` | Generic long value token. |
| `@IVAL` | Generic signed integer value token. |
| `@UIVAL` | Generic unsigned integer value token. |
| `@SVAL` | Generic short value token. |
| `@DVAL` | Generic double value token. |
| `@INODE` | Active input node token. |
| `@ONODE` | Active output node token. |
| `@TRIGGER` | Software trigger token. |
| `@HOME` | Home flags/action token. |
| `@FLAGS` | Feedback flags token. |
| `@PROC_ONODE` | Process selected output node in EPICS scan context. |
| `@PROC_INODE` | Process selected input node in EPICS scan context. |
| `@PLUGIN` | Active plugin selection token. |
| `@SOFTTRIGGERRATE` | Software trigger rate token. |
| `@DATASETEVENTS` | Post dataset-ready EPICS events. |
