# STM32H5 Peripheral Mapping

### Status LEDs
- `PB0` – LD1 (green) status LED, GPIO output, non-secure (`secure-supervisor/include/stm32h563.h:361`).
- `PF4` – LD2 (orange) status LED, GPIO output, non-secure.
- `PG4` – LD3 (red) status LED, GPIO output, non-secure.

All associated GPIOs are configured as non-secure by the secure supervisor to allow the frosted kernel to drive the panel.
### USART3 / ST-LINK virtual COM
- `PD8` – USART3_TX routed to the ST-LINK-V3 VCP (`userland/micropython/ports/stm32/boards/NUCLEO_H563ZI/mpconfigboard.h:45`).
- `PD9` – USART3_RX routed to the ST-LINK-V3 VCP.

### USB Full-Speed & Type-C glue
- `PA11` / `PA12` – USB FS DM/DP pair, non-secure (`secure-supervisor/include/stm32h563.h:369`).
- `PB13` / `PB14` – UCPD CC1/CC2 sideband, non-secure (`secure-supervisor/include/stm32h563.h:372`).
- `PG7` – Type-C fault indication (UCPD_FLT), non-secure.
- `PA9` – Dead-battery pin (UCPD_DBn), non-secure.
- `PA4` – VBUS sense input, non-secure.

### Ethernet RMII (LAN8742A on NUCLEO-H563ZI)
- `PA1` – RMII_REF_CLK, alternate function.
- `PA2` – RMII_MDIO.
- `PA7` – RMII_CRS_DV.
- `PC1` – RMII_MDC.
- `PC4` / `PC5` – RMII_RXD0 / RMII_RXD1.
- `PG11` – RMII_TX_EN.
- `PG13` – RMII_TXD0.
- `PB15` – RMII_TXD1.

## ILI9341 TFT console (SPI3-based attachment)

- `LCD_BL` – `PC11` (GPIO output, backlight enable, non-secure).
- `SPI3_SCK` – `PC10` (alternate function 6, non-secure).
- `SPI3_MOSI` – `PC12` (alternate function 6, non-secure). `SPI3_MISO` remains unconnected.
- `LCD_CS` – `PA6` (GPIO output, active low, non-secure).
- `LCD_RST` – `PC0` (GPIO output, non-secure).
- `LCD_DC` – `PF6` (GPIO output, command/data select, non-secure).

