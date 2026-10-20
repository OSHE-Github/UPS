# Charger voltage host tests

Compile the actual production module with the fake HAL in this directory:

```sh
cc -std=c11 -Wall -Wextra -Werror \
  -ISTM32CubeIDE/Tests/battery_voltage -ISTM32CubeIDE/Core/Inc \
  STM32CubeIDE/Tests/battery_voltage/test_battery_voltage.c \
  STM32CubeIDE/Core/Src/battery_voltage.c -o /tmp/test_battery_voltage
/tmp/test_battery_voltage
```

A zero exit status means all assertions passed. Tests exercise the register
addresses and scaling, preservation of unrelated configuration, one-shot
completion, wrong device identification, every I2C transaction failing,
conversion timeout, charger reset/configuration changes, retry/recovery,
range limits, stale data, and clock rollover. They do not validate electrical
I2C operation or charger measurement accuracy on hardware.
