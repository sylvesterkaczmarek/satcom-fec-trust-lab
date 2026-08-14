.PHONY: help build replay replay-impaired replay-ambiguous replay-failed replay-no-signal acquisition check check-acquisition check-acquisition-neon verify-acquisition-neon check-acquisition-sme2 verify-acquisition-sme2 compare-trust align check-metrics verify verify-fixtures verify-arm benchmark benchmark-acquisition benchmark-acquisition-repeat benchmark-android-build benchmark-android-verify benchmark-android-run benchmark-decoder-legacy test regenerate

help:
	@printf '%s\n' \
	  'Available targets:' \
	  '  make build           Build the supported host-side tools' \
	  '  make replay          Run the baseline replay fixture' \
	  '  make replay-impaired Run the impaired replay fixture' \
	  '  make replay-ambiguous Run the competing-peak replay fixture' \
	  '  make replay-failed   Run the CRC-failing replay fixture' \
	  '  make replay-no-signal Run the acquisition-rejection fixture' \
	  '  make acquisition     Run the clean scalar acquisition fixture' \
	  '  make check           Verify the baseline replay output' \
	  '  make check-acquisition Verify all scalar acquisition fixtures' \
	  '  make check-acquisition-neon Check direct NEON kernel equivalence' \
	  '  make verify-acquisition-neon Verify NEON execution and instructions' \
	  '  make check-acquisition-sme2 Check SME2 equivalence or availability' \
	  '  make verify-acquisition-sme2 Verify SME2 execution and instructions' \
	  '  make compare-trust   Compare all replay trust states' \
	  '  make align           Validate decoder-path alignment' \
	  '  make check-metrics   Validate branch-metric path equivalence' \
	  '  make verify          Run clean strict, sanitizer, and public correctness checks' \
	  '  make verify-fixtures Verify tracked fixture seeds and SHA-256 hashes' \
	  '  make verify-arm      Verify portable and optional Arm build modes' \
	  '  make benchmark       Run the acquisition workload-size sweep' \
	  '  make benchmark-acquisition Run the acquisition workload-size sweep' \
	  '  make benchmark-acquisition-repeat Run five independent benchmark processes' \
	  '  make benchmark-android-build Build the arm64-v8a ADB benchmark' \
	  '  make benchmark-android-verify Inspect the Android ELF and SIMD objects' \
	  '  make benchmark-android-run Build and run it on an authorized ADB device' \
	  '  make benchmark-decoder-legacy Run the legacy decoder microbenchmark' \
	  '  make test            Run the host-side automated tests' \
	  '  make regenerate      Regenerate the checked-in synthetic fixtures'

build:
	bash scripts/build_host_tools.sh all

replay:
	bash scripts/run_replay_demo.sh

replay-impaired:
	bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq

replay-ambiguous:
	bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk_ambiguous.iq

replay-failed:
	bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_failed.iq

replay-no-signal:
	bash scripts/run_replay_demo.sh --allow-failure data/synthetic/canned_replay/demo_conv_bpsk_no_signal.iq

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

verify:
	bash scripts/verify_public_workflow.sh

verify-fixtures:
	python3 scripts/update_fixture_checksums.py --check

verify-arm:
	bash scripts/verify_arm_paths.sh

benchmark:
	bash scripts/benchmark_acquisition.sh

benchmark-acquisition:
	bash scripts/benchmark_acquisition.sh

benchmark-acquisition-repeat:
	python3 scripts/repeat_acquisition_benchmark.py

benchmark-android-build:
	bash scripts/build_android_benchmark.sh

benchmark-android-verify:
	bash scripts/verify_android_benchmark_build.sh

benchmark-android-run:
	bash scripts/run_android_benchmark.sh

benchmark-decoder-legacy:
	bash scripts/benchmark_decoder_paths.sh

test:
	python3 -m unittest discover -s tests -v

regenerate:
	python3 scripts/generate_synthetic_iq.py
	python3 scripts/generate_acquisition_fixtures.py
	python3 scripts/update_fixture_checksums.py
