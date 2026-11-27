#ifndef BENCHMARK_HPP
#define BENCHMARK_HPP

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include "../generator/GraphGenerator.hpp"
#include "../graph/GraphAlgorithms.hpp"
#include "../lab2/TreeDictionary.hpp"

enum class BenchmarkTask { SpanningTree, Components };
enum class EdgeOrder { Current, Ascending, Descending, Shuffled };

struct BenchmarkInput
{
    std::optional<GenerationOptions> generation;
    EdgeOrder order = EdgeOrder::Current;
    unsigned int shuffleSeed = 0;
};

struct BenchmarkRecord
{
    BenchmarkTask task;
    std::string_view algorithm;
    BenchmarkInput input;
    std::size_t vertices;
    std::size_t edges;
    std::string startKey;
    std::size_t repeat;
    double milliseconds;
    std::size_t components = 0;
    std::size_t visited = 0;
    std::size_t series = 0;
    std::string source{};
};

struct BenchmarkReport
{
    ArraySequence<BenchmarkRecord> records;
    double bfsMedian;
    double dfsMedian;
    bool stopped = false;
};

struct BenchmarkControl
{
    std::function<bool()> shouldStop;
    std::function<void()> measured;

    bool ShouldStop() const { return shouldStop && shouldStop(); }
    void Measured() const { if (measured) { measured(); } }
};

inline const char* GetTaskName(BenchmarkTask task)
{
    switch (task)
    {
        case BenchmarkTask::SpanningTree: return "spanning_tree";
        case BenchmarkTask::Components: return "components";
    }
    throw std::invalid_argument("Unknown benchmark task");
}

inline const char* GetEdgeOrderName(EdgeOrder order)
{
    switch (order)
    {
        case EdgeOrder::Current: return "current";
        case EdgeOrder::Ascending: return "ascending";
        case EdgeOrder::Descending: return "descending";
        case EdgeOrder::Shuffled: return "shuffled";
    }
    throw std::invalid_argument("Unknown edge order");
}

inline double Median(ArraySequence<double> values)
{
    std::size_t count = values.GetLength();
    if (count == 0) { throw std::invalid_argument("Median: no samples"); }
    double* first = &values.Get(0);
    std::sort(first, first + count);
    std::size_t middle = count / 2;
    if (count % 2 == 1) { return values.Get(middle); }
    return values.Get(middle - 1) / 2 + values.Get(middle) / 2;
}

namespace benchmark_detail
{
    template<typename Action>
    double Measure(Action action)
    {
        auto start = std::chrono::steady_clock::now();
        action();
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - start).count();
    }

    template<typename Edge, typename Compare = std::less<Edge>>
    ArraySequence<Edge> ArrangeEdges(const ArraySequence<Edge>& source, EdgeOrder order, unsigned int seed,
                                     Compare compare = {})
    {
        GetEdgeOrderName(order);
        ArraySequence<Edge> edges(source);
        if (edges.GetLength() < 2 || order == EdgeOrder::Current) { return edges; }

        auto* first = &edges.Get(0);
        auto* end = first + edges.GetLength();
        std::sort(first, end, compare);
        if (order == EdgeOrder::Descending) { std::reverse(first, end); }
        if (order == EdgeOrder::Shuffled)
        {
            std::mt19937 random(seed);
            std::shuffle(first, end, random);
        }
        return edges;
    }

    template<typename Key, typename Weight>
    ArraySequence<std::size_t> LabelComponents(const Graph<Key, Weight>& graph, const ConnectedComponentsResult<Key>& result)
    {
        std::size_t count = graph.GetVertexCount();
        if (result.order.GetLength() != count) { throw std::logic_error("Benchmark: incomplete partition"); }

        auto labels = graph_detail::CreatePositions(count);
        ArraySequence<std::size_t> order(count);
        std::size_t begin = 0;
        for (std::size_t i = 0; i < result.componentEnds.GetLength(); ++i)
        {
            std::size_t end = result.componentEnds.Get(i);
            if (end <= begin || end > count) { throw std::logic_error("Benchmark: invalid component range"); }
            std::size_t label = count;
            for (std::size_t j = begin; j < end; ++j)
            {
                auto index = graph.FindVertexIndex(*result.order.Get(j));
                if (!index || labels.Get(*index) != count)
                {
                    throw std::logic_error("Benchmark: invalid component member");
                }
                labels.Get(*index) = *index;
                order.Append(*index);
                label = std::min(label, *index);
            }
            for (std::size_t j = begin; j < end; ++j) { labels.Get(order.Get(j)) = label; }
            begin = end;
        }
        if (begin != count) { throw std::logic_error("Benchmark: incomplete component ranges"); }
        return labels;
    }

    template<typename Key, typename Weight>
    void CheckSpanningTree(const Graph<Key, Weight>& graph, const SpanningTreeResult<Key>& result,
                           std::optional<std::size_t> start, const ArraySequence<std::size_t>& labels,
                           std::size_t reached)
    {
        std::size_t count = graph.GetVertexCount();
        auto expected = count == 0 ? SpanningTreeStatus::Empty :
            (reached == count ? SpanningTreeStatus::Found : SpanningTreeStatus::Disconnected);
        if (result.status != expected || result.order.GetLength() != reached)
        {
            throw std::logic_error("Benchmark: incorrect spanning-tree status or size");
        }

        std::size_t edges = expected == SpanningTreeStatus::Found ? count - 1 : 0;
        if (result.edges.GetLength() != edges) { throw std::logic_error("Benchmark: incorrect tree edge count"); }
        if (count == 0) { return; }

        auto seen = graph_detail::CreatePositions(count);
        for (std::size_t i = 0; i < reached; ++i)
        {
            auto index = graph.FindVertexIndex(*result.order.Get(i));
            if (!index || seen.Get(*index) != count || labels.Get(*index) != labels.Get(*start) ||
                (i == 0 && *index != *start))
            {
                throw std::logic_error("Benchmark: invalid member of traversal from the selected start");
            }
            seen.Get(*index) = i;
        }

        for (std::size_t i = 0; i < edges; ++i)
        {
            const auto& edge = result.edges.Get(i);
            if (edge.second != i + 1 || edge.first >= edge.second ||
                !graph.ContainsEdge(*result.order.Get(edge.first), *result.order.Get(edge.second)))
            {
                throw std::logic_error("Benchmark: invalid edge in spanning tree");
            }
        }
    }

    inline void WriteCsvField(std::ostream& output, const std::string& value)
    {
        if (value.find_first_of(",\"\r\n") == std::string::npos)
        {
            output << value;
            return;
        }

        output << '"';
        for (char symbol : value)
        {
            if (symbol == '"') { output << '"'; }
            output << symbol;
        }
        output << '"';
    }
}

