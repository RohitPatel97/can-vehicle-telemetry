#ifndef TELEMETRY_RECEIVER_H
#define TELEMETRY_RECEIVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "telemetry/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct telemetry_receiver_stats {
    uint32_t accepted_frames;
    uint32_t crc_errors;
    uint32_t range_errors;
    uint32_t flag_errors;
    uint32_t length_errors;
    uint32_t sequence_errors;
    uint32_t timeout_events;
    uint32_t can_errors;
} telemetry_receiver_stats_t;

typedef struct telemetry_receiver {
    telemetry_receiver_stats_t stats;
    uint32_t last_accepted_ms;
    uint32_t timeout_ms;
    uint8_t last_sequence;
    bool synchronized;
    bool timed_out;
} telemetry_receiver_t;

/** Initialize receiver state. A timeout of zero disables timeout detection. */
void telemetry_receiver_init(telemetry_receiver_t *receiver,
                             uint32_t timeout_ms);

/**
 * Validate a frame and enforce monotonic modulo-256 sequencing.
 *
 * The first valid frame establishes synchronization. A sequence mismatch is
 * rejected but becomes the new sequence baseline so the following frame can
 * recover without resetting the receiver.
 */
telemetry_status_t telemetry_receiver_process(telemetry_receiver_t *receiver,
                                              const uint8_t *frame,
                                              size_t length,
                                              uint32_t now_ms,
                                              telemetry_decoded_t *decoded);

/**
 * Detect a transition into timeout. Uses unsigned subtraction, so the check is
 * safe across a single uint32_t millisecond counter rollover.
 */
bool telemetry_receiver_check_timeout(telemetry_receiver_t *receiver,
                                      uint32_t now_ms);

/** Record a controller/bus error reported by the platform CAN driver. */
telemetry_status_t telemetry_receiver_note_can_error(
    telemetry_receiver_t *receiver);

#ifdef __cplusplus
}
#endif

#endif

