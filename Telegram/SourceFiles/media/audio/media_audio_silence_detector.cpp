/*
This file is part of AyuGramDesktop,
the unofficial desktop application for the Telegram messaging service.

AyuGram Skip Silence Feature
*/
#include "media/audio/media_audio_silence_detector.h"

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/samplefmt.h>
}

#include <algorithm>
#include <cstring>

namespace Media {
namespace {

constexpr float kMaxS16Value = 32768.0f;
constexpr float kMaxU8Value = 128.0f; // Centered at 128 for unsigned 8-bit.
constexpr float kMinRmsForLog = 1e-10f; // Prevent log(0).

} // namespace

SilenceDetector::SilenceDetector(int sampleRate, int channels)
: _sampleRate(sampleRate)
, _channels(channels) {
	recalculateSampleThresholds();
}

void SilenceDetector::setConfig(const SilenceSkipConfig &config) {
	_config = config;
	recalculateSampleThresholds();
}

void SilenceDetector::setSampleFormat(int sampleRate, int channels) {
	if (_sampleRate != sampleRate || _channels != channels) {
		_sampleRate = sampleRate;
		_channels = channels;
		recalculateSampleThresholds();
		reset();
	}
}

void SilenceDetector::reset() {
	_state = SilenceState::Noisy;
	_consecutiveSilentSamples = 0;
	_hangoverRemaining = 0;
}

void SilenceDetector::recalculateSampleThresholds() {
	// Convert millisecond durations to sample counts.
	// Note: sample counts are in terms of frames (one per channel group).
	const auto framesPerMs = _sampleRate / 1000;
	_minSilenceSamples = static_cast<int64_t>(
		_config.minSilenceDurationMs) * framesPerMs;
	_paddingSamples = static_cast<int64_t>(
		_config.paddingMs) * framesPerMs;
}

float SilenceDetector::computeRmsS16(
		const int16_t *samples,
		int count) const {
	if (count <= 0) {
		return 0.0f;
	}

	double sumSquares = 0.0;
	for (int i = 0; i < count; ++i) {
		const double normalized = samples[i] / static_cast<double>(kMaxS16Value);
		sumSquares += normalized * normalized;
	}
	return static_cast<float>(std::sqrt(sumSquares / count));
}

float SilenceDetector::computeRmsU8(
		const uint8_t *samples,
		int count) const {
	if (count <= 0) {
		return 0.0f;
	}

	double sumSquares = 0.0;
	for (int i = 0; i < count; ++i) {
		// U8 PCM is centered at 128.
		const double normalized = (static_cast<double>(samples[i]) - 128.0)
			/ kMaxU8Value;
		sumSquares += normalized * normalized;
	}
	return static_cast<float>(std::sqrt(sumSquares / count));
}

float SilenceDetector::rmsToDbfs(float rms) const {
	if (rms < kMinRmsForLog) {
		return -100.0f; // Effectively silent.
	}
	return 20.0f * std::log10(rms);
}

void SilenceDetector::transitionTo(SilenceState newState) {
	if (_state != newState) {
		_state = newState;
		if (_stateChangedCallback) {
			_stateChangedCallback(newState);
		}
	}
}

float SilenceDetector::computeRmsFrame(const AVFrame *frame) const {
	if (!frame || frame->nb_samples <= 0) {
		return 0.0f;
	}
	const auto format = static_cast<AVSampleFormat>(frame->format);
	const auto channels = std::max(1, frame->ch_layout.nb_channels ? frame->ch_layout.nb_channels : _channels);
	const auto nb_samples = frame->nb_samples;
	const bool isPlanar = (format == AV_SAMPLE_FMT_FLTP
		|| format == AV_SAMPLE_FMT_S16P
		|| format == AV_SAMPLE_FMT_U8P
		|| format == AV_SAMPLE_FMT_S32P
		|| format == AV_SAMPLE_FMT_DBLP);

	double sumSquares = 0.0;
	int64_t totalCount = 0;

	if (format == AV_SAMPLE_FMT_FLTP || format == AV_SAMPLE_FMT_FLT) {
		if (isPlanar) {
			for (int c = 0; c < channels; ++c) {
				const auto ptr = reinterpret_cast<const float*>(frame->extended_data[c]);
				if (!ptr) continue;
				for (int i = 0; i < nb_samples; ++i) {
					const double val = ptr[i];
					sumSquares += val * val;
				}
				totalCount += nb_samples;
			}
		} else {
			const auto ptr = reinterpret_cast<const float*>(frame->extended_data[0]);
			if (ptr) {
				const int total = nb_samples * channels;
				for (int i = 0; i < total; ++i) {
					const double val = ptr[i];
					sumSquares += val * val;
				}
				totalCount += total;
			}
		}
	} else if (format == AV_SAMPLE_FMT_S16 || format == AV_SAMPLE_FMT_S16P) {
		if (isPlanar) {
			for (int c = 0; c < channels; ++c) {
				const auto ptr = reinterpret_cast<const int16_t*>(frame->extended_data[c]);
				if (!ptr) continue;
				for (int i = 0; i < nb_samples; ++i) {
					const double val = ptr[i] / 32768.0;
					sumSquares += val * val;
				}
				totalCount += nb_samples;
			}
		} else {
			const auto ptr = reinterpret_cast<const int16_t*>(frame->extended_data[0]);
			if (ptr) {
				const int total = nb_samples * channels;
				for (int i = 0; i < total; ++i) {
					const double val = ptr[i] / 32768.0;
					sumSquares += val * val;
				}
				totalCount += total;
			}
		}
	} else if (format == AV_SAMPLE_FMT_U8 || format == AV_SAMPLE_FMT_U8P) {
		if (isPlanar) {
			for (int c = 0; c < channels; ++c) {
				const auto ptr = reinterpret_cast<const uint8_t*>(frame->extended_data[c]);
				if (!ptr) continue;
				for (int i = 0; i < nb_samples; ++i) {
					const double val = (ptr[i] - 128.0) / 128.0;
					sumSquares += val * val;
				}
				totalCount += nb_samples;
			}
		} else {
			const auto ptr = reinterpret_cast<const uint8_t*>(frame->extended_data[0]);
			if (ptr) {
				const int total = nb_samples * channels;
				for (int i = 0; i < total; ++i) {
					const double val = (ptr[i] - 128.0) / 128.0;
					sumSquares += val * val;
				}
				totalCount += total;
			}
		}
	} else if (format == AV_SAMPLE_FMT_S32 || format == AV_SAMPLE_FMT_S32P) {
		if (isPlanar) {
			for (int c = 0; c < channels; ++c) {
				const auto ptr = reinterpret_cast<const int32_t*>(frame->extended_data[c]);
				if (!ptr) continue;
				for (int i = 0; i < nb_samples; ++i) {
					const double val = ptr[i] / 2147483648.0;
					sumSquares += val * val;
				}
				totalCount += nb_samples;
			}
		} else {
			const auto ptr = reinterpret_cast<const int32_t*>(frame->extended_data[0]);
			if (ptr) {
				const int total = nb_samples * channels;
				for (int i = 0; i < total; ++i) {
					const double val = ptr[i] / 2147483648.0;
					sumSquares += val * val;
				}
				totalCount += total;
			}
		}
	}

	if (totalCount <= 0) {
		return 0.0f;
	}
	return static_cast<float>(std::sqrt(sumSquares / totalCount));
}

SilenceState SilenceDetector::feedFrame(const AVFrame *frame) {
	if (!frame || frame->nb_samples <= 0) {
		return _state;
	}
	const auto rms = computeRmsFrame(frame);
	const auto db = rmsToDbfs(rms);
	const auto belowThreshold = (db < _config.thresholdDb);

	const auto frameCount = frame->nb_samples;

	switch (_state) {
	case SilenceState::Noisy:
		if (belowThreshold) {
			_consecutiveSilentSamples += frameCount;
			if (_consecutiveSilentSamples >= _minSilenceSamples) {
				transitionTo(SilenceState::Silent);
			} else {
				transitionTo(SilenceState::MaybeSilent);
			}
		} else {
			_consecutiveSilentSamples = 0;
		}
		_hangoverRemaining = _paddingSamples;
		break;

	case SilenceState::MaybeSilent:
		if (belowThreshold) {
			_consecutiveSilentSamples += frameCount;
			if (_consecutiveSilentSamples >= _minSilenceSamples) {
				transitionTo(SilenceState::Silent);
			}
		} else {
			_consecutiveSilentSamples = 0;
			transitionTo(SilenceState::Noisy);
		}
		break;

	case SilenceState::Silent:
		if (!belowThreshold) {
			_consecutiveSilentSamples = 0;
			_hangoverRemaining = _paddingSamples;
			transitionTo(SilenceState::Noisy);
		}
		break;
	}

	return _state;
}

SilenceState SilenceDetector::feed(
		const int16_t *samples,
		int sampleCount) {
	const auto rms = computeRmsS16(samples, sampleCount);
	const auto db = rmsToDbfs(rms);
	const auto belowThreshold = (db < _config.thresholdDb);

	// Number of audio frames (not individual channel samples).
	const auto frameCount = (_channels > 0)
		? (sampleCount / _channels)
		: sampleCount;

	switch (_state) {
	case SilenceState::Noisy:
		if (belowThreshold) {
			_consecutiveSilentSamples += frameCount;
			if (_consecutiveSilentSamples >= _minSilenceSamples) {
				transitionTo(SilenceState::Silent);
			} else {
				transitionTo(SilenceState::MaybeSilent);
			}
		} else {
			_consecutiveSilentSamples = 0;
		}
		// Reset hangover counter while noisy.
		_hangoverRemaining = _paddingSamples;
		break;

	case SilenceState::MaybeSilent:
		if (belowThreshold) {
			_consecutiveSilentSamples += frameCount;
			if (_consecutiveSilentSamples >= _minSilenceSamples) {
				transitionTo(SilenceState::Silent);
			}
		} else {
			// False alarm — speech resumed before minimum duration.
			_consecutiveSilentSamples = 0;
			transitionTo(SilenceState::Noisy);
		}
		break;

	case SilenceState::Silent:
		if (!belowThreshold) {
			// Speech has resumed — instant recovery.
			_consecutiveSilentSamples = 0;
			_hangoverRemaining = _paddingSamples;
			transitionTo(SilenceState::Noisy);
		}
		break;
	}

	return _state;
}

SilenceState SilenceDetector::feedU8(
		const uint8_t *samples,
		int sampleCount) {
	const auto rms = computeRmsU8(samples, sampleCount);
	const auto db = rmsToDbfs(rms);
	const auto belowThreshold = (db < _config.thresholdDb);

	const auto frameCount = (_channels > 0)
		? (sampleCount / _channels)
		: sampleCount;

	switch (_state) {
	case SilenceState::Noisy:
		if (belowThreshold) {
			_consecutiveSilentSamples += frameCount;
			if (_consecutiveSilentSamples >= _minSilenceSamples) {
				transitionTo(SilenceState::Silent);
			} else {
				transitionTo(SilenceState::MaybeSilent);
			}
		} else {
			_consecutiveSilentSamples = 0;
		}
		_hangoverRemaining = _paddingSamples;
		break;

	case SilenceState::MaybeSilent:
		if (belowThreshold) {
			_consecutiveSilentSamples += frameCount;
			if (_consecutiveSilentSamples >= _minSilenceSamples) {
				transitionTo(SilenceState::Silent);
			}
		} else {
			_consecutiveSilentSamples = 0;
			transitionTo(SilenceState::Noisy);
		}
		break;

	case SilenceState::Silent:
		if (!belowThreshold) {
			_consecutiveSilentSamples = 0;
			_hangoverRemaining = _paddingSamples;
			transitionTo(SilenceState::Noisy);
		}
		break;
	}

	return _state;
}

} // namespace Media
