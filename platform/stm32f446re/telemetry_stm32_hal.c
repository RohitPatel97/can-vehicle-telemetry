#include "telemetry_stm32_hal.h"

#include <stdio.h>
#include <string.h>

enum { UART_LINE_CAPACITY = 64 };

static uint32_t lock_irqs(void)
{
    const uint32_t saved = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return saved;
}

static void unlock_irqs(uint32_t saved)
{
    __DMB();
    __set_PRIMASK(saved);
}

static uint32_t saturating_add(uint32_t value, uint32_t increment)
{
    return increment > UINT32_MAX - value ? UINT32_MAX : value + increment;
}

void telemetry_stm32_hal_init(telemetry_stm32_hal_t *context,
                              CAN_HandleTypeDef *can,
                              UART_HandleTypeDef *uart,
                              uint32_t standard_id,
                              uint32_t timeout_ms)
{
    if (context == NULL) {
        return;
    }

    (void)memset(context, 0, sizeof(*context));
    context->can = can;
    context->uart = uart;
    context->standard_id = standard_id;
    telemetry_receiver_init(&context->receiver, timeout_ms);
}

static void forward_uart(telemetry_stm32_hal_t *context,
                         const telemetry_decoded_t *decoded)
{
    char line[UART_LINE_CAPACITY];
    int length;

    if (context->uart == NULL) {
        return;
    }

    length = snprintf(line, sizeof(line), "TEL,%u,%u.%02u,%d,%u,%u\r\n",
                      (unsigned int)decoded->sequence,
                      (unsigned int)(decoded->sample.speed_centi_kph / 100U),
                      (unsigned int)(decoded->sample.speed_centi_kph % 100U),
                      (int)decoded->sample.coolant_c,
                      (unsigned int)decoded->sample.battery_mv,
                      (unsigned int)decoded->flags);

    if (length > 0 && (size_t)length < sizeof(line)) {
        if (HAL_UART_Transmit(context->uart, (uint8_t *)line,
                              (uint16_t)length, 10U) != HAL_OK) {
            context->uart_errors = saturating_add(context->uart_errors, 1U);
        }
    } else {
        context->uart_errors = saturating_add(context->uart_errors, 1U);
    }
}

void telemetry_stm32_hal_on_fifo0(telemetry_stm32_hal_t *context)
{
    telemetry_stm32_rx_event_t event = {0};
    uint32_t saved;

    if (context == NULL || context->can == NULL) {
        return;
    }

    if (HAL_CAN_GetRxMessage(context->can, CAN_RX_FIFO0, &event.header,
                             event.frame) !=
        HAL_OK) {
        telemetry_stm32_hal_on_can_error(context);
        return;
    }
    event.received_ms = HAL_GetTick();

    saved = lock_irqs();
    if (context->queued == TELEMETRY_STM32_QUEUE_CAPACITY) {
        context->pending_queue_overruns =
            saturating_add(context->pending_queue_overruns, 1U);
    } else {
        context->queue[context->write_index] = event;
        context->write_index = (uint8_t)((context->write_index + 1U) %
                                        TELEMETRY_STM32_QUEUE_CAPACITY);
        ++context->queued;
    }
    unlock_irqs(saved);
}

void telemetry_stm32_hal_on_can_error(telemetry_stm32_hal_t *context)
{
    uint32_t saved;
    if (context == NULL) {
        return;
    }
    saved = lock_irqs();
    context->pending_can_errors = saturating_add(context->pending_can_errors, 1U);
    unlock_irqs(saved);
}

static bool pop_event(telemetry_stm32_hal_t *context,
                       telemetry_stm32_rx_event_t *event)
{
    const uint32_t saved = lock_irqs();
    const bool available = context->queued != 0U;
    if (available) {
        *event = context->queue[context->read_index];
        context->read_index = (uint8_t)((context->read_index + 1U) %
                                       TELEMETRY_STM32_QUEUE_CAPACITY);
        --context->queued;
    }
    unlock_irqs(saved);
    return available;
}

static bool timeout_transition(telemetry_stm32_hal_t *context, uint32_t now_ms)
{
    const bool was_timed_out = context->receiver.timed_out;
    (void)telemetry_receiver_check_timeout(&context->receiver, now_ms);
    return !was_timed_out && context->receiver.timed_out;
}

bool telemetry_stm32_hal_poll(telemetry_stm32_hal_t *context)
{
    uint32_t saved;
    uint32_t can_errors;
    uint32_t overruns;
    unsigned int processed;
    bool entered_timeout = false;

    if (context == NULL) {
        return false;
    }

    saved = lock_irqs();
    can_errors = context->pending_can_errors;
    overruns = context->pending_queue_overruns;
    context->pending_can_errors = 0U;
    context->pending_queue_overruns = 0U;
    unlock_irqs(saved);
    context->receiver.stats.can_errors =
        saturating_add(context->receiver.stats.can_errors, can_errors);
    context->queue_overruns = saturating_add(context->queue_overruns, overruns);

    for (processed = 0U; processed < TELEMETRY_STM32_QUEUE_CAPACITY; ++processed) {
        telemetry_stm32_rx_event_t event;
        telemetry_decoded_t decoded;
        if (!pop_event(context, &event)) {
            break;
        }
        /* Preserve an outage even if recovery was queued before main ran. */
        if (timeout_transition(context, event.received_ms)) {
            entered_timeout = true;
        }
        if (event.header.IDE != CAN_ID_STD || event.header.RTR != CAN_RTR_DATA ||
            event.header.StdId != context->standard_id) {
            context->envelope_errors = saturating_add(context->envelope_errors, 1U);
            continue;
        }
        if (event.header.DLC != TELEMETRY_FRAME_SIZE) {
            ++context->receiver.stats.length_errors;
            continue;
        }
        if (telemetry_receiver_process(&context->receiver, event.frame,
                                        TELEMETRY_FRAME_SIZE, event.received_ms,
                                        &decoded) == TELEMETRY_OK) {
            forward_uart(context, &decoded);
        }
    }

    if (timeout_transition(context, HAL_GetTick())) {
        entered_timeout = true;
    }
    return entered_timeout;
}
