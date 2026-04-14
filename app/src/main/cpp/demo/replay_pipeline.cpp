#include "replay_pipeline.h"

#include <algorithm>

#include "../dsp/framing.h"
#include "../dsp/front_end_dsp.h"
#include "../dsp/soft_demod.h"
#include "../fec/convolutional_codec.h"
#include "../fec/viterbi_decoder_neon.h"
#include "../fec/viterbi_decoder_sme2.h"
#include "../trust/trust_score.h"
#include "../util/iq_reader.h"
#include "../util/logging.h"

namespace satcomfec {

namespace {

const std::vector<uint8_t> kDemoSyncWord {
    1, 1, 0, 1, 0, 0, 1, 1,
    1, 0, 1, 0, 0, 1, 0, 1,
};

constexpr size_t kDemoPayloadBytes = 14;
constexpr size_t kDemoCodedBitsPerFrame =
    2 * ((kDemoPayloadBytes + 1) * 8 + (kConvConstraintLength - 1));

const ImplementationInfo& decoder_info(ReplayDecoder decoder) {
    switch (decoder) {
        case ReplayDecoder::kViterbiNeon:
            return viterbi_neon_implementation_info();
        case ReplayDecoder::kViterbiSme2:
            return viterbi_sme2_implementation_info();
    }

    return viterbi_sme2_implementation_info();
}

bool dispatch_decoder(ReplayDecoder decoder,
                      const SoftBitBuffer& frame_soft_bits,
                      std::vector<uint8_t>& decoded_bits) {
    switch (decoder) {
        case ReplayDecoder::kViterbiNeon:
            return viterbi_decode_neon(frame_soft_bits, decoded_bits);
        case ReplayDecoder::kViterbiSme2:
            return viterbi_decode_sme2(frame_soft_bits, decoded_bits);
    }

    return false;
}

}  // namespace

const std::vector<uint8_t>& demo_sync_word() {
    return kDemoSyncWord;
}

size_t demo_coded_bits_per_frame() {
    return kDemoCodedBitsPerFrame;
}

PreparedReplayFrame prepare_demo_frame(const ReplayConfig& config) {
    PreparedReplayFrame prepared;
    std::vector<ComplexF> raw_iq;
    if (!load_iq_from_file(config.iq_path, raw_iq)) {
        prepared.error_message = "Failed to load IQ file";
        return prepared;
    }

    std::vector<ComplexF> front_end_iq;
    if (!run_front_end(raw_iq, front_end_iq, FrontEndConfig {})) {
        prepared.error_message = "Front-end processing failed";
        return prepared;
    }

    SoftBitBuffer soft_bits;
    if (!soft_demodulate_bpsk(front_end_iq, soft_bits,
                              DemodConfig {config.samples_per_symbol})) {
        prepared.error_message = "Soft demodulation failed";
        return prepared;
    }

    std::vector<FrameDescriptor> frames;
    if (!find_frames(soft_bits,
                     frames,
                     FramingConfig {
                         demo_sync_word(),
                         demo_coded_bits_per_frame(),
                         static_cast<int>(demo_sync_word().size() - 2),
                     })) {
        prepared.error_message = "Frame sync failed";
        return prepared;
    }

    const FrameDescriptor& frame = frames.front();
    const auto frame_begin = soft_bits.begin() + static_cast<long>(frame.start_index);
    const auto frame_end =
        frame_begin + static_cast<long>(frame.length);
    prepared.frame_soft_bits.assign(frame_begin, frame_end);
    prepared.sync_score = frame.correlation_score;
    prepared.ok = true;
    return prepared;
}

ReplayResult run_demo_replay(const ReplayConfig& config) {
    ReplayResult result;
    const ImplementationInfo& info = decoder_info(config.decoder);
    result.decoder_name = info.path_name;
    result.implementation_class = implementation_class_label(info.implementation_class);
    result.implementation_summary = info.summary;

    const PreparedReplayFrame prepared = prepare_demo_frame(config);
    if (!prepared.ok) {
        result.error_message = prepared.error_message;
        return result;
    }

    std::vector<uint8_t> decoded_bits;
    if (!dispatch_decoder(config.decoder,
                          prepared.frame_soft_bits,
                          decoded_bits)) {
        result.error_message = "Decoder failed";
        return result;
    }

    const std::vector<uint8_t> decoded_bytes = bits_to_bytes(decoded_bits);
    if (decoded_bytes.size() != kDemoPayloadBytes + 1) {
        result.error_message = "Decoded payload length did not match the canned frame";
        return result;
    }

    const std::vector<uint8_t> payload(decoded_bytes.begin(),
                                       decoded_bytes.end() - 1);
    const uint8_t observed_crc = decoded_bytes.back();
    const uint8_t expected_crc = crc8_bytes(payload);

    result.decoded_text = bytes_to_ascii(payload);
    result.crc_ok = (observed_crc == expected_crc);
    result.sync_score = prepared.sync_score;
    result.trust_features = compute_trust_features(
        prepared.frame_soft_bits,
        prepared.sync_score,
        static_cast<int>(demo_sync_word().size()),
        result.crc_ok);
    result.trust_score = compute_trust_score(result.trust_features);
    result.ok = result.crc_ok;
    if (!result.crc_ok) {
        result.error_message = "CRC mismatch";
    }

    log_info("run_demo_replay: replay complete");
    return result;
}

}  // namespace satcomfec
