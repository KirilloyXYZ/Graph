#ifndef GRAPHTRAVERSAL_HPP
#define GRAPHTRAVERSAL_HPP

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>

#include "Graph.hpp"
#include "../lab1/ArraySequence.hpp"
#include "../lab1/IEnumerator.hpp"
#include "../lab1/Stack.hpp"

struct TraversalStep
{
    std::size_t vertex;
    std::optional<std::size_t> parent;
};

using TraversalMarks = ArraySequence<bool>;

template<typename Key, typename Weight = double>
using TraversalFactory = std::unique_ptr<IEnumerator<TraversalStep, Key>> (*) (const Graph<Key, Weight>&, std::size_t, TraversalMarks&);

namespace graph_detail
{
    template<typename Key, typename Weight>
    class TraversalEnumerator : public IEnumerator<TraversalStep, Key>
    {
    protected:
        const Graph<Key, Weight>& graph;
        TraversalMarks& marks;
        std::size_t start;
        const std::size_t expectedVersion;
        bool starting = true;
        std::optional<TraversalStep> current;

        void CheckVersion() const
        {
            if (graph.GetTopologyVersion() != expectedVersion)
            {
                throw std::logic_error("Enumerator: graph topology changed");
            }
        }

        bool Discover(std::size_t vertex, std::optional<std::size_t> parent)
        {
            marks.Get(vertex) = true;
            current = TraversalStep{vertex, parent};
            return true;
        }

    public:
        TraversalEnumerator(const Graph<Key, Weight>& graph, std::size_t start, TraversalMarks& marks)
            : graph(graph), marks(marks), start(start), expectedVersion(graph.GetTopologyVersion())
        {
            if (start >= graph.GetVertexCount()) { throw std::out_of_range("Enumerator: start vertex not found"); }
            if (marks.GetLength() != graph.GetVertexCount() || marks.Get(start))
            {
                throw std::invalid_argument("Enumerator: invalid traversal marks");
            }
        }

        const TraversalStep& GetCurrent() const override
        {
            CheckVersion();
            if (!current) { throw std::out_of_range("Enumerator: no current element"); }
            return *current;
        }

        const Key& GetCurrentKey() const override { return graph.GetVertexKey(GetCurrent().vertex); }
    };
}

template<typename Key, typename Weight = double>
class BFSEnumerator final : public graph_detail::TraversalEnumerator<Key, Weight>
{
private:
    ArraySequence<std::size_t> pending;
    std::size_t head = 0;
    std::size_t nextNeighbor = 0;

public:
    using graph_detail::TraversalEnumerator<Key, Weight>::TraversalEnumerator;

    bool MoveNext() override
    {
        this->CheckVersion();
        this->current.reset();
        if (this->starting)
        {
            pending.Append(this->start);
            this->starting = false;
            return this->Discover(this->start, std::nullopt);
        }
        while (head < pending.GetLength())
        {
            std::size_t parent = pending.Get(head);
            if (nextNeighbor == this->graph.GetNeighborCount(parent))
            {
                ++head;
                nextNeighbor = 0;
                continue;
            }
            std::size_t neighbor = this->graph.GetNeighborIndex(parent, nextNeighbor);
            if (!this->marks.Get(neighbor))
            {
                pending.Append(neighbor);
                ++nextNeighbor;
                return this->Discover(neighbor, parent);
            }
            ++nextNeighbor;
        }
        return false;
    }
};

template<typename Key, typename Weight = double>
class DFSEnumerator final : public graph_detail::TraversalEnumerator<Key, Weight>
{
private:
    struct Frame
    {
        std::size_t vertex;
        std::size_t nextNeighbor;
    };

    Stack<Frame> path;

public:
    using graph_detail::TraversalEnumerator<Key, Weight>::TraversalEnumerator;

    bool MoveNext() override
    {
        this->CheckVersion();
        this->current.reset();
        if (this->starting)
        {
            path.Push({this->start, 0});
            this->starting = false;
            return this->Discover(this->start, std::nullopt);
        }
        while (!path.IsEmpty())
        {
            Frame& frame = path.Top();
            if (frame.nextNeighbor == this->graph.GetNeighborCount(frame.vertex))
            {
                path.Pop();
                continue;
            }
            std::size_t neighbor = this->graph.GetNeighborIndex(frame.vertex, frame.nextNeighbor);
            if (!this->marks.Get(neighbor))
            {
                path.Push({neighbor, 0});
                ++frame.nextNeighbor;
                return this->Discover(neighbor, frame.vertex);
            }
            ++frame.nextNeighbor;
        }
        return false;
    }
};

template<typename Key, typename Weight = double>
std::unique_ptr<IEnumerator<TraversalStep, Key>> CreateBFSEnumerator(
    const Graph<Key, Weight>& graph, std::size_t start, TraversalMarks& marks)
{
    return std::make_unique<BFSEnumerator<Key, Weight>>(graph, start, marks);
}

template<typename Key, typename Weight = double>
std::unique_ptr<IEnumerator<TraversalStep, Key>> CreateDFSEnumerator(
    const Graph<Key, Weight>& graph, std::size_t start, TraversalMarks& marks)
{
    return std::make_unique<DFSEnumerator<Key, Weight>>(graph, start, marks);
}

#endif
