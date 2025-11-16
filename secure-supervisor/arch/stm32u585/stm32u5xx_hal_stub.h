/*
 *  Minimal stub of the STM32U5 HAL used only for compile‑time.
 *  The real HAL contains many types and functions; we provide
 *  lightweight definitions so that `machine_init.c` can compile
 *  without pulling in the full driver set.
 */

#include <stdint.h>

/* Basic types */
typedef uint32_t HAL_StatusTypeDef;
typedef uint32_t HAL_TickFreqTypeDef;

/* Macros used in the code */
#define HAL_OK           ((HAL_StatusTypeDef)0)
#define HAL_ERROR        ((HAL_StatusTypeDef)1)
#define HAL_TIMEOUT      ((HAL_StatusTypeDef)2)
#define HAL_BUSY         ((HAL_StatusTypeDef)3)

/* Dummy peripheral clock controls */
static inline void __HAL_RCC_GPIOA_CLK_ENABLE(void) {}
static inline void __HAL_RCC_GPIOB_CLK_ENABLE(void) {}
static inline void __HAL_RCC_GPIOC_CLK_ENABLE(void) {}
static inline void __HAL_RCC_GPIOD_CLK_ENABLE(void) {}
static inline void __HAL_RCC_GPIOE_CLK_ENABLE(void) {}
static inline void __HAL_RCC_USBFS_CLK_ENABLE(void) {}
static inline void __HAL_RCC_USART1_CLK_ENABLE(void) {}

/* Dummy HAL init functions */
static inline HAL_StatusTypeDef HAL_Init(void) { return HAL_OK; }
static inline HAL_StatusTypeDef HAL_Delay(uint32_t){ return HAL_OK; }
static inline HAL_StatusTypeDef HAL_RCC_OscConfig(void*) { return HAL_OK; }
static inline HAL_StatusTypeDef HAL_RCC_ClockConfig(void*, uint32_t) { return HAL_OK; }
static inline void __HAL_RCC_CRS_CLK_ENABLE(void) {}
static inline void __HAL_RCC_CRYP_CLK_ENABLE(void) {}
/** Additional dummy macros if required can be added here **/
