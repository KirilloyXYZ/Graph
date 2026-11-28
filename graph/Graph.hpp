#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <algorithm>
#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../lab1/ArraySequence.hpp"
#include "../lab1/Sequence.hpp"
#include "../lab2/IDictionary.hpp"

template<typename Key, typename Weight = double>
class Graph
{
public:
    using Index = std::size_t;
    struct WeightedEdge
    {
        Key first;
        Key second;
        Weight weight;
    };

    using Dictionary = IDictionary<Key, Index>;

private:
    struct EdgeRecord
    {
        const Index first;
        const Index second;
        Weight weight;

        EdgeRecord(Index first, Index second, const Weight& weight)
            : first(first), second(second), weight(weight) { }

        Index OtherEnd(Index vertex) const noexcept { return vertex == first ? second : first; }
    };

    using EdgeHandle = std::shared_ptr<EdgeRecord>;

    struct Vertex
    {
        const Key key;
        std::unique_ptr<Sequence<EdgeHandle>> edges;

        Vertex(const Key& key, std::unique_ptr<Sequence<EdgeHandle>> edges)
            : key(key), edges(std::move(edges)) { }
    };

    using VertexHandle = std::shared_ptr<Vertex>;

    template<typename T>
    using SequenceFactory = std::unique_ptr<Sequence<T>> (*)();

    SequenceFactory<VertexHandle> createVertices;
    SequenceFactory<EdgeHandle> createEdges;
    std::unique_ptr<Dictionary> indices;
    std::unique_ptr<Sequence<VertexHandle>> vertices;
    std::size_t edgeCount = 0;
    std::size_t topologyVersion = 0;

    template<typename T>
    static std::unique_ptr<Sequence<T>> MakeEmptySequence(SequenceFactory<T> factory);

    const Vertex& GetVertex(Index index) const;
    Index RequireVertex(const Key& key) const;
    static Index FindEdgePosition(const Sequence<EdgeHandle>& edges, Index vertex, Index neighbor);
    EdgeRecord* FindEdgeBetween(Index first, Index second) const;
    EdgeRecord& RequireEdge(const Key& first, const Key& second) const;
    void AppendEdge(Index first, Index second, const Weight& weight);

    void Load(const Sequence<Key>& keys, const Sequence<WeightedEdge>& edges);
    void CopyIncidentEdges(const Graph& source, std::optional<Index> removed = std::nullopt);
    void CheckDuplicateEdges() const;
    void Swap(Graph& other) noexcept;

public:
    Graph(std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
          SequenceFactory<EdgeHandle> createEdges);
    Graph(const Sequence<Key>& keys, const Sequence<WeightedEdge>& edges, std::unique_ptr<Dictionary> dictionary,
          SequenceFactory<VertexHandle> createVertices, SequenceFactory<EdgeHandle> createEdges);
    Graph(const Graph& other);
    Graph(Graph&& other);
    Graph& operator=(const Graph& other);
    Graph& operator=(Graph&& other);

    std::size_t GetVertexCount() const;
    std::size_t GetEdgeCount() const;
    std::size_t GetTopologyVersion() const noexcept;
    std::optional<Index> FindVertexIndex(const Key& key) const;
    bool ContainsVertex(const Key& key) const;
    bool ContainsEdge(const Key& first, const Key& second) const;

    const Key& GetVertexKey(Index index) const;
    std::size_t GetNeighborCount(Index index) const;
    Index GetNeighborIndex(Index index, std::size_t position) const;
    const Weight& GetNeighborWeight(Index index, std::size_t position) const;
    const Weight& GetEdgeWeight(const Key& first, const Key& second) const;

    template<typename Action>
    void ForEachNeighbor(const Key& key, Action action) const;

    template<typename Action>
    void ForEachEdge(Action action) const;

    template<typename Action>
    void ForEachWeightedEdge(Action action) const;

    void AddVertex(const Key& key);
    void RemoveVertex(const Key& key);
    void AddEdge(const Key& first, const Key& second, const Weight& weight);
    void SetEdgeWeight(const Key& first, const Key& second, const Weight& weight);
    void RemoveEdge(const Key& first, const Key& second);
};

template<typename Key, typename Weight>
template<typename T>
std::unique_ptr<Sequence<T>> Graph<Key, Weight>::MakeEmptySequence(SequenceFactory<T> factory)
{
    auto sequence = factory();
    if (sequence == nullptr || sequence->GetLength() != 0)
    {
        throw std::invalid_argument("Graph: a sequence factory must return an empty sequence");
    }
    return sequence;
}

