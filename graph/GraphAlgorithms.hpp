#ifndef GRAPHALGORITHMS_HPP
#define GRAPHALGORITHMS_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "Graph.hpp"
#include "../lab1/ArraySequence.hpp"
#include "../lab1/Stack.hpp"

enum class TraversalMethod { BreadthFirst, DepthFirst };
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

namespace graph_detail
{
    class IndexQueue
    {
    private:
        Sequence<std::size_t>& items;
        std::size_t head = 0;

    public:
        explicit IndexQueue(Sequence<std::size_t>& items) : items(items) { }

        void Push(std::size_t value) { this->items.Append(value); }
        std::size_t Front() const { return this->items.Get(this->head); }
        bool IsEmpty() const { return this->head == this->items.GetLength(); }
        void Pop()
        {
            if (this->IsEmpty()) { throw std::out_of_range("IndexQueue::Pop: queue is empty"); }
            ++this->head;
        }
    };

    inline void CheckMethod(TraversalMethod method)
    {
        if (method != TraversalMethod::BreadthFirst && method != TraversalMethod::DepthFirst)
        {
            throw std::invalid_argument("Unknown traversal method");
        }
    }

    inline ArraySequence<std::size_t> CreatePositions(std::size_t count)
    {
        ArraySequence<std::size_t> positions(count);
        for (std::size_t i = 0; i < count; ++i) { positions.Append(count); }
        return positions;
    }

    template<typename Key, typename Discover>
    void VisitBreadthFirst(const Graph<Key>& graph, std::size_t start, const Sequence<std::size_t>& positions,
                           IndexQueue& queue, const Discover& discover)
    {
        queue.Push(start);
        std::size_t missing = graph.GetVertexCount();
        while (!queue.IsEmpty())
        {
            std::size_t current = queue.Front();
            queue.Pop();
            std::size_t degree = graph.GetNeighborCount(current);
            for (std::size_t position = 0; position < degree; ++position)
            {
                std::size_t neighbor = graph.GetNeighborIndex(current, position);
                if (positions.Get(neighbor) == missing)
                {
                    discover(neighbor, current);
                    queue.Push(neighbor);
                }
            }
        }
    }

    template<typename Key, typename Discover>
    void VisitDepthFirst(const Graph<Key>& graph, std::size_t start, const Sequence<std::size_t>& positions,
                         const Discover& discover)
    {
        struct Frame
        {
            std::size_t vertex;
            std::size_t nextNeighbor;
        };

        Stack<Frame> path;
        path.Push({start, 0});
        std::size_t missing = graph.GetVertexCount();
        while (!path.IsEmpty())
        {
            Frame& current = path.Top();
            if (current.nextNeighbor == graph.GetNeighborCount(current.vertex))
            {
                path.Pop();
                continue;
            }
            std::size_t neighbor = graph.GetNeighborIndex(current.vertex, current.nextNeighbor++);
            if (positions.Get(neighbor) == missing)
            {
                discover(neighbor, current.vertex);
                path.Push({neighbor, 0});
            }
        }
    }

    template<typename Key>
    void Visit(const Graph<Key>& graph, std::size_t start, TraversalMethod method,
               ArraySequence<std::size_t>& positions, IndexQueue& queue, TraversalKeys<Key>& order,
               ArraySequence<std::pair<std::size_t, std::size_t>>* edges = nullptr)
    {
        auto discover = [&](std::size_t vertex, std::optional<std::size_t> parent)
        {
            std::size_t position = order.GetLength();
            positions.Get(vertex) = position;
            order.Append(std::make_shared<const Key>(graph.GetVertexKey(vertex)));
            if (edges != nullptr && parent) { edges->Append({positions.Get(*parent), position}); }
        };

        discover(start, std::nullopt);
        if (method == TraversalMethod::BreadthFirst)
        {
            VisitBreadthFirst(graph, start, positions, queue, discover);
        }
        else
        {
            VisitDepthFirst(graph, start, positions, discover);
        }
    }
}

template<typename Key>
TraversalKeys<Key> Traverse(const Graph<Key>& graph, std::size_t start, TraversalMethod method)
{
    graph_detail::CheckMethod(method);
    if (start >= graph.GetVertexCount()) { throw std::out_of_range("Traverse: start vertex not found"); }

    TraversalKeys<Key> order;
    auto positions = graph_detail::CreatePositions(graph.GetVertexCount());
    ArraySequence<std::size_t> pending;
    graph_detail::IndexQueue queue(pending);
    graph_detail::Visit(graph, start, method, positions, queue, order);
    return order;
}

template<typename Key>
SpanningTreeResult<Key> FindSpanningTree(const Graph<Key>& graph, std::optional<std::size_t> start,
                                        TraversalMethod method)
{
    graph_detail::CheckMethod(method);
    SpanningTreeResult<Key> result;
    std::size_t count = graph.GetVertexCount();
    if (count == 0) { return result; }
    if (!start || *start >= count) { throw std::out_of_range("FindSpanningTree: start vertex not found"); }

    auto positions = graph_detail::CreatePositions(count);
    ArraySequence<std::size_t> pending;
    graph_detail::IndexQueue queue(pending);
    graph_detail::Visit(graph, *start, method, positions, queue, result.order, &result.edges);

    if (result.order.GetLength() == count) { result.status = SpanningTreeStatus::Found; }
    else
    {
        result.status = SpanningTreeStatus::Disconnected;
        result.edges.Clear();
    }
    return result;
}

template<typename Key>
ConnectedComponentsResult<Key> FindConnectedComponents(const Graph<Key>& graph, TraversalMethod method)
{
    graph_detail::CheckMethod(method);
    ConnectedComponentsResult<Key> result;
    std::size_t count = graph.GetVertexCount();
    auto positions = graph_detail::CreatePositions(count);
    ArraySequence<std::size_t> pending;
    graph_detail::IndexQueue queue(pending);

    // Очередь полностью прочитана после каждой компоненты. За весь поиск
    // в неё добавляется не более одной записи на вершину.
    for (std::size_t start = 0; start < count; ++start)
    {
        if (positions.Get(start) != count) { continue; }
        graph_detail::Visit(graph, start, method, positions, queue, result.order);
        result.componentEnds.Append(result.order.GetLength());
    }
    return result;
}

#endif