template<typename Key, typename Weight>
BenchmarkReport CompareAlgorithms(const Graph<Key, Weight>& graph, BenchmarkTask task, std::optional<std::size_t> start,
                                  std::size_t repeats, const BenchmarkInput& input = {},
                                  const BenchmarkControl& control = {})
{
    GetTaskName(task);
    if (repeats == 0 || repeats > std::numeric_limits<std::size_t>::max() / 2)
    {
        throw std::invalid_argument("Benchmark: incorrect repeat count");
    }

    std::size_t count = graph.GetVertexCount();
    std::string startKey;
    if (task == BenchmarkTask::SpanningTree && count != 0)
    {
        if (!start || *start >= count) { throw std::out_of_range("Benchmark: start vertex not found"); }
        std::ostringstream text;
        text << graph.GetVertexKey(*start);
        startKey = text.str();
    }

    if (control.ShouldStop()) { return {ArraySequence<BenchmarkRecord>(), 0, 0, true}; }

    auto reference = FindConnectedComponents(graph, CreateBFSEnumerator<Key, Weight>);
    auto labels = benchmark_detail::LabelComponents(graph, reference);
    std::size_t reached = 0;
    if (task == BenchmarkTask::SpanningTree && count != 0)
    {
        std::size_t label = labels.Get(*start);
        for (std::size_t i = 0; i < count; ++i)
        {
            if (labels.Get(i) == label) { ++reached; }
        }
    }

    BenchmarkReport report{ArraySequence<BenchmarkRecord>(repeats * 2), 0, 0};
    ArraySequence<double> bfs(repeats);
    ArraySequence<double> dfs(repeats);

    const std::pair<std::string_view, TraversalFactory<Key, Weight>> algorithms[]{
        {"BFS", CreateBFSEnumerator<Key, Weight>}, {"DFS", CreateDFSEnumerator<Key, Weight>}
    };
    auto run = [&](std::size_t algorithmIndex, std::size_t repeat)
    {
        const auto& algorithm = algorithms[algorithmIndex];
        double milliseconds = 0;
        if (task == BenchmarkTask::SpanningTree)
        {
            SpanningTreeResult<Key> result;
            milliseconds = benchmark_detail::Measure([&]() { result = FindSpanningTree(graph, start, algorithm.second); });
            benchmark_detail::CheckSpanningTree(graph, result, start, labels, reached);
        }
        else
        {
            ConnectedComponentsResult<Key> result;
            milliseconds = benchmark_detail::Measure([&]() { result = FindConnectedComponents(graph, algorithm.second); });
            auto actual = benchmark_detail::LabelComponents(graph, result);
            for (std::size_t i = 0; i < count; ++i)
            {
                if (actual.Get(i) != labels.Get(i)) { throw std::logic_error("Benchmark: partitions differ"); }
            }
        }

        control.Measured();
        return BenchmarkRecord{task, algorithm.first, input, count, graph.GetEdgeCount(), startKey, repeat, milliseconds,
                               reference.componentEnds.GetLength(),
                               task == BenchmarkTask::SpanningTree ? reached : count};
    };

    auto keep = [&](const BenchmarkRecord& record)
    {
        report.records.Append(record);
        if (record.algorithm == "BFS") { bfs.Append(record.milliseconds); }
        else { dfs.Append(record.milliseconds); }
    };
    for (std::size_t trial = 0; trial < repeats; ++trial)
    {
        if (control.ShouldStop())
        {
            report.stopped = true;
            break;
        }
        auto firstMethod = trial % 2;
        auto secondMethod = 1 - firstMethod;
        auto first = run(firstMethod, trial + 1);
        if (control.ShouldStop())
        {
            report.stopped = true;
            break;
        }
        auto second = run(secondMethod, trial + 1);
        keep(first);
        keep(second);
    }
    if (bfs.GetLength() != 0)
    {
        report.bfsMedian = Median(std::move(bfs));
        report.dfsMedian = Median(std::move(dfs));
    }
    return report;
}

