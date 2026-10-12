/*
This file is part of AyuGramDesktop,
the unofficial desktop application for the Telegram messaging service.

AyuGram Skip Silence Feature
*/
#pragma once

#include <cstdint>
#include <cmath>
#include <functional>

struct AVFrame;

namespace Media {

// Configuration for the silence detection engine.
struct SilenceSkipConfig {
	// Silence threshold in dBFS. Audio below this level is considered silent.
	// -30 dBFS is the gold standard for speech in presence of room/mic noise.
	float thresholdDb = -30.0f;
	// Minimum silence duration in milliseconds before speed-up activates.
	int minSilenceDurationMs = 200;
	// Playback speed multiplier during detected silence.
	float silenceSpeed = 3.0f;
	// Padding in milliseconds to preserve before/after speech boundaries.
	int paddingMs = 30;
};

// The three states of the silence detection FSM.
enum class SilenceState {
	// Audio energy is above threshold — normal playback speed.
	Noisy,
	// Audio dropped below threshold, waiting for minimum duration.
	MaybeSilent,
	// Confirmed silence — speed-up is active.
	Silent,
};

// Real-time silence detector using RMS energy analysis with a
// three-state finite state machine (Noisy → MaybeSilent → Silent).
//
// Thread safety: NOT thread-safe. Must be called from the audio
// decoding thread only (same thread as AbstractAudioFFMpegLoader).
class SilenceDetector {
public:
	explicit SilenceDetector(int sampleRate = 48000, int channels = 2);

	// Feed any decoded FFmpeg AVFrame (supports FLTP, FLT, S16, S16P, U8, S32).
	// Returns the current silence state after processing.
	SilenceState feedFrame(const AVFrame *frame);

	// Feed decoded PCM samples (16-bit signed integers, interleaved).
	// Returns the current silence state after processing.
	SilenceState feed(const int16_t *samples, int sampleCount);

	// Feed decoded PCM samples (unsigned 8-bit, interleaved).
	SilenceState feedU8(const uint8_t *samples, int sampleCount);

	void setConfig(const SilenceSkipConfig &config);
	[[nodiscard]] const SilenceSkipConfig &config() const { return _config; }

	[[nodiscard]] SilenceState state() const { return _state; }
	[[nodiscard]] bool isSilent() const {
		return _state == SilenceState::Silent;
	}
	[[nodiscard]] bool isActive() const {
		return _state != SilenceState::Noisy;
	}

	// Reset the detector to initial state (Noisy).
	void reset();

	// Reconfigure sample rate and channels (e.g., on format change).
	void setSampleFormat(int sampleRate, int channels);

	// Callback invoked when silence state changes.
	// Called from the audio thread — must be lightweight.
	using StateChangedCallback = std::function<void(SilenceState newState)>;
	void setStateChangedCallback(StateChangedCallback callback) {
		_stateChangedCallback = std::move(callback);
	}

private:
	// Compute RMS of any decoded FFmpeg AVFrame.
	[[nodiscard]] float computeRmsFrame(const AVFrame *frame) const;

	// Compute RMS of 16-bit PCM samples (normalized to 0.0–1.0 range).
	[[nodiscard]] float computeRmsS16(
		const int16_t *samples,
		int count) const;

	// Compute RMS of 8-bit unsigned PCM samples.
	[[nodiscard]] float computeRmsU8(
		const uint8_t *samples,
		int count) const;

	// Convert linear RMS amplitude to dBFS.
	[[nodiscard]] float rmsToDbfs(float rms) const;

	// Recalculate sample-based thresholds from millisecond config.
	void recalculateSampleThresholds();

	// Transition to a new state and fire callback if changed.
	void transitionTo(SilenceState newState);

	SilenceSkipConfig _config;
	SilenceState _state = SilenceState::Noisy;

	int _sampleRate = 48000;
	int _channels = 2;

	// Counter of consecutive samples below the silence threshold.
	int64_t _consecutiveSilentSamples = 0;

	// Pre-computed thresholds in samples (from millisecond config).
	int64_t _minSilenceSamples = 0;
	int64_t _paddingSamples = 0;

	// Hangover counter: how many samples to remain in Noisy state
	// after the last above-threshold sample, to avoid cutting
	// the decay tail of speech.
	int64_t _hangoverRemaining = 0;

	StateChangedCallback _stateChangedCallback;
};

} // namespace Media
