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
