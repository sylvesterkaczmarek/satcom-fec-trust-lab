#include <cmath>
#include <iostream>

#include "demo/replay_acquisition.h"
#include "dsp/soft_demod.h"
#include "trust/trust_score.h"

int main() {
    using namespace satcomfec;

    const std::vector<ComplexF> iq {{0.62f, 0}, {-0.62f, 0}, {0.31f, 0}};
    SoftBitBuffer bits;
    DemodStats demod;
    if (!soft_demodulate_bpsk(iq, bits, {1}, &demod) || demod.clipped_symbol_count ||
        bits.front() != 127 || bits[1] != -127) {
        std::cerr << "Adaptive scaling must preserve signs and leave clipping unmeasured\n";
        return 1;
    }
    if (!soft_demodulate_bpsk(iq, bits, {1, 0.62f}, &demod) || demod.clipped_symbol_count != 0) {
        std::cerr << "Symbols exactly at fixed full scale must not count as clipped\n";
        return 1;
    }
    if (!soft_demodulate_bpsk(iq, bits, {1, 0.4f}, &demod) || demod.clipped_symbol_count != 2) {
        std::cerr << "Fixed full scale should clip only the first two symbols\n";
        return 1;
    }

    FrameDescriptor frame;
    frame.length = bits.size();
    frame.correlation_score = 16;
    const ReplayAcquisitionDiagnostics acquisition;
    auto features = compute_trust_features(bits, frame, 16, demod, acquisition, false, false);
    if (features.frame_sync_margin_measured || !features.clipping_measured ||
        std::abs(features.clipped_symbol_fraction - 2.0f / 3.0f) > 1e-6f) {
        std::cerr << "Single-offset sync has no margin, fixed-scale demod has a clipping fraction\n";
        return 1;
    }
    frame.has_second_best_correlation = true;
    frame.second_best_correlation_score = 14;
    features = compute_trust_features(bits, frame, 16, demod, acquisition, false, false);
    const auto assessment = assess_trust(features, compute_trust_score_breakdown(features));
    if (!features.frame_sync_margin_measured || features.normalized_frame_sync_margin != 0.125f ||
        !assessment.ambiguous_frame_sync || !assessment.demod_clipping) {
        std::cerr << "A measured narrow sync margin and quantiser clipping should both be flagged\n";
        return 1;
    }

    TrustFeatures partial;
    partial.acquisition_accepted = 1.0f;
    partial.frame_sync_measured = true;
    partial.normalized_frame_sync_score = 0.5f;
    if (std::abs(compute_trust_score(partial) - 0.5f) > 1e-6f) {
        std::cerr << "Missing components must contribute no score or weight\n";
        return 1;
    }
    partial.frame_sync_margin_measured = true;
    partial.normalized_frame_sync_margin = 0.0f;
    if (std::abs(compute_trust_score(partial) - 0.25f) > 1e-6f) {
        std::cerr << "A measured zero margin must contribute its weight\n";
        return 1;
    }
    partial.crc_evaluated = 1.0f;
    partial.crc_pass = 1.0f;
    partial.frame_sync_margin_measured = false;
    partial.normalized_frame_sync_score = 1.0f;
    const auto no_competitor = assess_trust(partial, compute_trust_score_breakdown(partial));
    if (std::string(no_competitor.band) != "guarded") {
        std::cerr << "Disabling the separation gate cannot earn high confidence without a competitor\n";
        return 1;
    }
    return 0;
}
