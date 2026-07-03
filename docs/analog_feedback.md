# Analog feedback plugin startup

## IOC shell startup

After loading the application DBD and calling your generated registerRecordDeviceDriver function, run:

```iocsh
fbAnalogInitPlugin
fbListPlugins
fbpr
fbInit 0,0,0,0,0,0,100,100
fbOpen 1
```

`fbAnalogInitPlugin` is an alias for `fbAnalogPluginInstantiate`. It calls `feedbackInit(&fbAnalogDrvSet)` and then `fbActivate(fbAnalogDrvSet.id)`.

A generic IOC-shell `fbActivate <id>` command is also now registered by `feedback_common.c`, so you can activate any registered plugin manually:

```iocsh
fbListPlugins
fbActivate 0
```

## Example records

```db
record(ai, "$(P)AI0") {
    field(DTYP, "Feedback Analog Input")
    field(SCAN, "I/O Intr")
    field(PRIO, "HIGH")
    field(INP,  "@AIN,0")
    field(PREC, "6")
}
```

## Manual test without the feedback loop

You can publish a value directly into the analog cache and trigger I/O Intr processing:

```iocsh
fbAnalogSet 0,1.234
fbAnalogGet 0
```

## Hardware integration point

Write the callback functions. Use the API to register those functions:
```
// required
extern int fbSetAnalogInputUpdateCallback(fbAnalogInputUpdateCallback callback);
extern fbAnalogInputUpdateCallback fbGetAnalogInputUpdateCallback(void);
// optional
extern int fbSetAnalogFilterUpdateCallback(fbAnalogFilterUpdateCallback callback);
extern fbAnalogFilterUpdateCallback fbGetAnalogFilterUpdateCallback(void);
extern int fbSetAnalogPluginInitCallback(fbAnalogPluginHookCallback callback);
extern fbAnalogPluginHookCallback fbGetAnalogPluginInitCallback(void);
extern int fbSetAnalogPluginOpenCallback(fbAnalogPluginHookCallback callback);
extern fbAnalogPluginHookCallback fbGetAnalogPluginOpenCallback(void);
extern int fbSetAnalogPluginCloseCallback(fbAnalogPluginHookCallback callback);
extern fbAnalogPluginHookCallback fbGetAnalogPluginCloseCallback(void);
```
