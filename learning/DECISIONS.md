# AyuGramDesktop — Architecture Decision Records

## ADR-001: Skip Silence — Dynamic Speed Modulation Over Frame Dropping
- **Date**: 2026-10-11
- **Decision**: Use dynamic playback speed changes (via existing `atempo` filter) instead of PCM frame dropping or splicing.
- **Rationale**: Frame dropping causes catastrophic video desync with inter-frame codecs (H.264/H.265). Speed modulation leverages the existing infrastructure and maintains perfect A/V sync.
- **Trade-offs**: Slightly more complex state machine, but zero risk of playback artifacts.

## ADR-002: Skip Silence — RMS Energy Detection Over FFmpeg silencedetect Filter
- **Date**: 2026-10-11
- **Decision**: Implement custom RMS sliding-window detection rather than using FFmpeg's `silencedetect` filter.
- **Rationale**: `silencedetect` adds metadata to frames but doesn't integrate naturally with AyuGram's existing filter graph topology. Custom RMS gives us direct control over the 3-state FSM (Noisy → MaybeSilent → Silent) and avoids filter graph reconfiguration.
- **Trade-offs**: More code to maintain, but more responsive and configurable.

## ADR-003: Skip Silence — Parameter Alignment with ExoPlayer Media3 and Hangover Padding
- **Date**: 2026-10-13
- **Decision**: Adopt ExoPlayer / NewPipe golden parameters (`minSilenceDurationMs = 100`, `paddingMs = 20`, `thresholdDb = -30.0f`) with `kSilenceSpeedDefault = 2.5` and active hangover countdown padding.
- **Rationale**: A 200ms duration felt sluggish on natural conversational pauses. Lowering to 100ms matches ExoPlayer's proven speech cadence detection. Capping silence speed at 2.5x aligns with Telegram's tested maximum speed envelope, preventing `atempo` phase-vocoder pitch chirping and video frame presentation lag while preserving seamless 20ms consonant boundaries.
- **Trade-offs**: Slightly faster transitions between states, safely mitigated by 20ms hangover cushion.
