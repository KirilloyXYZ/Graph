#ifndef BENCHMARKDATA_HPP
#define BENCHMARKDATA_HPP

#include <array>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "../benchmarks/Benchmark.hpp"

enum class Sweep { Vertices, Edges, Topologies };

struct ExperimentSettings
{
    BenchmarkTask task = BenchmarkTask::SpanningTree;
    Sweep sweep = Sweep::Vertices;
    Topology topology = Topology::Cycle;
    std::array<bool, 6> topologies{true, false, false, true, true, false};
    std::size_t from = 1000;
    std::size_t to = 5000;
    std::size_t step = 1000;
    std::size_t edgeFrom = 0;
    std::size_t edgeTo = 4000;
    std::size_t edgeStep = 1000;
    std::size_t vertices = 1000;
    std::size_t edges = 150;
    unsigned int seed = 42;
    unsigned int shuffleSeed = 19;
    std::size_t repeats = 7;
};

inline std::size_t CountExperimentRange(std::size_t from, std::size_t to, std::size_t step)
{
    if (step == 0 || from > to) { throw std::invalid_argument("Проверьте границы диапазона и положительный шаг."); }
    std::size_t intervals = (to - from) / step;
    if (intervals == std::numeric_limits<std::size_t>::max()) { throw std::length_error("Диапазон слишком велик."); }
    return intervals + 1;
}

inline std::size_t CheckedExperimentProduct(std::size_t first, std::size_t second)
{
    if (second && first > std::numeric_limits<std::size_t>::max() / second)
    {
        throw std::length_error("Слишком много точек или повторов.");
    }
    return first * second;
}

inline std::vector<Topology> ExperimentTopologies(const ExperimentSettings& settings)
{
    if (settings.sweep == Sweep::Edges) { return {Topology::Random}; }
    if (settings.sweep == Sweep::Vertices) { return {settings.topology}; }

    std::vector<Topology> values;
    for (std::size_t i = 0; i < settings.topologies.size(); ++i)
    {
        if (settings.topologies[i]) { values.push_back(static_cast<Topology>(i)); }
    }
    if (values.empty()) { throw std::invalid_argument("Выберите хотя бы одну топологию."); }
    return values;
}

inline std::size_t ExperimentSizes(const ExperimentSettings& settings)
{
    return settings.sweep == Sweep::Edges
        ? CountExperimentRange(settings.edgeFrom, settings.edgeTo, settings.edgeStep)
        : CountExperimentRange(settings.from, settings.to, settings.step);
}

inline GenerationOptions OptionsAt(const ExperimentSettings& settings, Topology topology, std::size_t index)
{
    if (settings.sweep == Sweep::Edges)
    {
        return {Topology::Random, settings.vertices, settings.edgeFrom + index * settings.edgeStep, settings.seed};
    }
    return {topology, settings.from + index * settings.step, topology == Topology::Random ? settings.edges : 0, settings.seed};
}

inline std::size_t ValidateExperiment(const ExperimentSettings& settings)
{
    auto topologies = ExperimentTopologies(settings);
    auto sizes = ExperimentSizes(settings);
    // У всех поддерживаемых топологий допустимость диапазона проверяется на его концах.
    for (auto topology : topologies)
    {
        GetGeneratedEdgeCount(OptionsAt(settings, topology, 0));
        GetGeneratedEdgeCount(OptionsAt(settings, topology, sizes - 1));
    }
    return CheckedExperimentProduct(topologies.size(), sizes);
}

#endif
