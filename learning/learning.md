# AyuGramDesktop — Learning

## Media Player Architecture
- Uses FFmpeg for demuxing/decoding, OpenAL Soft for audio output, custom OpenGL/RHI for video rendering.
- Audio pipeline: FFMpegLoader → AVFilterGraph (atempo) → SwrContext → Mixer → OpenAL.
- Speed changes use FFmpeg `atempo` filter, pitch-preserved. Existing `changeSpeedFilter()` method.
- A/V sync: OpenAL sample offset is the master clock. Video frames are timed against it.
- Hardware acceleration: D3D11VA (Win), VideoToolbox (macOS), VA-API (Linux).

## Codebase Conventions
- C++20, Qt framework, custom CRL (task queues) and RPL (reactive streams).
- All media code under `Telegram/SourceFiles/media/`.
- Speed limits in `media_common.h`: min 0.5×, max 2.5×.
- AyuGram-specific modifications are kept separate from upstream tdesktop code where possible.

## Key Files for Skip Silence
- `media/audio/media_audio_silence_detector.h/.cpp` — 3-state FSM (Noisy/MaybeSilent/Silent) with RMS dBFS calculation
- `media/audio/media_audio_ffmpeg_loader.h/.cpp` — Audio decoding + atempo filter graph + raw PCM frame inspection
- `media/streaming/media_streaming_player.h/.cpp` — Central coordinator, A/V sync, dynamically modulates speed while keeping user base speed
- `media/player/media_player_dropdown.h/.cpp` — Speed controller dropdown UI with "Skip Silence" toggle item
- `media/media_common.h` — Shared constants (kSilenceSpeedMax = 6.0, kSilenceSpeedDefault = 4.0)
- `ayu/ayu_settings.h/.cpp` — AyuGram native JSON settings for skipSilence flag

## Technical Learnings
- **A/V Sync Preservation**: Hard cutting/splicing PCM samples causes severe keyframe desync and decoder stalls in H.264/H.265. Dynamic playback speed adjustment via `atempo` (2.5× during silence) keeps video and audio presentation timestamps continuously synchronized via OpenAL's audio clock without robotic pitch distortion or frame drops.
- **ExoPlayer / NewPipe Parameter Alignment**: ExoPlayer Media3's `SilenceSkippingAudioProcessor` uses three golden constants: `DEFAULT_SILENCE_THRESHOLD_LEVEL = 1024` (~ -30.1 dBFS), `DEFAULT_MINIMUM_SILENCE_DURATION_US = 100,000 µs` (100ms), and `DEFAULT_PADDING_SILENCE_US = 20,000 µs` (20ms). Aligning `SilenceDetector` to 100ms catches natural conversational cadence, while 20ms hangover padding prevents consonant clipping at word boundaries.
- **Hangover Counter Logic**: In state machine silence detectors, hangover counters must actively decrement during sub-threshold frames in `Noisy` and `MaybeSilent` states. Without active decrement, the transition occurs immediately without preserving boundary speech tails.
- **Thread Safety**: Audio decoding runs on worker threads (`crl::queue`), while player state updates and UI operate on the main Qt thread. Cross-thread silence notifications must be dispatched through `crl::on_main` to reactive streams (`rpl::event_stream<SilenceUpdate>`).
- **Assertion Boundaries**: Upstream Telegram code enforced `speed <= kSpeedMax (2.5)` in `SpeedIndependentPosition` and `Player::setSpeed`. For automated silence skip to work without tripping Debug assertions, boundaries were expanded to `kSilenceSpeedMax = 6.0`.
- **MSVC PCH Memory Limit in CI**: Heavy C++20 projects like Telegram fail on standard 7GB GitHub Actions Windows runners with `error C3859 / C1076` when using `--parallel` because unconstrained MSBuild spawns multiple concurrent `cl.exe` instances mapping large precompiled headers. Resolved by using `al-cheb/configure-pagefile-action@v1.4` (16GB-24GB) and passing `/m:2 /p:CL_MPCount=2 /p:PreferredToolArchitecture=x64` to MSBuild.

