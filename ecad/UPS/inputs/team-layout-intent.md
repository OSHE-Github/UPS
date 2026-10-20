# Team layout intent supplied by the user

Source: chat excerpts pasted by the user on October 20, 2026. These establish design intent; they do not include the underlying current/thermal calculations.

- Tyler Blazoff, October 16, 2026, 10:05 AM: MCU at bottom left; high-power rails across the top toward output at top right; use large copper pours for each rail stage, including VSYS, 5 V, and VBUS.
- Tyler Blazoff, October 16, 2026, 10:23 AM: use multiple vias on high-power pads; suggested 3 × 2 arrays for margin and heat distribution.
- Mason Menosky, October 16, 2026, 10:39 AM: agreed with that approach.
- Andrew Kroll, October 16, 2026, 3:30 PM: recalled a prior calculation that one larger via could carry 5 A; noted that additional vias may still be useful.

The user also confirmed: the UPS goes underneath the Pi 5 like a boot, with batteries facing down.

Still needed for final copper and via sizing: per-stage continuous/peak currents, minimum battery voltage, efficiency and charging operating cases, allowable rail drop and temperature rise, and the fabricator's copper/plating/drill stack-up.
