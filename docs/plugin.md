# Feedback Plugin

This leightwight plugin structure is very simple, define
`tfbdrvset` jump table and the `tfbdrvpar` parameter structure.

The feedback core calls the lifecycle callbacks (`configure`, `init`,
`open`, `close`) and executes one selected input node and one selected
output node in the real-time feedback loop. Plugins are registered with
`feedbackInit()` and activated through the common feedback interface.

- At least one input node is required.
- Callbacks should be deterministic, fast, and non-blocking when a real-time feedback task is intended.

## Minimal example

``` c
static tfbdrvpar feedbackPar;

static int inputNode(tfbdrvpar *pfb);
static int outputNode(tfbdrvpar *pfb);

static tfbdrvset myPlugin = {
    // name 
    "Minimal Feedback Plugin",
     { "input", // input node names
      "", "", "", "", "", "", "", 
      "output", // output node names
      "", "", "", "", "", "", "" },
    0, // id
    1, // version
    sizeof(tfbdrvset),
    &feedbackPar,
    // report(), configure(), init(), ...
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    // input function array
    inputNode, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    // output function array
    outputNode, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};

void pluginRegister(void)
{
    feedbackInit(&myPlugin);
}
```