inline BenchmarkReport BenchmarkGeneratedGraph(const GeneratedGraphData& data, const GenerationOptions& options,
                                               BenchmarkTask task, EdgeOrder order, unsigned int shuffleSeed,
                                               std::size_t repeats, const BenchmarkControl& control = {})
{
    if (control.ShouldStop()) { return {ArraySequence<BenchmarkRecord>(), 0, 0, true}; }
    auto edges = benchmark_detail::ArrangeEdges(data.edges, order, shuffleSeed);
    Graph<std::size_t> graph(data.vertices, edges, std::make_unique<TreeDictionary<std::size_t, std::size_t>>(),
                             CreateArraySequence, CreateArraySequence);
    auto start = task == BenchmarkTask::SpanningTree ? graph.FindVertexIndex(1) : std::nullopt;
    return CompareAlgorithms(graph, task, start, repeats, {options, order, shuffleSeed}, control);
}

inline void WriteBenchmarkCsv(std::ostream& output, const Sequence<BenchmarkRecord>& records,
                              bool extended = false)
{
    output.imbue(std::locale::classic());
    output << "task,algorithm,topology,V,E,n,m,seed,edge_order,shuffle_seed,start,repeat,time_ms";
    if (extended) { output << ",series,source,components,visited,graph_status"; }
    output << '\n';
    output << std::fixed << std::setprecision(9);
    for (std::size_t i = 0; i < records.GetLength(); ++i)
    {
        const auto& record = records.Get(i);
        const auto& generation = record.input.generation;
        output << GetTaskName(record.task) << ','
               << record.algorithm << ','
               << (generation ? GetTopologyName(generation->topology) : "manual") << ','
               << record.vertices << ',' << record.edges << ',';
        if (generation) { output << generation->vertexCount; }
        output << ',';
        if (generation && generation->topology == Topology::Random) { output << generation->edgeCount; }
        output << ',';
        if (generation && generation->topology == Topology::Random) { output << generation->seed; }
        output << ',' << GetEdgeOrderName(record.input.order) << ',';
        if (record.input.order == EdgeOrder::Shuffled) { output << record.input.shuffleSeed; }
        output << ',';
        benchmark_detail::WriteCsvField(output, record.startKey);
        output << ',' << record.repeat << ',' << record.milliseconds;
        if (extended)
        {
            output << ',' << record.series << ',';
            benchmark_detail::WriteCsvField(output, record.source);
            const char* status = record.vertices == 0 ? "empty" : (record.components == 1 ? "connected" : "disconnected");
            output << ',' << record.components << ',' << record.visited << ',' << status;
        }
        output << '\n';
    }
}

inline std::filesystem::path SaveBenchmarkCsvTo(const Sequence<BenchmarkRecord>& records,
                                               const std::filesystem::path& path, bool extended = false)
{
    if (path.extension() != ".csv") { throw std::invalid_argument("Use a filename with the .csv extension"); }
    auto directory = path.has_parent_path() ? path.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(directory);
    auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    auto staging = directory / (".csv-" + std::to_string(nonce));
    while (!std::filesystem::create_directory(staging)) { staging += "-"; }

    auto temporary = staging / path.filename();
    std::error_code ignored;
    try
    {
        std::ofstream output;
        output.exceptions(std::ios::badbit | std::ios::failbit);
        output.open(temporary);
        WriteBenchmarkCsv(output, records, extended);
        output.close();
        // Заменяем итоговый файл после успешной записи всех строк.
        std::filesystem::rename(temporary, path);
    }
    catch (...)
    {
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove(staging, ignored);
        throw;
    }
    std::filesystem::remove(staging, ignored);
    return path;
}

inline std::filesystem::path SaveBenchmarkCsv(const Sequence<BenchmarkRecord>& records,
                                             const std::string& filename)
{
    std::filesystem::path file(filename);
    if (file != file.filename())
    {
        throw std::invalid_argument("Use a filename with the .csv extension");
    }
    return SaveBenchmarkCsvTo(records, std::filesystem::path("results") / file);
}

#endif
