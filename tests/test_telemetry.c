#include "telemetry/protocol.h"
#include "telemetry/receiver.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static unsigned int tests_run = 0U;
static unsigned int assertions_failed = 0U;

#define EXPECT_TRUE(condition)                                                   \
    do {                                                                         \
        if (!(condition)) {                                                      \
            (void)fprintf(stderr, "%s:%d: expected %s\n", __FILE__, __LINE__,  \
                          #condition);                                            \
            ++assertions_failed;                                                 \
        }                                                                        \
    } while (false)

#define EXPECT_EQ(expected, actual)                                               \
    do {                                                                          \
        const unsigned long long expected_value =                                 \
            (unsigned long long)(expected);                                        \
        const unsigned long long actual_value =                                   \
            (unsigned long long)(actual);                                          \
        if (expected_value != actual_value) {                                      \
            (void)fprintf(stderr, "%s:%d: expected %llu, got %llu\n", __FILE__, \
                          __LINE__, expected_value, actual_value);                 \
            ++assertions_failed;                                                  \
        }                                                                         \
    } while (false)

static telemetry_sample_t nominal_sample(void)
{
    telemetry_sample_t sample;
    sample.speed_centi_kph = 12345U;
    sample.coolant_c = 92;
    sample.battery_mv = 14320U;
    return sample;
}

static void run_test(void (*test)(void), const char *name)
{
    const unsigned int failures_before = assertions_failed;
    ++tests_run;
    test();
    (void)printf("%s %s\n",
                 failures_before == assertions_failed ? "PASS" : "FAIL",
                 name);
}

static void test_crc_standard_check_vector(void)
{
    static const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(0x4BU, telemetry_crc8(input, sizeof(input)));
}

static void test_round_trip_and_wire_order(void)
{
    const telemetry_sample_t sample = nominal_sample();
    uint8_t frame[TELEMETRY_FRAME_SIZE] = {0U};
    telemetry_decoded_t decoded;

    EXPECT_EQ(TELEMETRY_OK,
              telemetry_encode(&sample, 42U, TELEMETRY_ALLOWED_FLAGS, frame));
    EXPECT_EQ(42U, frame[0]);
    EXPECT_EQ(0x39U, frame[1]);
    EXPECT_EQ(0x30U, frame[2]);
    EXPECT_EQ(132U, frame[3]);
    EXPECT_EQ(0xF0U, frame[4]);
    EXPECT_EQ(0x37U, frame[5]);
    EXPECT_EQ(3U, frame[6]);
    EXPECT_EQ(0xBEU, frame[7]);

    EXPECT_EQ(TELEMETRY_OK,
              telemetry_decode(frame, sizeof(frame), &decoded));
    EXPECT_EQ(42U, decoded.sequence);
    EXPECT_EQ(sample.speed_centi_kph, decoded.sample.speed_centi_kph);
    EXPECT_EQ(sample.coolant_c, decoded.sample.coolant_c);
    EXPECT_EQ(sample.battery_mv, decoded.sample.battery_mv);
    EXPECT_EQ(TELEMETRY_ALLOWED_FLAGS, decoded.flags);
}

static void test_boundary_values(void)
{
    telemetry_sample_t sample = {
        TELEMETRY_MAX_SPEED_CENTI_KPH,
        TELEMETRY_MIN_COOLANT_C,
        TELEMETRY_MIN_BATTERY_MV
    };
    uint8_t frame[TELEMETRY_FRAME_SIZE];
    telemetry_decoded_t decoded;

    EXPECT_EQ(TELEMETRY_OK, telemetry_encode(&sample, 0U, 0U, frame));
    EXPECT_EQ(TELEMETRY_OK, telemetry_decode(frame, sizeof(frame), &decoded));

    sample.speed_centi_kph = 0U;
    sample.coolant_c = TELEMETRY_MAX_COOLANT_C;
    sample.battery_mv = TELEMETRY_MAX_BATTERY_MV;
    EXPECT_EQ(TELEMETRY_OK, telemetry_encode(&sample, 255U, 0U, frame));
    EXPECT_EQ(TELEMETRY_OK, telemetry_decode(frame, sizeof(frame), &decoded));
}

static void test_encoder_rejects_invalid_input(void)
{
    telemetry_sample_t sample = nominal_sample();
    uint8_t frame[TELEMETRY_FRAME_SIZE];

    sample.speed_centi_kph = (uint16_t)(TELEMETRY_MAX_SPEED_CENTI_KPH + 1);
    EXPECT_EQ(TELEMETRY_ERR_RANGE,
              telemetry_encode(&sample, 0U, 0U, frame));

    sample = nominal_sample();
    sample.coolant_c = (int16_t)(TELEMETRY_MIN_COOLANT_C - 1);
    EXPECT_EQ(TELEMETRY_ERR_RANGE,
              telemetry_encode(&sample, 0U, 0U, frame));

    sample = nominal_sample();
    sample.battery_mv = (uint16_t)(TELEMETRY_MIN_BATTERY_MV - 1);
    EXPECT_EQ(TELEMETRY_ERR_RANGE,
              telemetry_encode(&sample, 0U, 0U, frame));

    sample = nominal_sample();
    EXPECT_EQ(TELEMETRY_ERR_FLAGS,
              telemetry_encode(&sample, 0U, 0x80U, frame));
    EXPECT_EQ(TELEMETRY_ERR_NULL, telemetry_encode(NULL, 0U, 0U, frame));
    EXPECT_EQ(TELEMETRY_ERR_NULL, telemetry_encode(&sample, 0U, 0U, NULL));
}

static void test_decoder_fault_paths(void)
{
    const telemetry_sample_t sample = nominal_sample();
    uint8_t frame[TELEMETRY_FRAME_SIZE];
    telemetry_decoded_t decoded;

    EXPECT_EQ(TELEMETRY_OK, telemetry_encode(&sample, 7U, 0U, frame));
    EXPECT_EQ(TELEMETRY_ERR_LENGTH,
              telemetry_decode(frame, TELEMETRY_FRAME_SIZE - 1U, &decoded));

    frame[1] ^= 0x01U;
    EXPECT_EQ(TELEMETRY_ERR_CRC,
              telemetry_decode(frame, sizeof(frame), &decoded));
    frame[1] ^= 0x01U;

    frame[1] = 0xFFU;
    frame[2] = 0xFFU;
    frame[7] = telemetry_crc8(frame, 7U);
    EXPECT_EQ(TELEMETRY_ERR_RANGE,
              telemetry_decode(frame, sizeof(frame), &decoded));

    EXPECT_EQ(TELEMETRY_OK, telemetry_encode(&sample, 7U, 0U, frame));
    frame[6] = 0x80U;
    frame[7] = telemetry_crc8(frame, 7U);
    EXPECT_EQ(TELEMETRY_ERR_FLAGS,
              telemetry_decode(frame, sizeof(frame), &decoded));
    EXPECT_EQ(TELEMETRY_ERR_NULL,
              telemetry_decode(NULL, sizeof(frame), &decoded));
}

static void encode_sequence(uint8_t sequence,
                            uint8_t frame[TELEMETRY_FRAME_SIZE])
{
    const telemetry_sample_t sample = nominal_sample();
    EXPECT_EQ(TELEMETRY_OK, telemetry_encode(&sample, sequence, 0U, frame));
}

static void test_receiver_sequence_wrap_and_recovery(void)
{
    telemetry_receiver_t receiver;
    telemetry_decoded_t decoded;
    uint8_t frame[TELEMETRY_FRAME_SIZE];

    telemetry_receiver_init(&receiver, 100U);
    encode_sequence(254U, frame);
    EXPECT_EQ(TELEMETRY_OK,
              telemetry_receiver_process(&receiver, frame, sizeof(frame), 10U,
                                         &decoded));
    encode_sequence(255U, frame);
    EXPECT_EQ(TELEMETRY_OK,
              telemetry_receiver_process(&receiver, frame, sizeof(frame), 20U,
                                         &decoded));
    encode_sequence(0U, frame);
    EXPECT_EQ(TELEMETRY_OK,
              telemetry_receiver_process(&receiver, frame, sizeof(frame), 30U,
                                         &decoded));

    encode_sequence(2U, frame);
    EXPECT_EQ(TELEMETRY_ERR_SEQUENCE,
              telemetry_receiver_process(&receiver, frame, sizeof(frame), 40U,
                                         &decoded));
    encode_sequence(3U, frame);
    EXPECT_EQ(TELEMETRY_OK,
              telemetry_receiver_process(&receiver, frame, sizeof(frame), 50U,
                                         &decoded));

    EXPECT_EQ(4U, receiver.stats.accepted_frames);
    EXPECT_EQ(1U, receiver.stats.sequence_errors);
}

static void test_receiver_error_counters(void)
{
    telemetry_receiver_t receiver;
    telemetry_decoded_t decoded;
    uint8_t frame[TELEMETRY_FRAME_SIZE];

    telemetry_receiver_init(&receiver, 100U);
    encode_sequence(1U, frame);
    frame[0] ^= 0x80U;
    EXPECT_EQ(TELEMETRY_ERR_CRC,
              telemetry_receiver_process(&receiver, frame, sizeof(frame), 0U,
                                         &decoded));
    EXPECT_EQ(1U, receiver.stats.crc_errors);
    EXPECT_EQ(TELEMETRY_ERR_CAN,
              telemetry_receiver_note_can_error(&receiver));
    EXPECT_EQ(1U, receiver.stats.can_errors);
}

static void test_timeout_transition_and_rollover(void)
{
    telemetry_receiver_t receiver;
    telemetry_decoded_t decoded;
    uint8_t frame[TELEMETRY_FRAME_SIZE];

    telemetry_receiver_init(&receiver, 32U);
    EXPECT_TRUE(!telemetry_receiver_check_timeout(&receiver, 1000U));

    encode_sequence(1U, frame);
    EXPECT_EQ(TELEMETRY_OK,
              telemetry_receiver_process(&receiver, frame, sizeof(frame),
                                         UINT32_MAX - 15U, &decoded));
    EXPECT_TRUE(!telemetry_receiver_check_timeout(&receiver, 15U));
    EXPECT_TRUE(telemetry_receiver_check_timeout(&receiver, 17U));
    EXPECT_TRUE(telemetry_receiver_check_timeout(&receiver, 18U));
    EXPECT_EQ(1U, receiver.stats.timeout_events);

    encode_sequence(2U, frame);
    EXPECT_EQ(TELEMETRY_OK,
              telemetry_receiver_process(&receiver, frame, sizeof(frame), 20U,
                                         &decoded));
    EXPECT_TRUE(!receiver.timed_out);
}

int main(void)
{
    run_test(test_crc_standard_check_vector, "crc standard check vector");
    run_test(test_round_trip_and_wire_order, "round trip and byte order");
    run_test(test_boundary_values, "boundary values");
    run_test(test_encoder_rejects_invalid_input, "encoder rejects invalid input");
    run_test(test_decoder_fault_paths, "decoder fault paths");
    run_test(test_receiver_sequence_wrap_and_recovery,
             "receiver sequence wrap and recovery");
    run_test(test_receiver_error_counters, "receiver error counters");
    run_test(test_timeout_transition_and_rollover,
             "timeout transition and timer rollover");

    (void)printf("%u tests, %u failed assertions\n",
                 tests_run, assertions_failed);
    return assertions_failed == 0U ? 0 : 1;
}
