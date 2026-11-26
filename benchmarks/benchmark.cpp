#include <charconv>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

#include "Benchmark.hpp"

namespace
{
    std::size_t ReadSize(std::string_view text)
    {
        std::size_t number = 0;
        auto result = std::from_chars(text.data(), text.data() + text.size(), number);
        if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        {
            throw std::invalid_argument("Expected an unsigned integer");
        }
        return number;
    }

    Topology ReadTopology(std::string_view name)
    {
        for (Topology topology : {Topology::Chain, Topology::Cycle, Topology::Complete,
                                  Topology::Star, Topology::BinaryTree, Topology::Random})
        {
            if (name == GetTopologyName(topology)) { return topology; }
        }
        throw std::invalid_argument("Unknown graph topology");
    }

    BenchmarkTask ReadTask(std::string_view name)
    {
        if (name == "spanning_tree") { return BenchmarkTask::SpanningTree; }
        if (name == "components") { return BenchmarkTask::Components; }
        throw std::invalid_argument("Use spanning_tree or components");
    }

    void RunCase(const GeneratedGraphData& data, const GenerationOptions& options, BenchmarkTask task,
                 std::size_t repeats, ArraySequence<BenchmarkRecord>& records)
    {
        for (EdgeOrder order : {EdgeOrder::Ascending, EdgeOrder::Descending, EdgeOrder::Shuffled})
        {
            auto report = BenchmarkGeneratedGraph(data, options, task, order, options.seed, repeats);
            std::cout << GetTaskName(task) << ", " << GetTopologyName(options.topology)
                      << ", V = " << data.vertices.GetLength() << ", E = " << data.edges.GetLength()
                      << ", " << GetEdgeOrderName(order) << ", медиана BFS " << std::fixed << std::setprecision(6)
                      << report.bfsMedian << " мс, DFS " << report.dfsMedian << " мс\n";
            for (std::size_t i = 0; i < report.records.GetLength(); ++i) { records.Append(report.records.Get(i)); }
        }
    }
}

int main(int argc, char* argv[])
{
    try
    {
        ArraySequence<BenchmarkRecord> records;
        if (argc == 1)
        {
            for (std::size_t count : {100, 1000})
            {
                GenerationOptions options{Topology::Cycle, count};
                auto data = GenerateGraphData(options);
                RunCase(data, options, BenchmarkTask::SpanningTree, 7, records);
                RunCase(data, options, BenchmarkTask::Components, 7, records);
            }
        }
        else
        {
            if (argc != 5 && argc != 7)
            {
                throw std::invalid_argument("Usage: benchmark <topology> <n> <repeats> <task> [<m> <seed>]");
            }
            GenerationOptions options{ReadTopology(argv[1]), ReadSize(argv[2])};
            std::size_t repeats = ReadSize(argv[3]);
            if (repeats == 0) { throw std::invalid_argument("Benchmark needs at least one repeat"); }
            BenchmarkTask task = ReadTask(argv[4]);
            if ((options.topology == Topology::Random) != (argc == 7))
            {
                throw std::invalid_argument("Pass both m and seed for the random topology");
            }
            if (argc == 7)
            {
                options.edgeCount = ReadSize(argv[5]);
                std::size_t seed = ReadSize(argv[6]);
                if (seed > std::numeric_limits<unsigned int>::max()) { throw std::out_of_range("Seed is too large"); }
                options.seed = static_cast<unsigned int>(seed);
            }
            RunCase(GenerateGraphData(options), options, task, repeats, records);
        }

        auto saved = SaveBenchmarkCsv(records, "benchmark.csv");
        std::cout << "Сохранено в " << saved.string() << '\n';
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Ошибка замера — " << error.what() << '\n';
        return 1;
    }
}
