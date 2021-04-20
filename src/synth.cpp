// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Paul Ferrand

#include "synth.h"

Synth::Synth(int blockSize)
: blockSize_(blockSize) 
{
    buffers_[0].resize(blockSize);
    buffers_[1].resize(blockSize);
    synth_.setSamplesPerBlock(blockSize);
    playSample_.test_and_set();
    playSampleOff_.test_and_set();
    playWave_.test_and_set();
    playWaveOff_.test_and_set();
}

void Synth::callback(float* output, int frameCount)
{
    std::unique_lock<std::mutex> lock { callbackLock_, std::try_to_lock };
    if (!lock.owns_lock())
        return;

    float* audioBuffer[2] { buffers_[0].data(), buffers_[1].data() };
    int renderIdx { 0 };
    
    if (!playSample_.test_and_set())
        synth_.noteOn(0, sampleNote_, 127);

    if (!playSampleOff_.test_and_set())
        synth_.noteOff(1, sampleNote_, 127);

    if (!playWave_.test_and_set())
        synth_.noteOn(0, waveNote_, 127);

    if (!playWaveOff_.test_and_set())
        synth_.noteOff(1, waveNote_, 127);

    while (frameCount > 0) {
        int frames = std::min(frameCount, blockSize_);
        synth_.renderBlock(audioBuffer, frames);
        for (int i = 0; i < frames; i++) {
            output[renderIdx + 2 * i] = buffers_[0][i];
            output[renderIdx + 2 * i + 1] = buffers_[1][i];
        }
        renderIdx += 2 * frames;
        frameCount -= frames;
    }
}

void Synth::loadString(std::string_view sfz)
{
    std::lock_guard<std::mutex> lock { callbackLock_ };
    synth_.loadSfzString(sfzPath_.string(), std::string(sfz));
}
