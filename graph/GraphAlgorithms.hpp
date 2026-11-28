#ifndef GRAPHALGORITHMS_HPP
#define GRAPHALGORITHMS_HPP

#include <algorithm>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "GraphTraversal.hpp"

enum class SpanningTreeStatus { Found, Empty, Disconnected };

template<typename Key>
using TraversalKeys = ArraySequence<std::shared_ptr<const Key>>;

template<typename Key>
struct SpanningTreeResult
{
    SpanningTreeStatus status = SpanningTreeStatus::Empty;
    TraversalKeys<Key> order;
    ArraySequence<std::pair<std::size_t, std::size_t>> edges;
};

template<typename Key>
struct ConnectedComponentsResult
{
    TraversalKeys<Key> order;
    ArraySequence<std::size_t> componentEnds;
};

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

    inline TraversalMarks CreateMarks(std::size_t count)
    {
        TraversalMarks marks(count);
        for (std::size_t i = 0; i < count; ++i) { marks.Append(false); }
        return marks;
    }

    inline ArraySequence<std::size_t> CreatePositions(std::size_t count)
    {
        ArraySequence<std::size_t> positions(count);
        for (std::size_t i = 0; i < count; ++i) { positions.Append(count); }
        return positions;
    }

    template<typename Key, typename Weight>
    void Visit(const Graph<Key, Weight>& graph, std::size_t start, TraversalFactory<Key, Weight> create,
               TraversalMarks& marks, TraversalKeys<Key>& order, SpanningTreeResult<Key>* tree = nullptr,
               ArraySequence<std::size_t>* positions = nullptr)
    {
        auto enumerator = create(graph, start, marks);
        if (!enumerator) { throw std::invalid_argument("Traversal factory returned no enumerator"); }
        while (enumerator->MoveNext())
        {
            std::size_t position = order.GetLength();
            order.Append(std::make_shared<const Key>(enumerator->GetCurrentKey()));
            if (tree)
            {
                const auto& step = enumerator->GetCurrent();
                positions->Get(step.vertex) = position;
                if (step.parent) { tree->edges.Append({positions->Get(*step.parent), position}); }
            }
        }
    }
}

template<typename Key, typename Weight>
TraversalKeys<Key> Traverse(const Graph<Key, Weight>& graph, std::size_t start, TraversalFactory<Key, Weight> create)
{
    if (!create) { throw std::invalid_argument("Traverse: null traversal factory"); }
    if (start >= graph.GetVertexCount()) { throw std::out_of_range("Traverse: start vertex not found"); }

    TraversalKeys<Key> order;
    auto marks = graph_detail::CreateMarks(graph.GetVertexCount());
    graph_detail::Visit(graph, start, create, marks, order);
    return order;
}

template<typename Key, typename Weight>
SpanningTreeResult<Key> FindSpanningTree(const Graph<Key, Weight>& graph, std::optional<std::size_t> start,
                                        TraversalFactory<Key, Weight> create)
{
    if (!create) { throw std::invalid_argument("FindSpanningTree: null traversal factory"); }
    SpanningTreeResult<Key> result;
    std::size_t count = graph.GetVertexCount();
    if (count == 0) { return result; }
    if (!start || *start >= count) { throw std::out_of_range("FindSpanningTree: start vertex not found"); }

    auto marks = graph_detail::CreateMarks(count);
    auto positions = graph_detail::CreatePositions(count);
    graph_detail::Visit(graph, *start, create, marks, result.order, &result, &positions);

    if (result.order.GetLength() == count) { result.status = SpanningTreeStatus::Found; }
    else
    {
        result.status = SpanningTreeStatus::Disconnected;
        result.edges.Clear();
    }
    return result;
}

template<typename Key, typename Weight>
ConnectedComponentsResult<Key> FindConnectedComponents(const Graph<Key, Weight>& graph,
                                                        TraversalFactory<Key, Weight> create)
{
    if (!create) { throw std::invalid_argument("FindConnectedComponents: null traversal factory"); }
    ConnectedComponentsResult<Key> result;
    std::size_t count = graph.GetVertexCount();
    auto marks = graph_detail::CreateMarks(count);

    for (std::size_t start = 0; start < count; ++start)
    {
        if (marks.Get(start)) { continue; }
        graph_detail::Visit(graph, start, create, marks, result.order);
        result.componentEnds.Append(result.order.GetLength());
    }
    return result;
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
