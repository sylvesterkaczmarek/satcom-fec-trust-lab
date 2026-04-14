#ifndef SATCOMFEC_REPLAY_PIPELINE_H
#define SATCOMFEC_REPLAY_PIPELINE_H

#include <string>
#include <vector>

#include "../dsp/framing.h"
#include "../dsp/front_end_dsp.h"
#include "../dsp/soft_demod.h"
#include "../fec/convolutional_codec.h"
#include "../trust/trust_features.h"
#include "../trust/trust_score.h"

namespace satcomfec {

enum class ReplayDecoder {
    kViterbiNeon,
    kViterbiSme2,
};

struct ReplayConfig {
    std::string iq_path;
    ReplayDecoder decoder = ReplayDecoder::kViterbiNeon;
    size_t samples_per_symbol = 8;
};

struct PreparedReplayFrame {
    bool ok = false;
    SoftBitBuffer frame_soft_bits;
    FrontEndStats front_end_stats;
    DemodStats demod_stats;
    FrameDescriptor frame;
    std::string error_message;
};

struct ReplayResult {
    bool ok = false;
    std::string iq_path;
    std::string decoder_name;
    std::string implementation_class;
    std::string implementation_summary;
    size_t samples_per_symbol = 0;
    size_t decoded_payload_bytes = 0;
    size_t expected_payload_bytes = 0;
    size_t frame_soft_bits = 0;
    std::string decoded_text;
    bool crc_ok = false;
    FrontEndStats front_end_stats;
    DemodStats demod_stats;
    FrameDescriptor frame;
    float trust_score = 0.0f;
    TrustFeatures trust_features;
    TrustScoreBreakdown trust_breakdown;
    TrustAssessment trust_assessment;
    std::string error_message;
};

const std::vector<uint8_t>& demo_sync_word();
size_t demo_coded_bits_per_frame();
PreparedReplayFrame prepare_demo_frame(const ReplayConfig& config);
ReplayResult run_demo_replay(const ReplayConfig& config);

}  // namespace satcomfec

#endif  // SATCOMFEC_REPLAY_PIPELINE_H
