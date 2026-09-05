#ifndef TEST_STM32F4XX_HAL_H
#define TEST_STM32F4XX_HAL_H

/* Host test double: never add this include path to STM32 firmware builds. */
#include <stdint.h>

typedef struct { uint32_t instance; } CAN_HandleTypeDef;
typedef struct { uint32_t instance; } UART_HandleTypeDef;
typedef struct {
    uint32_t StdId;
    uint32_t ExtId;
    uint32_t IDE;
    uint32_t RTR;
    uint32_t DLC;
    uint32_t Timestamp;
    uint32_t FilterMatchIndex;
} CAN_RxHeaderTypeDef;

typedef enum { HAL_OK = 0, HAL_ERROR = 1, HAL_BUSY = 2, HAL_TIMEOUT = 3 }
    HAL_StatusTypeDef;

#define CAN_ID_STD 0x00000000U
#define CAN_ID_EXT 0x00000004U
#define CAN_RTR_DATA 0x00000000U
#define CAN_RTR_REMOTE 0x00000002U
#define CAN_RX_FIFO0 0U

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __DMB(void);
void __set_PRIMASK(uint32_t value);
uint32_t HAL_GetTick(void);
HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *can, uint32_t fifo,
                                      CAN_RxHeaderTypeDef *header,
                                      uint8_t *data);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                   uint16_t size, uint32_t timeout);

#endif
