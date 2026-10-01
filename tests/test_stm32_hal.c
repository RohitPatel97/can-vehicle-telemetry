#include "telemetry_stm32_hal.h"

#include <stdio.h>
#include <string.h>

/* The real adapter is linked into this executable. Only HAL/CMSIS calls below
 * are simulated; no CAN transceiver, interrupt timing or UART hardware is used. */
static unsigned int tests_run;
static unsigned int failed;

#define EXPECT(condition) do { \
    if (!(condition)) { \
        (void)fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failed; \
    } \
} while (false)

static CAN_HandleTypeDef can_handle;
static UART_HandleTypeDef uart_handle;
static telemetry_stm32_hal_t context;
static uint32_t tick_ms;
static uint32_t primask;
static bool in_callback;
static HAL_StatusTypeDef rx_result;
static HAL_StatusTypeDef uart_result;
static CAN_RxHeaderTypeDef next_header;
static uint8_t next_frame[TELEMETRY_FRAME_SIZE];
static unsigned int rx_calls;
static unsigned int uart_calls;
static char uart_lines[128][64];
static void (*during_uart)(void);
static uint32_t refill_first_ms;

uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1U; }
void __DMB(void) { /* Functional double; not a memory-ordering model. */ }
void __set_PRIMASK(uint32_t value) { primask = value; }
uint32_t HAL_GetTick(void) { return tick_ms; }

HAL_StatusTypeDef HAL_CAN_GetRxMessage(CAN_HandleTypeDef *can, uint32_t fifo,
                                      CAN_RxHeaderTypeDef *header, uint8_t *data)
{
    EXPECT(can == &can_handle);
    EXPECT(fifo == CAN_RX_FIFO0);
    EXPECT(in_callback);
    ++rx_calls;
    if (rx_result == HAL_OK) {
        *header = next_header;
        (void)memcpy(data, next_frame, sizeof(next_frame));
    }
    return rx_result;
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                   uint16_t size, uint32_t timeout)
{
    EXPECT(uart == &uart_handle);
    EXPECT(!in_callback);
    EXPECT(primask == 0U);
    EXPECT(timeout == 10U);
    EXPECT(uart_calls < 128U);
    EXPECT(size < 64U);
    if (uart_calls < 128U && size < 64U) {
        (void)memcpy(uart_lines[uart_calls], data, size);
        uart_lines[uart_calls][size] = '\0';
    }
    ++uart_calls;
    if (during_uart != NULL) {
        during_uart();
    }
    return uart_result;
}

static void reset_fixture(uint32_t timeout_ms)
{
    telemetry_stm32_hal_init(&context, &can_handle, &uart_handle,
                             TELEMETRY_CAN_ID, timeout_ms);
    tick_ms = 0U;
    primask = 0U;
    in_callback = false;
    rx_result = HAL_OK;
    uart_result = HAL_OK;
    next_header = (CAN_RxHeaderTypeDef){0};
    next_header.StdId = TELEMETRY_CAN_ID;
    next_header.DLC = TELEMETRY_FRAME_SIZE;
    rx_calls = 0U;
    uart_calls = 0U;
    during_uart = NULL;
    refill_first_ms = 0U;
    (void)memset(uart_lines, 0, sizeof(uart_lines));
}

static void prepare_frame(uint8_t sequence, uint32_t now_ms)
{
    const telemetry_sample_t sample = {12345U, 92, 14320U};
    tick_ms = now_ms;
    EXPECT(telemetry_encode(&sample, sequence, TELEMETRY_ALLOWED_FLAGS,
                             next_frame) == TELEMETRY_OK);
}

static void receive_prepared(void)
{
    in_callback = true;
    telemetry_stm32_hal_on_fifo0(&context);
    in_callback = false;
}

static void receive(uint8_t sequence, uint32_t now_ms)
{
    prepare_frame(sequence, now_ms);
    receive_prepared();
}

static void test_isr_defers_work_and_preserves_irq_mask(void)
{
    reset_fixture(250U);
    primask = 1U;
    receive(42U, 10U);
    EXPECT(primask == 1U);
    EXPECT(context.queued == 1U);
    EXPECT(context.receiver.stats.accepted_frames == 0U);
    EXPECT(uart_calls == 0U);

    primask = 0U;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(primask == 0U);
    EXPECT(context.queued == 0U);
    EXPECT(context.receiver.stats.accepted_frames == 1U);
    EXPECT(context.receiver.last_accepted_ms == 10U);
    EXPECT(uart_calls == 1U);
    EXPECT(strcmp(uart_lines[0], "TEL,42,123.45,92,14320,3\r\n") == 0);
}

