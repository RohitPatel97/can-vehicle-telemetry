#ifndef TELEMETRY_PROTOCOL_H
#define TELEMETRY_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    TELEMETRY_CAN_ID = 0x321,
    TELEMETRY_FRAME_SIZE = 8,
    TELEMETRY_CAN_BITRATE = 500000,
    TELEMETRY_MAX_SPEED_CENTI_KPH = 30000,
    TELEMETRY_MIN_COOLANT_C = -40,
    TELEMETRY_MAX_COOLANT_C = 150,
    TELEMETRY_MIN_BATTERY_MV = 6000,
    TELEMETRY_MAX_BATTERY_MV = 18000,
    TELEMETRY_FLAG_ENGINE_RUNNING = 0x01,
    TELEMETRY_FLAG_CHARGING = 0x02,
    TELEMETRY_ALLOWED_FLAGS = 0x03
};

typedef enum telemetry_status {
    TELEMETRY_OK = 0,
    TELEMETRY_ERR_NULL,
    TELEMETRY_ERR_LENGTH,
    TELEMETRY_ERR_CRC,
    TELEMETRY_ERR_RANGE,
    TELEMETRY_ERR_FLAGS,
    TELEMETRY_ERR_SEQUENCE,
    TELEMETRY_ERR_TIMEOUT,
    TELEMETRY_ERR_CAN
} telemetry_status_t;

typedef struct telemetry_sample {
    uint16_t speed_centi_kph;
    int16_t coolant_c;
    uint16_t battery_mv;
} telemetry_sample_t;

typedef struct telemetry_decoded {
    telemetry_sample_t sample;
    uint8_t sequence;
    uint8_t flags;
} telemetry_decoded_t;

/**
 * Compute CRC-8/SAE-J1850 (poly 0x1D, init 0xFF, xorout 0xFF,
 * no reflection) over an arbitrary byte sequence.
 */
uint8_t telemetry_crc8(const uint8_t *data, size_t length);

/** Encode one sample into the fixed eight-byte CAN payload. */
telemetry_status_t telemetry_encode(const telemetry_sample_t *sample,
                                    uint8_t sequence,
                                    uint8_t flags,
                                    uint8_t frame[TELEMETRY_FRAME_SIZE]);

/** Decode and validate one fixed-width CAN payload. */
telemetry_status_t telemetry_decode(const uint8_t *frame,
                                    size_t length,
                                    telemetry_decoded_t *decoded);

/** Human-readable stable label for logging and test output. */
const char *telemetry_status_string(telemetry_status_t status);

#ifdef __cplusplus
}
#endif

#endif

