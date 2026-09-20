# Changelog

All notable changes to WetChorus are recorded here. The published notes for
each release are on the [Releases page](https://github.com/yonie/WetChorus/releases);
this file is the portable copy that travels with the source.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and
this project uses [semantic versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [1.0.0]

Initial release.

### Added
- Bucket-brigade stereo chorus, modelled as the circuit: the LFO drives the
  delay line's clock rather than a delay time, so bandwidth moves with pitch.
- 100% wet - no dry signal on either output at any setting, and no mix control.
- MODE runs the whole way from vibrato to chorus as the LFO phase between the
  two voices, rather than switching between them.
- SPEED and MODE as stepped knobs, 17 detents each, 65 with Shift held.
- DEPTH as a latching button with an indicator lamp above it.
- Mono input metering plus a stereo output pair.
- Full VST3 parameter automation.
- UI Zoom at 75%, 100% or 125% from the panel's right-click menu.
- Saved state carries its own detent count from version 1, so a later change of
  grid cannot move anyone's settings.
