# Embedded feedback test IOC

This is a minimal Linux IOC for `feedback-core`


## Run

From the repository root:

```sh
make -C testIoc run
```

The IOC starts the built-in analog feedback plugin and publishes four records:

```text
FBTEST:AI0
FBTEST:AI1
FBTEST:AI2
FBTEST:AI3
```

Verify:

```sh
camonitor FBTEST:AI0 FBTEST:AI1 FBTEST:AI2 FBTEST:AI3
```
