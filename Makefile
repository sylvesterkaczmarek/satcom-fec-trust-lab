.PHONY: help build replay replay-impaired replay-failed acquisition check check-acquisition check-acquisition-neon verify-acquisition-neon check-acquisition-sme2 verify-acquisition-sme2 compare-trust align check-metrics verify-arm benchmark test regenerate

help:
	@printf '%s\n' \
	  'Available targets:' \
	  '  make build           Build the supported host-side tools' \
	  '  make replay          Run the baseline replay fixture' \
	  '  make replay-impaired Run the impaired replay fixture' \
	  '  make replay-failed   Run the CRC-failing replay fixture' \
	  '  make acquisition     Run the clean scalar acquisition fixture' \
	  '  make check           Verify the baseline replay output' \
	  '  make check-acquisition Verify all scalar acquisition fixtures' \
	  '  make check-acquisition-neon Check direct NEON kernel equivalence' \
	  '  make verify-acquisition-neon Verify NEON execution and instructions' \
	  '  make check-acquisition-sme2 Check SME2 equivalence or availability' \
	  '  make verify-acquisition-sme2 Verify SME2 execution and instructions' \
	  '  make compare-trust   Compare healthy, impaired, and failed trust results' \
	  '  make align           Validate decoder-path alignment' \
	  '  make check-metrics   Validate branch-metric path equivalence' \
	  '  make verify-arm      Verify portable and optional Arm build modes' \
	  '  make benchmark       Run the local decoder-path timing harness' \
	  '  make test            Run the host-side automated tests' \
	  '  make regenerate      Regenerate the checked-in synthetic fixtures'

build:
	bash scripts/build_host_tools.sh all

replay:
	bash scripts/run_replay_demo.sh

replay-impaired:
	bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq

replay-failed:
	bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_failed.iq

acquisition:
	bash scripts/run_acquisition_demo.sh

check:
	bash scripts/check_replay_demo.sh

check-acquisition:
	bash scripts/check_acquisition_demo.sh

check-acquisition-neon:
	bash scripts/check_acquisition_neon.sh

verify-acquisition-neon:
	bash scripts/verify_acquisition_neon.sh

check-acquisition-sme2:
	bash scripts/check_sme2_acquisition.sh

verify-acquisition-sme2:
	bash scripts/verify_sme2_acquisition_assembly.sh

compare-trust:
	bash scripts/compare_trust_cases.sh

align:
	bash scripts/validate_decoder_alignment.sh

check-metrics:
	bash scripts/check_branch_metrics.sh

verify-arm:
	bash scripts/verify_arm_paths.sh

benchmark:
	bash scripts/benchmark_decoder_paths.sh

test:
	python3 -m unittest discover -s tests -v

regenerate:
	python3 scripts/generate_synthetic_iq.py
	python3 scripts/generate_acquisition_fixtures.py
