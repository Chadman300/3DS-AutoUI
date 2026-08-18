# 3DS AutoUI implementation plan

## Milestone 1: baseline OBD2 + dashboard
- Validate a working bridge from OBD2 adapter to a clean JSON gauge payload
- Keep standardized values for RPM, speed, coolant, boost, voltage, fuel, load, and throttle
- Confirm the demo dashboard can render these values cleanly

## Milestone 2: customization layer
- Add theme presets and a color-aware dashboard profile
- Allow per-gauge min/max and warning ranges
- Save profile presets for different vehicles or driving styles

## Milestone 3: brand-aware UI
- Detect or select a car brand from the profile or OBD2 data
- Show the correct local logo asset for Subaru / WRX
- Add fallback generic logo when detection is missing

## Milestone 4: 3DS-ready rendering
- Convert the dashboard renderer into a 3DS-friendly state model
- Keep the UI simple, readable, and high contrast
- Add low-refresh-rate update logic for performance

## Milestone 5: on-car validation
- Compare live values against a phone app and the cluster
- Tune the gauge ranges and safe warnings for the WRX
- Add vehicle-specific overrides when needed

## Execution status
- Started with a working demo bridge and dashboard renderer
- Added a configurable Subaru WRX profile
- Added a local logo asset system for brand-aware UI
- Ready for the next pass: richer customization and a 3DS-friendly rendering model