static void test_envelope_and_length_rejection(void)
{
    unsigned int length;
    reset_fixture(250U);
    next_header.StdId = TELEMETRY_CAN_ID + 1U;
    receive(0U, 0U);
    next_header.StdId = TELEMETRY_CAN_ID;
    next_header.IDE = CAN_ID_EXT;
    receive(0U, 0U);
    next_header.IDE = CAN_ID_STD;
    next_header.RTR = CAN_RTR_REMOTE;
    receive(0U, 0U);
    next_header.RTR = CAN_RTR_DATA;
    for (length = 0U; length <= 9U; ++length) {
        if (length != TELEMETRY_FRAME_SIZE) {
            next_header.DLC = length;
            receive(0U, 0U);
        }
    }
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.envelope_errors == 3U);
    EXPECT(context.receiver.stats.length_errors == 9U);
    EXPECT(!context.receiver.synchronized);
    EXPECT(uart_calls == 0U);
    next_header.DLC = TELEMETRY_FRAME_SIZE;
    receive(0U, 10U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(uart_calls == 1U);
}

static void test_corruption_and_sequence_are_never_forwarded(void)
{
    reset_fixture(250U);
    receive(10U, 0U);
    prepare_frame(11U, 20U);
    next_frame[1] ^= 1U;
    receive_prepared();
    receive(12U, 40U);
    receive(13U, 60U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.crc_errors == 1U);
    EXPECT(context.receiver.stats.sequence_errors == 1U);
    EXPECT(context.receiver.stats.accepted_frames == 2U);
    EXPECT(uart_calls == 2U);
    EXPECT(strcmp(uart_lines[0], "TEL,10,123.45,92,14320,3\r\n") == 0);
    EXPECT(strcmp(uart_lines[1], "TEL,13,123.45,92,14320,3\r\n") == 0);
}

static void test_full_queue_drops_newest_then_recovers(void)
{
    unsigned int index;
    reset_fixture(250U);
    for (index = 0U; index < TELEMETRY_STM32_QUEUE_CAPACITY + 2U; ++index) {
        receive((uint8_t)index, index);
    }
    EXPECT(context.queued == TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.pending_queue_overruns == 2U);
    EXPECT(context.queue_overruns == 0U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.queue_overruns == 2U);
    EXPECT(context.pending_queue_overruns == 0U);
    EXPECT(context.receiver.stats.accepted_frames == TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.stats.sequence_errors == 0U);
    EXPECT(context.receiver.last_sequence == TELEMETRY_STM32_QUEUE_CAPACITY - 1U);
    EXPECT(uart_calls == TELEMETRY_STM32_QUEUE_CAPACITY);
    receive((uint8_t)(TELEMETRY_STM32_QUEUE_CAPACITY + 2U), 40U);
    receive((uint8_t)(TELEMETRY_STM32_QUEUE_CAPACITY + 3U), 60U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.sequence_errors == 1U);
    EXPECT(uart_calls == TELEMETRY_STM32_QUEUE_CAPACITY + 1U);
}

static void refill_from_uart(void)
{
    receive((uint8_t)(TELEMETRY_STM32_QUEUE_CAPACITY + uart_calls - 1U), 0U);
}

static void test_poll_is_bounded_when_interrupts_refill_queue(void)
{
    unsigned int index;
    reset_fixture(250U);
    for (index = 0U; index < TELEMETRY_STM32_QUEUE_CAPACITY; ++index) {
        receive((uint8_t)index, 0U);
    }
    during_uart = refill_from_uart;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(uart_calls == TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.queued == TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.queue_overruns == 0U);
    during_uart = NULL;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.queued == 0U);
    EXPECT(uart_calls == 2U * TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.stats.sequence_errors == 0U);
}

static void queue_initial_batch(uint32_t received_ms)
{
    unsigned int index;
    for (index = 0U; index < TELEMETRY_STM32_QUEUE_CAPACITY; ++index) {
        receive((uint8_t)index, received_ms);
    }
}

static void refill_with_advancing_ticks(void)
{
    const uint32_t offset = 3U * (uint32_t)(uart_calls - 1U);
    receive((uint8_t)(TELEMETRY_STM32_QUEUE_CAPACITY + uart_calls - 1U),
            refill_first_ms + offset);
}

