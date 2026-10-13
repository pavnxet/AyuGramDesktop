# AyuGramDesktop — Execution Flow

## Media Playback Pipeline
```
[File/MTProto] → Media::Streaming::File (demux thread)
    ├── Video Packets → VideoTrackObject (crl::queue) → VideoTrack::Shared (ring buffer) → Renderer
    └── Audio Packets → Streaming::AudioTrack → Mixer::feedFromExternal()
                                                     ↓
                                        FFMpegLoader / ChildFFMpegLoader
                                                     ↓
                                        AVFilterGraph (atempo)
                                                     ↓
                                        SwrContext → PCM 16-bit
                                                     ↓
                                        Media::Player::Mixer → OpenAL
```

## Speed Change Flow
```
UI (SpeedController) → PlaybackControls::saveSpeed()
    → Player::setSpeed()
        → AudioTrack: AbstractAudioFFMpegLoader::changeSpeedFilter(speed)
        → VideoTrack: adjusts _renderFrameTimer intervals
```

## Skip Silence Integration Flow (Implemented)
```
[User clicks "⚡ Skip Silence" in Speed dropdown]
    ↓
AyuSettings::getInstance().setSkipSilence(true)
    ↓
[Audio Decoding Thread (AbstractAudioFFMpegLoader)]
    ↓
readOrBufferForFilter(): feeds decoded PCM frames to SilenceDetector
    ↓
SilenceDetector (3-state FSM: Noisy → MaybeSilent → Silent)
    ↓
On state change: dispatches via crl::on_main -> Media::Player::NotifySilenceChanged(audioId, isSilent)
    ↓
[Main Thread]
    ↓
Media::Player::SilenceChanged() event stream fires
    ↓
Media::Streaming::Player::handleSilenceStateChanged(isSilent)
    ↓
Player::applyEffectiveSpeed():
    - If silence active && skipSilence: speeds up to std::max(_userSpeed, kSilenceSpeedDefault) (2.5×)
    - If speech resumes: instantly restores user's base speed (_userSpeed)
    - Applies to BOTH _audio->setSpeed() and _video->setSpeed() synchronously
```

## Recent Changes
- 2026-10-13: Aligned Skip Silence with NewPipe / ExoPlayer Media3 parameters & fixed hangover padding
  - Modified: `media_common.h` (`kSilenceSpeedDefault = 2.5`)
  - Modified: `media/audio/media_audio_silence_detector.h` (`minSilenceDurationMs = 100`, `silenceSpeed = 2.5`, `paddingMs = 20`)
  - Modified: `media/audio/media_audio_silence_detector.cpp` (Fixed hangover decrement logic in `feedFrame`, `feed`, `feedU8` to enforce 20ms boundary padding)
  - Modified: `media/streaming/media_streaming_player.cpp` (Used `std::max(_userSpeed, kSilenceSpeedDefault)`)
  - Modified: `media/audio/media_audio_ffmpeg_loader.cpp` (Reset silence detector on `startReadingQueuedFrames`)
- 2026-10-11: Implemented Skip Silence feature
  - New: `media_audio_silence_detector.h/.cpp` (SilenceDetector class, RMS sliding window, 3-state FSM)
  - Modified: `media_common.h` (`kSilenceSpeedMax = 6.0`, `kSilenceSpeedDefault = 4.0`)
  - Modified: `Telegram/CMakeLists.txt` (Registered new detector files)
  - Modified: `ayu/ayu_settings.h/.cpp` (Added `skipSilence` JSON-persisted setting with reactive stream)
  - Modified: `media/audio/media_audio.h/.cpp` (Added `SilenceUpdate` struct, `SilenceChanged()` producer, `NotifySilenceChanged()`)
  - Modified: `media/audio/media_audio_loader.h` (Added silence skip virtual interface)
  - Modified: `media/audio/media_audio_ffmpeg_loader.h/.cpp` (Integrated SilenceDetector, feed frames, clamp up to `kSilenceSpeedMax`)
  - Modified: `media/audio/media_audio_loaders.cpp` (Hooked `setSilenceCallback` on loader creation)
  - Modified: `media/streaming/media_streaming_player.h/.cpp` (Subscribed to `SilenceChanged`, implemented `handleSilenceStateChanged` and `applyEffectiveSpeed`)
  - Modified: `media/player/media_player_dropdown.cpp` (Added "Skip Silence" checkbox toggle item to `FillSpeedMenu`)
