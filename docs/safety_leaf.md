# Safety and trust considerations

This project is a demo and research tool. It is not a certified flight or ground system.

## Known hazards

- Misleading link quality metrics under interference or hardware problems  
- Silent data corruption if FEC logic is misconfigured  
- Over reliance on a single trust score in complex scenarios

## Current mitigations

- Simple trust score that can go low when basic indicators look bad  
- Clear separation between demo signals and real mission data  
- Golden vector tests to protect against obvious decoder regressions

## Recommended usage

- Treat the tool as a support instrument during tests, not as the sole authority on link health.  
- Validate new configurations with synthetic data and known good vectors before using them in the field.  
- Keep the trust logic conservative and prefer abstention over false confidence.

As the trust layer matures, additional monitors and tests should be added here together with their rationale.