static void test_pending_continuous_traffic_defers_wall_clock_timeout(void)
{
    reset_fixture(32U);
    queue_initial_batch(0U);
    refill_first_ms = 3U;
    during_uart = refill_with_advancing_ticks;

    /* The budget ends at tick 48, but valid arrivals at 3..48 remain queued. */
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.queued == TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.last_accepted_ms == 0U);
    EXPECT(context.receiver.stats.timeout_events == 0U);
    EXPECT(!context.receiver.timed_out);
    EXPECT(uart_calls == TELEMETRY_STM32_QUEUE_CAPACITY);

    during_uart = NULL;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.queued == 0U);
    EXPECT(context.receiver.last_accepted_ms == 48U);
    EXPECT(context.receiver.stats.accepted_frames == 2U * TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.stats.sequence_errors == 0U);
    EXPECT(context.receiver.stats.timeout_events == 0U);
    EXPECT(uart_calls == 2U * TELEMETRY_STM32_QUEUE_CAPACITY);

    tick_ms = 80U;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    tick_ms = 81U;
    EXPECT(telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.timeout_events == 1U);
    EXPECT(context.receiver.timed_out);
}

static void test_real_gap_is_reported_when_pending_batch_is_processed(void)
{
    reset_fixture(32U);
    queue_initial_batch(0U);
    refill_first_ms = 40U;
    during_uart = refill_with_advancing_ticks;

    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.queued == TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.stats.timeout_events == 0U);

    /* Defer the wall clock, but preserve the actual 0 -> 40 ms receive gap. */
    during_uart = NULL;
    EXPECT(telemetry_stm32_hal_poll(&context));
    EXPECT(context.queued == 0U);
    EXPECT(context.receiver.last_accepted_ms == 85U);
    EXPECT(context.receiver.stats.accepted_frames == 2U * TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.stats.sequence_errors == 0U);
    EXPECT(context.receiver.stats.timeout_events == 1U);
    EXPECT(!context.receiver.timed_out);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.timeout_events == 1U);
}

static void test_pending_traffic_preserves_timeout_order_across_rollover(void)
{
    const uint32_t initial_ms = UINT32_MAX - 15U;
    reset_fixture(32U);
    queue_initial_batch(initial_ms);
    refill_first_ms = initial_ms + 3U;
    during_uart = refill_with_advancing_ticks;

    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.queued == TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.last_accepted_ms == initial_ms);
    EXPECT(context.receiver.stats.timeout_events == 0U);

    during_uart = NULL;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.queued == 0U);
    EXPECT(context.receiver.last_accepted_ms == 32U);
    EXPECT(context.receiver.stats.accepted_frames == 2U * TELEMETRY_STM32_QUEUE_CAPACITY);
    EXPECT(context.receiver.stats.sequence_errors == 0U);
    EXPECT(context.receiver.stats.timeout_events == 0U);
    EXPECT(!context.receiver.timed_out);

    tick_ms = 64U;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    tick_ms = 65U;
    EXPECT(telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.timeout_events == 1U);
}

static void test_hal_failure_accounting_is_deferred(void)
{
    reset_fixture(250U);
    rx_result = HAL_ERROR;
    receive(0U, 0U);
    primask = 1U;
    telemetry_stm32_hal_on_can_error(&context);
    EXPECT(primask == 1U);
    EXPECT(context.pending_can_errors == 2U);
    EXPECT(context.receiver.stats.can_errors == 0U);
    EXPECT(context.queued == 0U);
    primask = 0U;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.can_errors == 2U);
    EXPECT(context.pending_can_errors == 0U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.can_errors == 2U);
    EXPECT(uart_calls == 0U);
}

