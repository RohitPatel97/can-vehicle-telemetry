/*
 * STM32F446RE + STM32Cube HAL integration excerpt.
 *
 * This is documentation-quality integration code, not a standalone generated
 * main.c. Let CubeMX generate clocks, GPIO, CAN1 and USART2, then copy the
 * marked calls/callbacks into USER CODE sections. The bit timing below assumes
 * a 45 MHz APB1 peripheral clock:
 *
 *   45 MHz / prescaler 6 / (1 sync + 11 BS1 + 3 BS2) = 500 kbit/s
 *
 * Recalculate these values if the CubeMX clock tree differs.
 */

#include "main.h"
#include "telemetry_stm32_hal.h"

extern CAN_HandleTypeDef hcan1;
extern UART_HandleTypeDef huart2;

static telemetry_stm32_hal_t telemetry_context;

static HAL_StatusTypeDef telemetry_configure_can(void)
{
    CAN_FilterTypeDef filter = {0};

    hcan1.Init.Prescaler = 6U;
    hcan1.Init.SyncJumpWidth = CAN_SJW_1TQ;
    hcan1.Init.TimeSeg1 = CAN_BS1_11TQ;
    hcan1.Init.TimeSeg2 = CAN_BS2_3TQ;
    hcan1.Init.Mode = CAN_MODE_NORMAL;
    hcan1.Init.AutoBusOff = ENABLE;
    hcan1.Init.AutoRetransmission = ENABLE;

    if (HAL_CAN_Init(&hcan1) != HAL_OK) {
        return HAL_ERROR;
    }

    /* bxCAN filter registers align the 11-bit identifier at bit 5. */
    filter.FilterBank = 0U;
    filter.FilterMode = CAN_FILTERMODE_IDMASK;
    filter.FilterScale = CAN_FILTERSCALE_32BIT;
    filter.FilterIdHigh = (uint32_t)TELEMETRY_CAN_ID << 5U;
    filter.FilterIdLow = 0U;
    filter.FilterMaskIdHigh = 0x7FFU << 5U;
    /* Also match IDE=0 and RTR=0; the main-context adapter rechecks both. */
    filter.FilterMaskIdLow = 0x0006U;
    filter.FilterFIFOAssignment = CAN_RX_FIFO0;
    filter.FilterActivation = ENABLE;
    filter.SlaveStartFilterBank = 14U;

    if (HAL_CAN_ConfigFilter(&hcan1, &filter) != HAL_OK ||
        HAL_CAN_Start(&hcan1) != HAL_OK ||
        HAL_CAN_ActivateNotification(
            &hcan1, CAN_IT_RX_FIFO0_MSG_PENDING | CAN_IT_RX_FIFO0_OVERRUN | CAN_IT_ERROR |
                        CAN_IT_BUSOFF | CAN_IT_LAST_ERROR_CODE) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

/* Call once after MX_CAN1_Init() and MX_USART2_UART_Init(). */
void telemetry_application_init(void)
{
    telemetry_stm32_hal_init(&telemetry_context, &hcan1, &huart2,
                             TELEMETRY_CAN_ID, 250U);
    if (telemetry_configure_can() != HAL_OK) {
        Error_Handler();
    }
}

/* One bounded FIFO read/queue insertion per callback; no receiver/UART work. */
void HAL_CAN_RxFifo0MsgPendingCallback(CAN_HandleTypeDef *can)
{
    if (can == &hcan1) {
        telemetry_stm32_hal_on_fifo0(&telemetry_context);
    }
}

void HAL_CAN_ErrorCallback(CAN_HandleTypeDef *can)
{
    if (can == &hcan1) {
        telemetry_stm32_hal_on_can_error(&telemetry_context);
    }
}

/*
 * Call frequently from the main loop with interrupts enabled. Each call drains
 * at most 16 frames; diagnostic UART can wait up to 10 ms per accepted frame.
 * Read receiver/diagnostic counters here only, never from an interrupt.
 */
void telemetry_application_poll(void)
{
    if (telemetry_stm32_hal_poll(&telemetry_context)) {
        /* Application policy: illuminate a warning, log, or enter safe state. */
    }
}