template<typename Key, typename Weight>
Graph<Key, Weight>::Graph(std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
                  SequenceFactory<EdgeHandle> createEdges)
    : createVertices(createVertices), createEdges(createEdges), indices(std::move(dictionary))
{
    if (this->indices == nullptr || this->indices->GetCount() != 0)
    {
        throw std::invalid_argument("Graph: an empty dictionary is required");
    }
    if (createVertices == nullptr || createEdges == nullptr)
    {
        throw std::invalid_argument("Graph: both sequence factories are required");
    }
    this->vertices = MakeEmptySequence(createVertices);
}

template<typename Key, typename Weight>
Graph<Key, Weight>::Graph(const Sequence<Key>& keys, const Sequence<WeightedEdge>& edges,
                          std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
                          SequenceFactory<EdgeHandle> createEdges)
    : Graph(std::move(dictionary), createVertices, createEdges)
{
    this->Load(keys, edges);
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::Load(const Sequence<Key>& keys, const Sequence<WeightedEdge>& edges)
{
    for (Index i = 0; i < keys.GetLength(); ++i) { this->AddVertex(keys.Get(i)); }
    for (Index i = 0; i < edges.GetLength(); ++i)
    {
        const auto& edge = edges.Get(i);
        Index first = this->RequireVertex(edge.first);
        Index second = this->RequireVertex(edge.second);
        if (first == second) { throw std::invalid_argument("Graph::AddEdge: loop"); }
        this->AppendEdge(first, second, edge.weight);
    }
    this->CheckDuplicateEdges();
}

template<typename Key, typename Weight>
Graph<Key, Weight>::Graph(const Graph<Key, Weight>& other)
    : createVertices(other.createVertices), createEdges(other.createEdges),
      indices(other.indices->Clone()), vertices(MakeEmptySequence(other.createVertices))
{
    for (Index i = 0; i < other.GetVertexCount(); ++i)
    {
        this->vertices->Append(std::make_shared<Vertex>(other.GetVertexKey(i), MakeEmptySequence(this->createEdges)));
    }
    this->CopyIncidentEdges(other);
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::CopyIncidentEdges(const Graph& source, std::optional<Index> removed)
{
    const auto remap = [removed](Index index) { return removed && *removed < index ? index - 1 : index; };
    const auto count = this->GetVertexCount();
    auto pending = std::make_unique<ArraySequence<EdgeHandle>[]>(count);
    auto byNeighbor = std::make_unique<EdgeHandle[]>(count);
    std::size_t copiedCount = 0;

    for (Index u = 0; u < source.GetVertexCount(); ++u)
    {
        if (removed == u) { continue; }
        const Index target = remap(u);
        auto& incoming = pending[target];
        // The lower endpoint has already created each incoming edge.
        for (Index i = 0; i < incoming.GetLength(); ++i)
        {
            auto& edge = incoming.Get(i);
            const Index first = edge->first;
            byNeighbor[first] = std::move(edge);
        }
        incoming.Clear();

        const Sequence<EdgeHandle>& sourceEdges = *source.GetVertex(u).edges;
        auto& targetEdges = *this->GetVertex(target).edges;
        for (Index i = 0; i < sourceEdges.GetLength(); ++i)
        {
            const EdgeRecord& edge = *sourceEdges.Get(i);
            const Index neighbor = edge.OtherEnd(u);
            if (removed == neighbor) { continue; }
            const Index targetNeighbor = remap(neighbor);
            if (neighbor < u)
            {
                auto& copy = byNeighbor[targetNeighbor];
                targetEdges.Append(copy);
                copy.reset();
            }
            else
            {
                auto copy = std::make_shared<EdgeRecord>(target, targetNeighbor, edge.weight);
                targetEdges.Append(copy);
                pending[targetNeighbor].Append(copy);
                ++copiedCount;
            }
        }
    }
    this->edgeCount = copiedCount;
}

template<typename Key, typename Weight>
Graph<Key, Weight>::Graph(Graph<Key, Weight>&& other)
    : Graph(other.indices->CreateEmpty(), other.createVertices, other.createEdges)
{
    this->Swap(other);
    ++other.topologyVersion;
}

template<typename Key, typename Weight>
Graph<Key, Weight>& Graph<Key, Weight>::operator=(const Graph<Key, Weight>& other)
{
    if (this != &other)
    {
        Graph<Key, Weight> copy(other);
        this->Swap(copy);
        ++this->topologyVersion;
    }
    return *this;
}

template<typename Key, typename Weight>
Graph<Key, Weight>& Graph<Key, Weight>::operator=(Graph<Key, Weight>&& other)
{
    if (this != &other)
    {
        Graph<Key, Weight> moved(std::move(other));
        this->Swap(moved);
        ++this->topologyVersion;
    }
    return *this;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::Swap(Graph<Key, Weight>& other) noexcept
{
    std::swap(this->createVertices, other.createVertices);
    std::swap(this->createEdges, other.createEdges);
    this->indices.swap(other.indices);
    this->vertices.swap(other.vertices);
    std::swap(this->edgeCount, other.edgeCount);
}

template<typename Key, typename Weight>
std::size_t Graph<Key, Weight>::GetVertexCount() const { return this->vertices->GetLength(); }

template<typename Key, typename Weight>
std::size_t Graph<Key, Weight>::GetEdgeCount() const { return this->edgeCount; }

template<typename Key, typename Weight>
std::size_t Graph<Key, Weight>::GetTopologyVersion() const noexcept { return this->topologyVersion; }

template<typename Key, typename Weight>
std::optional<typename Graph<Key, Weight>::Index> Graph<Key, Weight>::FindVertexIndex(const Key& key) const
{
    const Index* found = this->indices->Find(key);
    if (found == nullptr) { return std::nullopt; }
    return *found;
}

template<typename Key, typename Weight>
bool Graph<Key, Weight>::ContainsVertex(const Key& key) const { return this->FindVertexIndex(key).has_value(); }

template<typename Key, typename Weight>
typename Graph<Key, Weight>::Index Graph<Key, Weight>::RequireVertex(const Key& key) const
{
    auto index = this->FindVertexIndex(key);
    if (!index) { throw std::out_of_range("Graph: vertex not found"); }
    return *index;
}

template<typename Key, typename Weight>
const typename Graph<Key, Weight>::Vertex& Graph<Key, Weight>::GetVertex(Index index) const
{
    return *this->vertices->Get(index);
}

template<typename Key, typename Weight>
const Key& Graph<Key, Weight>::GetVertexKey(Index index) const { return this->GetVertex(index).key; }

template<typename Key, typename Weight>
std::size_t Graph<Key, Weight>::GetNeighborCount(Index index) const
{
    return this->GetVertex(index).edges->GetLength();
}

template<typename Key, typename Weight>
typename Graph<Key, Weight>::Index Graph<Key, Weight>::GetNeighborIndex(Index index, std::size_t position) const
{
    return this->GetVertex(index).edges->Get(position)->OtherEnd(index);
}

template<typename Key, typename Weight>
const Weight& Graph<Key, Weight>::GetNeighborWeight(Index index, std::size_t position) const
{
    return this->GetVertex(index).edges->Get(position)->weight;
}

template<typename Key, typename Weight>
const Weight& Graph<Key, Weight>::GetEdgeWeight(const Key& first, const Key& second) const
{
    return this->RequireEdge(first, second).weight;
}

template<typename Key, typename Weight>
auto Graph<Key, Weight>::FindEdgePosition(const Sequence<EdgeHandle>& edges, Index vertex, Index neighbor) -> Index
{
    Index count = edges.GetLength();
    for (Index i = 0; i < count; ++i)
    {
        if (edges.Get(i)->OtherEnd(vertex) == neighbor) { return i; }
    }
    return count;
}

template<typename Key, typename Weight>
auto Graph<Key, Weight>::FindEdgeBetween(Index first, Index second) const -> EdgeRecord*
{
    if (this->GetNeighborCount(first) > this->GetNeighborCount(second)) { std::swap(first, second); }
    const auto& edges = *this->GetVertex(first).edges;
    Index position = FindEdgePosition(edges, first, second);
    return position == edges.GetLength() ? nullptr : edges.Get(position).get();
}

template<typename Key, typename Weight>
auto Graph<Key, Weight>::RequireEdge(const Key& first, const Key& second) const -> EdgeRecord&
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    auto* edge = this->FindEdgeBetween(u, v);
    if (edge == nullptr) { throw std::out_of_range("Graph: edge not found"); }
    return *edge;
}

template<typename Key, typename Weight>
bool Graph<Key, Weight>::ContainsEdge(const Key& first, const Key& second) const
{
    auto u = this->FindVertexIndex(first);
    auto v = this->FindVertexIndex(second);
    return u && v && *u != *v && this->FindEdgeBetween(*u, *v);
}

template<typename Key, typename Weight>
template<typename Action>
void Graph<Key, Weight>::ForEachNeighbor(const Key& key, Action action) const
{
    Index vertex = this->RequireVertex(key);
    const auto& edges = *this->GetVertex(vertex).edges;
    for (Index i = 0; i < edges.GetLength(); ++i)
    {
        action(this->GetVertexKey(edges.Get(i)->OtherEnd(vertex)));
    }
}

template<typename Key, typename Weight>
template<typename Action>
void Graph<Key, Weight>::ForEachEdge(Action action) const
{
    this->ForEachWeightedEdge([&](Index u, Index v, const Weight&)
    {
        action(this->GetVertexKey(u), this->GetVertexKey(v));
    });
}

template<typename Key, typename Weight>
template<typename Action>
void Graph<Key, Weight>::ForEachWeightedEdge(Action action) const
{
    for (Index u = 0; u < this->GetVertexCount(); ++u)
    {
        const Vertex& vertex = this->GetVertex(u);
        for (Index i = 0; i < vertex.edges->GetLength(); ++i)
        {
            const EdgeRecord& edge = *vertex.edges->Get(i);
            if (u == edge.first) { action(edge.first, edge.second, edge.weight); }
        }
    }
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::AddVertex(const Key& key)
{
    if (this->ContainsVertex(key)) { throw std::invalid_argument("Graph::AddVertex: duplicate key"); }

    Index index = this->GetVertexCount();
    auto vertex = std::make_shared<Vertex>(key, MakeEmptySequence(this->createEdges));
    this->vertices->Append(vertex);
    try { this->indices->Add(vertex->key, index); }
    catch (...)
    {
        // Последняя запись удаляется без выделения памяти.
        this->vertices->RemoveAt(index);
        throw;
    }
    ++this->topologyVersion;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::AppendEdge(Index first, Index second, const Weight& weight)
{
    if (this->edgeCount == std::numeric_limits<std::size_t>::max())
    {
        throw std::length_error("Graph::AddEdge: too many edges");
    }
    auto& left = *this->GetVertex(first).edges;
    auto& right = *this->GetVertex(second).edges;
    Index position = left.GetLength();
    auto [u, v] = std::minmax(first, second);
    auto edge = std::make_shared<EdgeRecord>(u, v, weight);
    left.Append(edge);
    try { right.Append(edge); }
    catch (...)
    {
        left.RemoveAt(position);
        throw;
    }
    ++this->edgeCount;
    ++this->topologyVersion;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::AddEdge(const Key& first, const Key& second, const Weight& weight)
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    if (u == v) { throw std::invalid_argument("Graph::AddEdge: loop"); }

    if (this->FindEdgeBetween(u, v))
    {
        throw std::invalid_argument("Graph::AddEdge: duplicate edge");
    }
    this->AppendEdge(u, v, weight);
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::SetEdgeWeight(const Key& first, const Key& second, const Weight& weight)
{
    auto& edge = this->RequireEdge(first, second);
    Weight updated(weight);
    if constexpr (std::is_nothrow_copy_assignable_v<Weight>)
    {
        edge.weight = std::as_const(updated);
    }
    else
    {
        static_assert(std::is_nothrow_swappable_v<Weight>,
                      "SetEdgeWeight requires nonthrowing copy assignment or swap");
        using std::swap;
        swap(edge.weight, updated);
    }
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::RemoveEdge(const Key& first, const Key& second)
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    auto& left = *this->GetVertex(u).edges;
    auto& right = *this->GetVertex(v).edges;
    Index leftPosition = FindEdgePosition(left, u, v);
    Index rightPosition = FindEdgePosition(right, v, u);
    if (leftPosition == left.GetLength() || rightPosition == right.GetLength())
    {
        throw std::out_of_range("Graph: edge not found");
    }

    left.RemoveAt(leftPosition);
    right.RemoveAt(rightPosition);
    --this->edgeCount;
    ++this->topologyVersion;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::RemoveVertex(const Key& key)
{
    Index removed = this->RequireVertex(key);
    Graph<Key, Weight> replacement(this->indices->CreateEmpty(), this->createVertices, this->createEdges);
    for (Index i = 0; i < this->GetVertexCount(); ++i)
    {
        if (i != removed) { replacement.AddVertex(this->GetVertexKey(i)); }
    }
    replacement.CopyIncidentEdges(*this, removed);
    this->Swap(replacement);
    ++this->topologyVersion;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::CheckDuplicateEdges() const
{
    if (this->edgeCount == 0) { return; }
    Index count = this->GetVertexCount();
    // Для строки u значение u означает, что индекс соседа уже встречался в этой строке.
    std::unique_ptr<Index[]> marks(new Index[count]);
    for (Index i = 0; i < count; ++i) { marks[i] = count; }
    for (Index u = 0; u < count; ++u)
    {
        const auto& edges = *this->GetVertex(u).edges;
        for (Index i = 0; i < edges.GetLength(); ++i)
        {
            Index v = edges.Get(i)->OtherEnd(u);
            if (marks[v] == u) { throw std::invalid_argument("Graph::AddEdge: duplicate edge"); }
            marks[v] = u;
        }
    }
}

#endif
