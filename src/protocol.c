#include "telemetry/protocol.h"

#include <stdbool.h>

enum {
    OFFSET_SEQUENCE = 0,
    OFFSET_SPEED_LO = 1,
    OFFSET_SPEED_HI = 2,
    OFFSET_COOLANT = 3,
    OFFSET_BATTERY_LO = 4,
    OFFSET_BATTERY_HI = 5,
    OFFSET_FLAGS = 6,
    OFFSET_CRC = 7,
    COOLANT_OFFSET_C = 40
};

static bool sample_is_in_range(const telemetry_sample_t *sample)
{
    return sample->speed_centi_kph <= TELEMETRY_MAX_SPEED_CENTI_KPH &&
           sample->coolant_c >= TELEMETRY_MIN_COOLANT_C &&
           sample->coolant_c <= TELEMETRY_MAX_COOLANT_C &&
           sample->battery_mv >= TELEMETRY_MIN_BATTERY_MV &&
           sample->battery_mv <= TELEMETRY_MAX_BATTERY_MV;
}

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8U));
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)(value >> 8U);
}

uint8_t telemetry_crc8(const uint8_t *data, size_t length)
{
    uint8_t crc = 0xFFU;
    size_t index;

    if (data == NULL) {
        return 0U;
    }

    for (index = 0U; index < length; ++index) {
        uint8_t bit;
        crc ^= data[index];
        for (bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x80U) != 0U) {
                crc = (uint8_t)(((uint32_t)crc << 1U) ^ 0x1DU);
            } else {
                crc = (uint8_t)(crc << 1U);
            }
        }
    }

    return (uint8_t)(crc ^ 0xFFU);
}

telemetry_status_t telemetry_encode(const telemetry_sample_t *sample,
                                    uint8_t sequence,
                                    uint8_t flags,
                                    uint8_t frame[TELEMETRY_FRAME_SIZE])
{
    if (sample == NULL || frame == NULL) {
        return TELEMETRY_ERR_NULL;
    }
    if (!sample_is_in_range(sample)) {
        return TELEMETRY_ERR_RANGE;
    }
    if ((flags & (uint8_t)(~TELEMETRY_ALLOWED_FLAGS)) != 0U) {
        return TELEMETRY_ERR_FLAGS;
    }

    frame[OFFSET_SEQUENCE] = sequence;
    write_u16_le(&frame[OFFSET_SPEED_LO], sample->speed_centi_kph);
    frame[OFFSET_COOLANT] = (uint8_t)(sample->coolant_c + COOLANT_OFFSET_C);
    write_u16_le(&frame[OFFSET_BATTERY_LO], sample->battery_mv);
    frame[OFFSET_FLAGS] = flags;
    frame[OFFSET_CRC] = telemetry_crc8(frame, OFFSET_CRC);

    return TELEMETRY_OK;
}

telemetry_status_t telemetry_decode(const uint8_t *frame,
                                    size_t length,
                                    telemetry_decoded_t *decoded)
{
    telemetry_decoded_t candidate;

    if (frame == NULL || decoded == NULL) {
        return TELEMETRY_ERR_NULL;
    }
    if (length != TELEMETRY_FRAME_SIZE) {
        return TELEMETRY_ERR_LENGTH;
    }
    if (telemetry_crc8(frame, OFFSET_CRC) != frame[OFFSET_CRC]) {
        return TELEMETRY_ERR_CRC;
    }
    if ((frame[OFFSET_FLAGS] & (uint8_t)(~TELEMETRY_ALLOWED_FLAGS)) != 0U) {
        return TELEMETRY_ERR_FLAGS;
    }

    candidate.sequence = frame[OFFSET_SEQUENCE];
    candidate.sample.speed_centi_kph = read_u16_le(&frame[OFFSET_SPEED_LO]);
    candidate.sample.coolant_c =
        (int16_t)((int16_t)frame[OFFSET_COOLANT] - COOLANT_OFFSET_C);
    candidate.sample.battery_mv = read_u16_le(&frame[OFFSET_BATTERY_LO]);
    candidate.flags = frame[OFFSET_FLAGS];

    if (!sample_is_in_range(&candidate.sample)) {
        return TELEMETRY_ERR_RANGE;
    }

    *decoded = candidate;
    return TELEMETRY_OK;
}

const char *telemetry_status_string(telemetry_status_t status)
{
    switch (status) {
    case TELEMETRY_OK:
        return "ok";
    case TELEMETRY_ERR_NULL:
        return "null argument";
    case TELEMETRY_ERR_LENGTH:
        return "invalid length";
    case TELEMETRY_ERR_CRC:
        return "crc mismatch";
    case TELEMETRY_ERR_RANGE:
        return "value out of range";
    case TELEMETRY_ERR_FLAGS:
        return "reserved flag set";
    case TELEMETRY_ERR_SEQUENCE:
        return "sequence mismatch";
    case TELEMETRY_ERR_TIMEOUT:
        return "receive timeout";
    case TELEMETRY_ERR_CAN:
        return "can controller error";
    default:
        return "unknown status";
    }
}
