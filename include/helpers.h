// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Paul Ferrand

#include <vector>
#include <tuple>
#include <complex>

using HarmonicVector = std::vector<std::pair<float, std::complex<float>>>;

std::vector<float> extractSignalRange(const float* source, double regionStart, double regionEnd, 
    double samplePeriod, int stride = 2, int offset = 0);

std::pair<float, std::complex<float>> frequencyPeakSearch(float* signal, size_t size, float coarseFrequency, 
    float sampleRate, float centsRange = 20, int pointsPerCents = 2);

std::vector<float> buildWavetable(const HarmonicVector& harmonics, int size,
    bool normalizePower = true);