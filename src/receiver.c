#include "telemetry/receiver.h"

#include <string.h>

void telemetry_receiver_init(telemetry_receiver_t *receiver,
                             uint32_t timeout_ms)
{
    if (receiver == NULL) {
        return;
    }

    (void)memset(receiver, 0, sizeof(*receiver));
    receiver->timeout_ms = timeout_ms;
}

static void count_decode_error(telemetry_receiver_t *receiver,
                               telemetry_status_t status)
{
    switch (status) {
    case TELEMETRY_ERR_CRC:
        ++receiver->stats.crc_errors;
        break;
    case TELEMETRY_ERR_RANGE:
        ++receiver->stats.range_errors;
        break;
    case TELEMETRY_ERR_FLAGS:
        ++receiver->stats.flag_errors;
        break;
    case TELEMETRY_ERR_LENGTH:
        ++receiver->stats.length_errors;
        break;
    default:
        break;
    }
}

telemetry_status_t telemetry_receiver_process(telemetry_receiver_t *receiver,
                                              const uint8_t *frame,
                                              size_t length,
                                              uint32_t now_ms,
                                              telemetry_decoded_t *decoded)
{
    telemetry_decoded_t candidate;
    telemetry_status_t status;

    if (receiver == NULL || decoded == NULL) {
        return TELEMETRY_ERR_NULL;
    }

    status = telemetry_decode(frame, length, &candidate);
    if (status != TELEMETRY_OK) {
        count_decode_error(receiver, status);
        return status;
    }

    if (receiver->synchronized) {
        const uint8_t expected = (uint8_t)(receiver->last_sequence + 1U);
        if (candidate.sequence != expected) {
            ++receiver->stats.sequence_errors;
            receiver->last_sequence = candidate.sequence;
            return TELEMETRY_ERR_SEQUENCE;
        }
    }

    receiver->synchronized = true;
    receiver->timed_out = false;
    receiver->last_sequence = candidate.sequence;
    receiver->last_accepted_ms = now_ms;
    ++receiver->stats.accepted_frames;
    *decoded = candidate;

    return TELEMETRY_OK;
}

bool telemetry_receiver_check_timeout(telemetry_receiver_t *receiver,
                                      uint32_t now_ms)
{
    bool expired;

    if (receiver == NULL || !receiver->synchronized ||
        receiver->timeout_ms == 0U) {
        return false;
    }

    expired = (uint32_t)(now_ms - receiver->last_accepted_ms) >
              receiver->timeout_ms;
    if (expired && !receiver->timed_out) {
        receiver->timed_out = true;
        ++receiver->stats.timeout_events;
    }

    return expired;
}

telemetry_status_t telemetry_receiver_note_can_error(
    telemetry_receiver_t *receiver)
{
    if (receiver == NULL) {
        return TELEMETRY_ERR_NULL;
    }

    ++receiver->stats.can_errors;
    return TELEMETRY_ERR_CAN;
}
