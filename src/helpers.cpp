// SPDX-License-Identifier: MIT
// Copyright (c) 2021 Paul Ferrand

#include "helpers.h"
#define EIGEN_MPL2_ONLY
#include <Eigen/Dense>
#include <numeric>

using namespace std::complex_literals;

template<class T>
constexpr T pi = T { 3.14159265358979323846 };

std::pair<float, std::complex<float>> frequencyPeakSearch(float* signal, size_t size, float coarseFrequency, 
    float sampleRate, float centsRange, int pointsPerCents)
{
    using namespace Eigen;

    // Build the super-resolution time-frequency matrix around the coarse frequency
    float logFreq = std::log2(coarseFrequency);
    VectorXf freq = VectorXf::LinSpaced(2 * pointsPerCents * static_cast<int>(centsRange) + 1, 
        logFreq - centsRange / 1200, logFreq + centsRange / 1200);
    freq = pow(2.0f, freq.array());
    VectorXf time = VectorXf::LinSpaced(size, 0, static_cast<float>(size - 1)) / sampleRate;

    MatrixXcf projectionMatrix = 
        exp(2.0if * pi<float> * (freq * time.transpose()).array());

    // Project the signal on the matrix
    VectorXcf projected = projectionMatrix * Map<VectorXf>(signal, size);

    // Find the highest harmonic
    unsigned maxIdx = 0;
    float maxHarmonic = 0.0f;
    for (unsigned i = 0; i < projected.size(); ++i) {
        float harmonic = std::abs(projected[i]);
        if (harmonic > maxHarmonic) {
            maxIdx = i;
            maxHarmonic = harmonic;
        }
    }

    return std::make_pair(freq[maxIdx], projected[maxIdx]);
}

std::vector<float> buildWavetable(const HarmonicVector& harmonics, int size, bool normalizePower)
{
    using namespace Eigen;
    
    std::vector<double> table;
    std::vector<float> output;
    table.resize(size);
    output.reserve(size);
    std::fill(table.begin(), table.end(), 0.0);

    if (harmonics.empty()) {
        std::fill_n(std::back_inserter(output), size, 0.0f);
        return output;
    }

    using RowArrayXd = Array<double, 1, Dynamic>;
    ArrayXd time = ArrayXd::LinSpaced(size, 0, static_cast<double>(size - 1));
    time /= static_cast<double>(size);
    Map<ArrayXd> mappedTable { table.data(), size };

    for (const auto& [f, h] : harmonics) {
        double freqIndex = std::round(f / harmonics.front().first);
        double phase = std::arg(h);
        double magnitude = std::abs(h);
        // fmt::print("Harmonic at {:.2f} ({}) Hz: {:.3f} exp (i pi {:.3f})\n", f, freqIndex, magnitude, phase);
        mappedTable += magnitude * (2.0 * pi<double> * freqIndex * time + phase).sin();
    }

    // Normalize the overall power
    if (normalizePower) {
        double squaredNorm = std::accumulate(harmonics.begin(), harmonics.end(), 0.0, 
            [] (double lhs, const auto& rhs) { return lhs + std::pow(std::abs(rhs.second), 2); });
        double norm = std::sqrt(squaredNorm);
        mappedTable /= norm;
    }

    // Roll the wavetable to start around 0
    size_t zeroIndex = 0;
    double zeroValue = mappedTable.maxCoeff();
    for (int i = 0; i < size; ++i) {
        double absValue = std::abs(mappedTable[i]);
        if (absValue < zeroValue) {
            zeroIndex = i;
            zeroValue = absValue;
        }
    }
    ArrayXd head = mappedTable.head(zeroIndex);
    ArrayXd tail = mappedTable.tail(size - zeroIndex);
    mappedTable << tail, head;

    std::transform(table.begin(), table.end(), std::back_inserter(output),
            [](double x) { return static_cast<float>(x); });

    return output;
}

std::vector<float> extractSignalRange(const float* source, double regionStart, double regionEnd, 
    double samplePeriod, int stride, int offset)
{
    std::vector<float> signal;

    if (regionStart > regionEnd)
        std::swap(regionStart, regionEnd);

    int rangeStart = static_cast<int>(regionStart / samplePeriod);
    int rangeEnd = static_cast<int>(regionEnd / samplePeriod);
    int rangeSize = rangeEnd - rangeStart;
    if (rangeSize == 0)
        return signal;
        
    signal.resize(rangeSize);
    for (int t = 0, s = rangeStart; s < rangeEnd; ++t, ++s)
        signal[t] = source[stride * s + offset];
    
    return signal;
}