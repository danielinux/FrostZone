# STM32H5 Peripheral Mapping

## ILI9341 TFT Console

- `SPI1_SCK` – `PA5` (alternate function 5, non-secure)
- `SPI1_MOSI` – `PB5` (AF5, non-secure)
- `SPI1_MISO` – not connected
- `LCD_CS` – `PD14` (GPIO output, active low, non-secure)
- `LCD_RST` – `PD15` (GPIO output, non-secure)
- `LCD_DC` – `PF3` (GPIO output, command/data select, non-secure)
- `LCD_BL` – `PB3` (GPIO output, backlight enable, non-secure)

All associated GPIOs are configured as non-secure by the secure supervisor to allow the frosted kernel to drive the panel.

