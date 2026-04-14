.PHONY: help build replay replay-impaired check compare-trust align benchmark test regenerate

help:
	@printf '%s\n' \
	  'Available targets:' \
	  '  make build           Build the supported host-side tools' \
	  '  make replay          Run the baseline replay fixture' \
	  '  make replay-impaired Run the impaired replay fixture' \
	  '  make check           Verify the baseline replay output' \
	  '  make compare-trust   Compare healthy and impaired trust results' \
	  '  make align           Validate decoder-path alignment' \
	  '  make benchmark       Run the local decoder-path timing harness' \
	  '  make test            Run the host-side automated tests' \
	  '  make regenerate      Regenerate the checked-in synthetic fixtures'

build:
	bash scripts/build_host_tools.sh all

replay:
	bash scripts/run_replay_demo.sh

replay-impaired:
	bash scripts/run_replay_demo.sh data/synthetic/canned_replay/demo_conv_bpsk_impaired.iq

check:
	bash scripts/check_replay_demo.sh

compare-trust:
	bash scripts/compare_trust_cases.sh

align:
	bash scripts/validate_decoder_alignment.sh

benchmark:
	bash scripts/benchmark_decoder_paths.sh

test:
	python3 -m unittest discover -s tests -v

regenerate:
	python3 scripts/generate_synthetic_iq.py
