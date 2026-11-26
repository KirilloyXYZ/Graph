#ifndef GRAPHGENERATOR_HPP
#define GRAPHGENERATOR_HPP

#include <cstddef>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

#include "../lab1/ArraySequence.hpp"
#include "../lab1/BinarySearchTree.hpp"

enum class Topology { Chain, Cycle, Complete, Star, BinaryTree, Random };

struct GenerationOptions
{
    Topology topology;
    std::size_t vertexCount;
    std::size_t edgeCount = 0;
    unsigned int seed = 0;
};

using GeneratedEdge = std::pair<std::size_t, std::size_t>;

struct GeneratedGraphData
{
    ArraySequence<std::size_t> vertices;
    ArraySequence<GeneratedEdge> edges;
};

inline const char* GetTopologyName(Topology topology)
{
    switch (topology)
    {
        case Topology::Chain: return "chain";
        case Topology::Cycle: return "cycle";
        case Topology::Complete: return "complete";
        case Topology::Star: return "star";
        case Topology::BinaryTree: return "binary_tree";
        case Topology::Random: return "random";
    }
    throw std::invalid_argument("Unknown graph topology");
}

namespace generator_detail
{
    inline std::size_t CountPairs(std::size_t vertices)
    {
        if (vertices < 2) { return 0; }
        std::size_t first = vertices;
        std::size_t second = vertices - 1;
        if (first % 2 == 0) { first /= 2; }
        else { second /= 2; }
        if (first > std::numeric_limits<std::size_t>::max() / second)
        {
            throw std::length_error("GenerateGraphData: too many possible edges");
        }
        return first * second;
    }

    inline std::size_t CheckAndCountEdges(const GenerationOptions& options)
    {
        std::size_t count = options.vertexCount;
        switch (options.topology)
        {
            case Topology::Chain: return count == 0 ? 0 : count - 1;
            case Topology::Cycle:
                if (count < 3) { throw std::invalid_argument("GenerateGraphData: a cycle needs at least 3 vertices"); }
                return count;
            case Topology::Complete: return CountPairs(count);
            case Topology::Star:
            case Topology::BinaryTree:
                if (count == 0)
                {
                    throw std::invalid_argument("GenerateGraphData: this topology needs at least 1 vertex");
                }
                return count - 1;
            case Topology::Random:
                if (options.edgeCount > CountPairs(count))
                {
                    throw std::invalid_argument("GenerateGraphData: too many edges for this vertex count");
                }
                return options.edgeCount;
        }
        throw std::invalid_argument("Unknown graph topology");
    }

    inline void AppendComplete(std::size_t count, ArraySequence<GeneratedEdge>& edges)
    {
        for (std::size_t u = 0; u < count; ++u)
        {
            for (std::size_t v = u + 1; v < count; ++v) { edges.Append({u + 1, v + 1}); }
        }
    }

    inline GeneratedEdge DecodePair(std::size_t rank, std::size_t count, std::size_t pairs)
    {
        // Строка r начинается с (r+1, r+2). До неё перечислены pairs - C(count-r, 2) пар.
        std::size_t left = 0;
        std::size_t right = count - 1;
        while (right - left > 1)
        {
            std::size_t row = left + (right - left) / 2;
            if (pairs - CountPairs(count - row) <= rank) { left = row; }
            else { right = row; }
        }
        std::size_t offset = rank - (pairs - CountPairs(count - left));
        return {left + 1, left + 2 + offset};
    }

    inline void AppendRandom(const GenerationOptions& options, ArraySequence<GeneratedEdge>& edges)
    {
        if (options.edgeCount == 0) { return; }
        std::size_t pairs = CountPairs(options.vertexCount);
        if (options.edgeCount == pairs)
        {
            AppendComplete(options.vertexCount, edges);
            return;
        }

        // Отбор Флойда, как при выборе уникальных позиций в SparseVector.
        BinarySearchTree<bool, std::size_t> selected;
        std::mt19937 random(options.seed);
        for (std::size_t i = 0; i < options.edgeCount; ++i)
        {
            std::size_t last = pairs - options.edgeCount + i;
            std::uniform_int_distribution<std::size_t> choose(0, last);
            if (!selected.Insert(choose(random), true)) { selected.Insert(last, true); }
        }

        auto enumerator = selected.GetEnumerator();
        while (enumerator->MoveNext())
        {
            edges.Append(DecodePair(enumerator->GetCurrentKey(), options.vertexCount, pairs));
        }
    }
}

inline std::size_t GetGeneratedEdgeCount(const GenerationOptions& options)
{
    return generator_detail::CheckAndCountEdges(options);
}

inline GeneratedGraphData GenerateGraphData(const GenerationOptions& options)
{
    std::size_t edges = GetGeneratedEdgeCount(options);
    std::size_t count = options.vertexCount;
    GeneratedGraphData data{ArraySequence<std::size_t>(count), ArraySequence<GeneratedEdge>(edges)};
    for (std::size_t i = 0; i < count; ++i) { data.vertices.Append(i + 1); }

    switch (options.topology)
    {
        case Topology::Chain:
        case Topology::Cycle:
            for (std::size_t i = 1; i < count; ++i) { data.edges.Append({i, i + 1}); }
            if (options.topology == Topology::Cycle) { data.edges.Append({1, count}); }
            break;
        case Topology::Complete:
            generator_detail::AppendComplete(count, data.edges);
            break;
        case Topology::Star:
            for (std::size_t i = 1; i < count; ++i) { data.edges.Append({1, i + 1}); }
            break;
        case Topology::BinaryTree:
            for (std::size_t i = 1; i < count; ++i) { data.edges.Append({(i + 1) / 2, i + 1}); }
            break;
        case Topology::Random:
            generator_detail::AppendRandom(options, data.edges);
            break;
    }
    return data;
}

#endif
