#ifndef MINIMUMSPANNINGTREE_HPP
#define MINIMUMSPANNINGTREE_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "GraphAlgorithms.hpp"

template<typename Weight>
struct MinimumTreeEdge
{
    std::size_t first;
    std::size_t second;
    Weight weight;
};

template<typename Key, typename Weight>
struct MinimumSpanningTreeResult
{
    SpanningTreeStatus status = SpanningTreeStatus::Empty;
    TraversalKeys<Key> vertices;
    ArraySequence<MinimumTreeEdge<Weight>> edges;
};

namespace graph_detail
{
    class DisjointSets
    {
    private:
        ArraySequence<std::size_t> parents;
        ArraySequence<std::size_t> sizes;

        std::size_t Find(std::size_t vertex)
        {
            while (vertex != parents.Get(vertex))
            {
                parents.Get(vertex) = parents.Get(parents.Get(vertex));
                vertex = parents.Get(vertex);
            }
            return vertex;
        }

    public:
        explicit DisjointSets(std::size_t count) : parents(count), sizes(count)
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                parents.Append(i);
                sizes.Append(1);
            }
        }

        bool Unite(std::size_t first, std::size_t second)
        {
            first = Find(first);
            second = Find(second);
            if (first == second) { return false; }
            if (sizes.Get(first) < sizes.Get(second)) { std::swap(first, second); }
            parents.Get(second) = first;
            sizes.Get(first) += sizes.Get(second);
            return true;
        }
    };
}

// Compare must define a strict weak order over the weights in graph.
template<typename Key, typename Weight, typename Compare = std::less<Weight>>
MinimumSpanningTreeResult<Key, Weight> FindMinimumSpanningTree(const Graph<Key, Weight>& graph, Compare compare = {})
{
    struct Candidate
    {
        std::size_t first;
        std::size_t second;
        const Weight* weight;
    };

    MinimumSpanningTreeResult<Key, Weight> result;
    const auto count = graph.GetVertexCount();
    if (count == 0) { return result; }

    result.vertices.Reserve(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        result.vertices.Append(std::make_shared<const Key>(graph.GetVertexKey(i)));
    }

    ArraySequence<Candidate> candidates(graph.GetEdgeCount());
    graph.ForEachWeightedEdge([&](std::size_t first, std::size_t second, const Weight& weight)
    {
        candidates.Append({first, second, &weight});
    });
    if (candidates.GetLength() > 1)
    {
        auto* first = &candidates.Get(0);
        std::sort(first, first + candidates.GetLength(), [&](const Candidate& left, const Candidate& right)
        {
            if (compare(*left.weight, *right.weight)) { return true; }
            if (compare(*right.weight, *left.weight)) { return false; }
            return std::pair{left.first, left.second} < std::pair{right.first, right.second};
        });
    }

    graph_detail::DisjointSets sets(count);
    for (std::size_t i = 0; i < candidates.GetLength() && result.edges.GetLength() < count - 1; ++i)
    {
        const auto& edge = candidates.Get(i);
        if (sets.Unite(edge.first, edge.second))
        {
            result.edges.Append({edge.first, edge.second, *edge.weight});
        }
    }
    if (result.edges.GetLength() == count - 1) { result.status = SpanningTreeStatus::Found; }
    else
    {
        result.status = SpanningTreeStatus::Disconnected;
        result.edges.Clear();
    }
    return result;
}

#endif
