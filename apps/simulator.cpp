#include "telemetry/protocol.h"
#include "telemetry/receiver.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

enum class Fault {
    none,
    crc,
    range,
    sequence,
    timeout,
    can_bus
};

struct Options {
    std::uint32_t frames = 8U;
    std::uint32_t fault_at = 3U;
    Fault fault = Fault::none;
};

void print_usage(const char *program)
{
    std::cout
        << "Usage: " << program
        << " [--frames N] [--fault none|crc|range|sequence|timeout|can]"
           " [--fault-at N]\n";
}

std::uint32_t parse_u32(std::string_view value, std::string_view option)
{
    std::size_t consumed = 0U;
    unsigned long parsed = 0UL;

    try {
        parsed = std::stoul(std::string(value), &consumed, 10);
    } catch (const std::exception &) {
        throw std::invalid_argument(std::string(option) + " expects an integer");
    }

    if (consumed != value.size() || parsed > 1000000UL) {
        throw std::invalid_argument(std::string(option) + " is outside 0..1000000");
    }
    return static_cast<std::uint32_t>(parsed);
}

Fault parse_fault(std::string_view name)
{
    if (name == "none") {
        return Fault::none;
    }
    if (name == "crc") {
        return Fault::crc;
    }
    if (name == "range") {
        return Fault::range;
    }
    if (name == "sequence") {
        return Fault::sequence;
    }
    if (name == "timeout") {
        return Fault::timeout;
    }
    if (name == "can") {
        return Fault::can_bus;
    }
    throw std::invalid_argument("unknown fault: " + std::string(name));
}

Options parse_options(int argc, char **argv)
{
    Options options;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_usage(argv[0]);
            std::exit(EXIT_SUCCESS);
        }
        if (index + 1 >= argc) {
            throw std::invalid_argument("missing value for " + std::string(argument));
        }

        const std::string_view value(argv[++index]);
        if (argument == "--frames") {
            options.frames = parse_u32(value, argument);
        } else if (argument == "--fault-at") {
            options.fault_at = parse_u32(value, argument);
        } else if (argument == "--fault") {
            options.fault = parse_fault(value);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }
    return options;
}

void print_frame(const std::array<std::uint8_t, TELEMETRY_FRAME_SIZE> &frame)
{
    std::ios_base::fmtflags previous_flags = std::cout.flags();
    const char previous_fill = std::cout.fill();

    for (std::uint8_t byte : frame) {
        std::cout << std::hex << std::uppercase << std::setfill('0')
                  << std::setw(2) << static_cast<unsigned int>(byte) << ' ';
    }

    std::cout.flags(previous_flags);
    std::cout.fill(previous_fill);
}

void inject_payload_fault(Fault fault,
                          std::array<std::uint8_t, TELEMETRY_FRAME_SIZE> &frame)
{
    if (fault == Fault::crc) {
        frame[1] ^= 0x01U;
    } else if (fault == Fault::range) {
        frame[1] = 0xFFU;
        frame[2] = 0xFFU;
        frame[7] = telemetry_crc8(frame.data(), 7U);
    }
}

void print_uart_line(const telemetry_decoded_t &decoded)
{
    const unsigned int whole_speed = decoded.sample.speed_centi_kph / 100U;
    const unsigned int fractional_speed = decoded.sample.speed_centi_kph % 100U;

    std::cout << "UART> TEL," << static_cast<unsigned int>(decoded.sequence)
              << ',' << whole_speed << '.' << std::setfill('0') << std::setw(2)
              << fractional_speed << ',' << std::setfill(' ') << decoded.sample.coolant_c
              << ',' << decoded.sample.battery_mv << ','
              << static_cast<unsigned int>(decoded.flags) << "\r\n";
}

int run(const Options &options)
{
    constexpr std::uint32_t timeout_ms = 250U;
    telemetry_receiver_t receiver{};
    std::uint8_t sequence = 0U;
    std::uint32_t now_ms = 0U;

    telemetry_receiver_init(&receiver, timeout_ms);

    std::cout << "CAN telemetry simulation: id=0x321 bitrate=500000 frame_bytes=8\n";
    for (std::uint32_t index = 0U; index < options.frames; ++index) {
        std::array<std::uint8_t, TELEMETRY_FRAME_SIZE> frame{};
        telemetry_decoded_t decoded{};
        telemetry_sample_t sample{};

        if (index == options.fault_at && options.fault == Fault::sequence) {
            sequence = static_cast<std::uint8_t>(sequence + 1U);
        }

        sample.speed_centi_kph =
            static_cast<std::uint16_t>(4200U + (index % 100U) * 37U);
        sample.coolant_c = static_cast<std::int16_t>(82 + static_cast<std::int32_t>(index % 8U));
        sample.battery_mv =
            static_cast<std::uint16_t>(13800U + (index % 100U) * 5U);

        const telemetry_status_t encode_status = telemetry_encode(
            &sample,
            sequence,
            static_cast<std::uint8_t>(TELEMETRY_FLAG_ENGINE_RUNNING |
                                      TELEMETRY_FLAG_CHARGING),
            frame.data());
        if (encode_status != TELEMETRY_OK) {
            std::cerr << "encoder stopped: " << telemetry_status_string(encode_status)
                      << '\n';
            return EXIT_FAILURE;
        }

        if (index == options.fault_at) {
            inject_payload_fault(options.fault, frame);
            if (options.fault == Fault::timeout) {
                now_ms += timeout_ms + 1U;
                if (telemetry_receiver_check_timeout(&receiver, now_ms)) {
                    std::cout << "EVENT timeout elapsed_ms=" << timeout_ms + 1U << '\n';
                }
            } else if (options.fault == Fault::can_bus) {
                const telemetry_status_t can_status =
                    telemetry_receiver_note_can_error(&receiver);
                std::cout << "EVENT " << telemetry_status_string(can_status) << '\n';
                std::cout << "RX[" << index
                          << "] controller did not deliver a payload\n";
                sequence = static_cast<std::uint8_t>(sequence + 1U);
                now_ms += 20U;
                continue;
            }
        }

        std::cout << "RX[" << index << "] ";
        print_frame(frame);

        const telemetry_status_t status = telemetry_receiver_process(
            &receiver, frame.data(), frame.size(), now_ms, &decoded);
        std::cout << "-> " << telemetry_status_string(status) << '\n';
        if (status == TELEMETRY_OK) {
            print_uart_line(decoded);
        }

        sequence = static_cast<std::uint8_t>(sequence + 1U);
        now_ms += 20U;
    }

    std::cout << "SUMMARY accepted=" << receiver.stats.accepted_frames
              << " crc=" << receiver.stats.crc_errors
              << " range=" << receiver.stats.range_errors
              << " sequence=" << receiver.stats.sequence_errors
              << " timeout=" << receiver.stats.timeout_events
              << " can=" << receiver.stats.can_errors << '\n';
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char **argv)
{
    try {
        return run(parse_options(argc, argv));
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << '\n';
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }
}
