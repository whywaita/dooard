# Power notes for M5Stack Core2

dooard currently behaves as an always-on M5Stack Core2 weather display. Use this
document to measure the load of the existing firmware, compare changes, and
decide whether a battery deployment needs brightness, Wi-Fi, or sleep changes.

## Hardware assumptions

- The PlatformIO target is `m5stack-core2`.
- M5Stack's current Core2 K010 documentation lists a 5 V at 500 mA input, an
  AXP192 PMU, and a 500 mAh at 3.7 V internal lithium battery.
- The Core2 version history says the battery capacity changed to 500 mAh in
  2023-10. Older Core2 material and units may still be based on 390 mAh packs,
  so record the capacity printed on the unit or purchase record before turning a
  measurement into a runtime estimate.
- Core2 v1.1 uses AXP2101 plus INA3221 power management, while Core2 v1.3 uses
  AXP192 again. dooard uses M5Unified, so keep PMIC revision in the measurement
  notes whenever comparing units.
- M5Unified exposes battery and PMIC helpers such as `M5.Power.isCharging()`,
  `M5.Power.getBatteryLevel()`, `M5.Power.getBatteryVoltage()`,
  `M5.Power.getBatteryCurrent()`, `M5.Power.setLed()`,
  `M5.Power.timerSleep()`, `M5.Power.lightSleep()`, and
  `M5.Power.powerOff()`.

Sources:

- <https://docs.m5stack.com/en/core/core2>
- <https://docs.m5stack.com/en/core/Core2%20v1.1>
- <https://docs.m5stack.com/en/core/Core2_v1.3>
- <https://docs.m5stack.com/en/arduino/m5core2/battery>
- <https://docs.m5stack.com/en/arduino/m5unified/power_class>

## Current dooard behavior

The current firmware does not enter light sleep, deep sleep, or timed power-off.
That makes the display easy to read but means the battery is treated as a short
backup supply, not as a full-day power source.

- `setup()` initializes M5Unified, leaves the display on, starts Wi-Fi station
  mode, connects to the configured access point, syncs time, and fetches weather.
- The screen is redrawn after every successful fetch and after manual refresh.
- Weather refresh runs every 15 minutes via `kWeatherRefreshIntervalMs`.
- Clock sync runs every 6 hours via `kClockSyncIntervalMs`.
- If Wi-Fi is disconnected, the firmware retries connection every 10 seconds.
- The main loop calls `M5.update()` and then delays for 50 ms.
- Button A, B, or C triggers an immediate refresh.

## Measurement checklist

Measure before changing firmware. Battery percentage alone is not enough because
the discharge curve is nonlinear and PMIC reporting differs between Core2
revisions.

1. Flash the exact commit that will be measured and write down the short SHA.
2. Record the Core2 revision, PMIC if known, battery capacity, battery age,
   charge state, display brightness, Wi-Fi RSSI/location, and attached modules.
3. Use a USB power meter on the USB-C input for repeatable relative comparisons.
   This is the easiest way to compare brightness levels and refresh policies.
4. For battery-only tests, also log `M5.Power.getBatteryVoltage()` and
   `M5.Power.getBatteryCurrent()` over serial or on screen. Confirm the sign and
   rough magnitude against an external meter before using it as current draw.
5. Measure these phases separately:
   - boot, Wi-Fi connect, NTP sync, and first weather fetch;
   - steady display with Wi-Fi connected for at least 5 minutes;
   - one manual button refresh;
   - disconnected Wi-Fi retry behavior for at least 2 minutes;
   - any candidate dimming or sleep policy.
6. Run at least three 15-minute refresh cycles before calling an average stable.
7. Do not treat the numbers as a runtime guarantee. Use them as deployment
   planning data and remeasure after hardware, firmware, or Wi-Fi changes.

For quick serial logging during a local measurement build, print PMIC readings
after `M5.begin(cfg)` and after each weather refresh:

```cpp
Serial.printf("charging=%d level=%d voltage_mv=%d current_ma=%d\n",
              M5.Power.isCharging(),
              M5.Power.getBatteryLevel(),
              M5.Power.getBatteryVoltage(),
              M5.Power.getBatteryCurrent());
```

## Runtime estimate

If current is measured at the battery, estimate runtime with:

```text
runtime_hours = usable_capacity_mAh / average_current_mA
```

Use 70% to 85% of the printed capacity as a conservative usable capacity unless
you have characterized the actual cell. For example:

- 500 mAh pack, 80% usable, 140 mA average: about 2.9 hours.
- 390 mAh pack, 80% usable, 140 mA average: about 2.2 hours.

If current is measured at USB input, use the number to compare firmware changes.
Do not convert it directly to battery runtime unless charge state and conversion
efficiency are known.

## Reduction options

Start with display and retry behavior. They are easier to validate than sleep
and they preserve the always-on display use case.

1. Dim the display after initialization:

   ```cpp
   M5.Display.setBrightness(64);
   ```

   Measure several values in the target lighting. A desk display often needs
   much less than full brightness.

2. Turn off the power indicator LED when it is not needed:

   ```cpp
   M5.Power.setLed(0);
   ```

3. Increase the weather refresh interval if 15 minutes is more frequent than the
   deployment needs. This reduces HTTPS and JSON work, but the display and Wi-Fi
   idle load still dominate an always-on deployment.

4. If always-on display is not required, add a periodic snapshot mode that wakes,
   connects, fetches, draws, and then sleeps:

   ```cpp
   M5.Power.timerSleep(15 * 60);
   ```

   Treat this as a product behavior change. Validate wake reliability, RTC/NTP
   behavior, button behavior, and whether the display state is acceptable while
   the unit sleeps.

5. If the screen must stay on but network freshness can be delayed, disconnect
   Wi-Fi between fetches and reconnect before the next refresh. Measure both the
   saved idle current and the reconnect spike:

   ```cpp
   WiFi.disconnect(true);
   WiFi.mode(WIFI_OFF);
   ```

6. Add exponential backoff for disconnected Wi-Fi sites. The current 10-second
   retry loop is useful during setup but can waste power in an outage.

## Deployment guidance

- Use USB-C power for an always-on desk display. The internal battery should be
  treated as backup unless measurements prove the required runtime.
- Prefer battery-only sleep mode for portable use. A continuously lit LCD plus
  Wi-Fi radio is the wrong baseline for long unattended runtime.
- Keep modules and GROVE devices out of the measurement unless they are part of
  the deployment. External devices can dominate current draw.
- Keep the unit in a strong Wi-Fi location. Poor signal or repeated reconnects
  can erase savings from a longer refresh interval.
- Re-run the checklist after changing brightness, refresh interval, Wi-Fi policy,
  sleep policy, or Core2 hardware revision.
