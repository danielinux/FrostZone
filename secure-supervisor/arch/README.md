# Architecture‑specific code

All files in this directory contain code that is specific to a particular
ARMv8‑M target.  The kernel and supervisor build system will pick the
appropriate directory based on the `TARGET` variable.

* `rp2350/` – code for the Raspberry‑Pi Pico RP2350.
* `stm32u585/` – code for the STM32U5 series.
* `stm32h563/` – code shared by the STM32H5 port.

The files provide a `machine_init()` function that performs low‑level
initialisation (clock configuration, peripheral aliasing, etc.) and expose
the same set of `__attribute__((cmse_nonsecure_entry))` symbols as the RP2350
implementations.

--- End of File ---