static void test_uart_failure_does_not_replay_accepted_data(void)
{
    reset_fixture(250U);
    uart_result = HAL_TIMEOUT;
    receive(0U, 0U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.uart_errors == 1U);
    EXPECT(context.receiver.stats.accepted_frames == 1U);
    EXPECT(context.queued == 0U);
    uart_result = HAL_OK;
    receive(1U, 20U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(uart_calls == 2U);
    EXPECT(context.uart_errors == 1U);
    EXPECT(context.receiver.stats.accepted_frames == 2U);
}

static void test_timeout_gap_survives_recovery_in_same_batch(void)
{
    reset_fixture(250U);
    receive(0U, 0U);
    receive(1U, 300U);
    EXPECT(telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.timeout_events == 1U);
    EXPECT(!context.receiver.timed_out);
    EXPECT(uart_calls == 2U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    tick_ms = 550U;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    tick_ms = 551U;
    EXPECT(telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.timeout_events == 2U);
    EXPECT(context.receiver.timed_out);
    tick_ms = 600U;
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.timeout_events == 2U);
}

static void test_receive_timestamp_drives_timeout_across_rollover(void)
{
    reset_fixture(32U);
    receive(0U, UINT32_MAX - 15U);
    tick_ms = 16U; /* 32 ms after arrival, even though polling started later. */
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.last_accepted_ms == UINT32_MAX - 15U);
    tick_ms = 17U;
    EXPECT(telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.timeout_events == 1U);
    receive(1U, 20U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(!context.receiver.timed_out);
    EXPECT(context.receiver.last_accepted_ms == 20U);
}

static void test_optional_uart_and_disabled_timeout(void)
{
    reset_fixture(0U);
    context.uart = NULL;
    receive(0U, 0U);
    receive(1U, 1000000U);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.accepted_frames == 2U);
    EXPECT(context.receiver.stats.timeout_events == 0U);
    EXPECT(context.uart_errors == 0U);
    EXPECT(uart_calls == 0U);
}

static void test_deferred_counters_saturate(void)
{
    unsigned int index;
    reset_fixture(250U);
    context.receiver.stats.can_errors = UINT32_MAX - 1U;
    context.pending_can_errors = UINT32_MAX;
    telemetry_stm32_hal_on_can_error(&context);
    EXPECT(context.pending_can_errors == UINT32_MAX);
    for (index = 0U; index < TELEMETRY_STM32_QUEUE_CAPACITY; ++index) {
        receive((uint8_t)index, 0U);
    }
    context.queue_overruns = UINT32_MAX - 1U;
    context.pending_queue_overruns = UINT32_MAX;
    receive(100U, 0U);
    EXPECT(context.pending_queue_overruns == UINT32_MAX);
    EXPECT(!telemetry_stm32_hal_poll(&context));
    EXPECT(context.receiver.stats.can_errors == UINT32_MAX);
    EXPECT(context.queue_overruns == UINT32_MAX);
}

static void test_null_contexts_do_not_call_hal(void)
{
    reset_fixture(250U);
    telemetry_stm32_hal_init(NULL, NULL, NULL, 0U, 0U);
    telemetry_stm32_hal_on_fifo0(NULL);
    telemetry_stm32_hal_on_can_error(NULL);
    EXPECT(!telemetry_stm32_hal_poll(NULL));
    context.can = NULL;
    telemetry_stm32_hal_on_fifo0(&context);
    EXPECT(rx_calls == 0U);
    EXPECT(uart_calls == 0U);
}

static void run_test(void (*test)(void), const char *name)
{
    const unsigned int before = failed;
    ++tests_run;
    test();
    (void)printf("%s %s\n", failed == before ? "PASS" : "FAIL", name);
}

int main(void)
{
    run_test(test_isr_defers_work_and_preserves_irq_mask, "ISR deferral and IRQ mask");
    run_test(test_envelope_and_length_rejection, "envelope and DLC rejection");
    run_test(test_corruption_and_sequence_are_never_forwarded, "accepted-only UART");
    run_test(test_full_queue_drops_newest_then_recovers, "queue overflow and recovery");
    run_test(test_poll_is_bounded_when_interrupts_refill_queue, "bounded poll with refill");
    run_test(test_pending_continuous_traffic_defers_wall_clock_timeout,
             "continuous backlog does not time out");
    run_test(test_real_gap_is_reported_when_pending_batch_is_processed,
             "real outage across bounded polls");
    run_test(test_pending_traffic_preserves_timeout_order_across_rollover,
             "backlog and tick rollover");
    run_test(test_hal_failure_accounting_is_deferred, "deferred CAN errors");
    run_test(test_uart_failure_does_not_replay_accepted_data, "UART failure policy");
    run_test(test_timeout_gap_survives_recovery_in_same_batch, "queued outage and recovery");
    run_test(test_receive_timestamp_drives_timeout_across_rollover, "receive tick and rollover");
    run_test(test_optional_uart_and_disabled_timeout, "optional UART and timeout");
    run_test(test_deferred_counters_saturate, "deferred counter saturation");
    run_test(test_null_contexts_do_not_call_hal, "null argument handling");
    (void)printf("%u tests, %u failed assertions\n", tests_run, failed);
    return failed == 0U ? 0 : 1;
}
