#ifndef TELEMETRY_STM32_HAL_H
#define TELEMETRY_STM32_HAL_H

/*
 * The host tests compile this adapter against a small HAL test double, not the
 * real STM32CubeF4 HAL. Add this file and telemetry_stm32_hal.c to a
 * CubeIDE/CubeMX target whose include path also contains this repository's
 * include/ directory.
 */
#include "stm32f4xx_hal.h"

#include <stdbool.h>
#include <stdint.h>

#include "telemetry/receiver.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { TELEMETRY_STM32_QUEUE_CAPACITY = 16 };

typedef struct telemetry_stm32_rx_event {
    CAN_RxHeaderTypeDef header;
    uint32_t received_ms;
    uint8_t frame[TELEMETRY_FRAME_SIZE];
} telemetry_stm32_rx_event_t;

typedef struct telemetry_stm32_hal {
    CAN_HandleTypeDef *can;
    UART_HandleTypeDef *uart;
    telemetry_receiver_t receiver;
    uint32_t standard_id;
    /* Main-context diagnostics. Deferred counters saturate at UINT32_MAX. */
    uint32_t queue_overruns;
    uint32_t envelope_errors;
    uint32_t uart_errors;
    /* Private IRQ/main handoff, guarded by PRIMASK-preserving critical sections. */
    telemetry_stm32_rx_event_t queue[TELEMETRY_STM32_QUEUE_CAPACITY];
    volatile uint32_t pending_can_errors;
    volatile uint32_t pending_queue_overruns;
    volatile uint8_t read_index;
    volatile uint8_t write_index;
    volatile uint8_t queued;
} telemetry_stm32_hal_t;

/** Initialize before enabling CAN interrupts. Do not reinitialize while active. */
void telemetry_stm32_hal_init(telemetry_stm32_hal_t *context,
                              CAN_HandleTypeDef *can,
                              UART_HandleTypeDef *uart,
                              uint32_t standard_id,
                              uint32_t timeout_ms);

/**
 * IRQ context: read one FIFO0 message and enqueue its raw envelope, bytes and
 * receive tick. No validation, receiver mutation, formatting or UART occurs.
 * If full, consume/drop the newest frame and count a software queue overrun.
 */
void telemetry_stm32_hal_on_fifo0(telemetry_stm32_hal_t *context);

/** IRQ context: defer one HAL error callback occurrence (not each error bit). */
void telemetry_stm32_hal_on_can_error(telemetry_stm32_hal_t *context);

/**
 * Main context only, with interrupts enabled. Process at most QUEUE_CAPACITY
 * frames, forward accepted data, and check timeout at each receive tick and the
 * current tick. Returns true if any new timeout occurred during this poll,
 * including an outage followed by recovery in the same batch. Read
 * receiver.timed_out to distinguish an ongoing outage from recovered traffic.
 * UART is blocking here (10 ms timeout per accepted frame), never in an ISR.
 * Receiver and public diagnostics must be read only from this main context.
 * This adapter is for single-core bare-metal Cortex-M, not multiple RTOS tasks.
 */
bool telemetry_stm32_hal_poll(telemetry_stm32_hal_t *context);

#ifdef __cplusplus
}
#endif

#endif
