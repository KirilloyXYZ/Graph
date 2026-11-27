#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../lab1/Sequence.hpp"
#include "../lab2/IDictionary.hpp"

template<typename Key, typename Weight = double>
class Graph
{
public:
    using Index = std::size_t;
    using Edge = std::pair<Key, Key>;
    struct WeightedEdge
    {
        Key first;
        Key second;
        Weight weight;
    };

    using Dictionary = IDictionary<Key, Index>;

private:
    struct Neighbor
    {
        Index index;
        // Sequence exposes elements by const reference. Only Graph::SetEdgeWeight edits this field.
        mutable Weight weight;
    };

    struct Vertex
    {
        const Key key;
        std::unique_ptr<Sequence<Neighbor>> neighbors;

        Vertex(const Key& key, std::unique_ptr<Sequence<Neighbor>> neighbors)
            : key(key), neighbors(std::move(neighbors)) { }
    };

    using VertexHandle = std::shared_ptr<Vertex>;

    template<typename T>
    using SequenceFactory = std::unique_ptr<Sequence<T>> (*)();

    SequenceFactory<VertexHandle> createVertices;
    SequenceFactory<Neighbor> createNeighbors;
    std::unique_ptr<Dictionary> indices;
    std::unique_ptr<Sequence<VertexHandle>> vertices;
    std::size_t edgeCount = 0;

    template<typename T>
    static std::unique_ptr<Sequence<T>> MakeEmptySequence(SequenceFactory<T> factory);

    const Vertex& GetVertex(Index index) const;
    Index RequireVertex(const Key& key) const;
    static Index FindNeighbor(const Sequence<Neighbor>& neighbors, Index neighbor);
    bool HasEdgeBetween(Index first, Index second) const;
    void AppendEdge(Index first, Index second, const Weight& weight);
    std::pair<Index, Index> RequireEdgePositions(Index first, Index second) const;

    template<typename InputEdge, typename WeightOf>
    void Load(const Sequence<Key>& keys, const Sequence<InputEdge>& edges, WeightOf weightOf);
    void CheckDuplicateEdges() const;
    void Swap(Graph& other) noexcept;

public:
    Graph(std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
          SequenceFactory<Neighbor> createNeighbors);
    template<typename W = Weight, std::enable_if_t<std::is_constructible_v<W, int>, int> = 0>
    Graph(const Sequence<Key>& keys, const Sequence<Edge>& edges, std::unique_ptr<Dictionary> dictionary,
          SequenceFactory<VertexHandle> createVertices, SequenceFactory<Neighbor> createNeighbors);
    Graph(const Sequence<Key>& keys, const Sequence<WeightedEdge>& edges, std::unique_ptr<Dictionary> dictionary,
          SequenceFactory<VertexHandle> createVertices, SequenceFactory<Neighbor> createNeighbors);
    Graph(const Graph& other);
    Graph(Graph&& other);
    Graph& operator=(const Graph& other);
    Graph& operator=(Graph&& other);

    std::size_t GetVertexCount() const;
    std::size_t GetEdgeCount() const;
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
    template<typename W = Weight, std::enable_if_t<std::is_same_v<W, Weight> && std::is_constructible_v<W, int>, int> = 0>
    void AddEdge(const Key& first, const Key& second);
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
                  SequenceFactory<Neighbor> createNeighbors)
    : createVertices(createVertices), createNeighbors(createNeighbors), indices(std::move(dictionary))
{
    if (this->indices == nullptr || this->indices->GetCount() != 0)
    {
        throw std::invalid_argument("Graph: an empty dictionary is required");
    }
    if (createVertices == nullptr || createNeighbors == nullptr)
    {
        throw std::invalid_argument("Graph: both sequence factories are required");
    }
    this->vertices = MakeEmptySequence(createVertices);
}

template<typename Key, typename Weight>
template<typename W, std::enable_if_t<std::is_constructible_v<W, int>, int>>
Graph<Key, Weight>::Graph(const Sequence<Key>& keys, const Sequence<Edge>& edges,
                          std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
                          SequenceFactory<Neighbor> createNeighbors)
    : Graph(std::move(dictionary), createVertices, createNeighbors)
{
    this->Load(keys, edges, [](const Edge&) { return Weight(1); });
}

template<typename Key, typename Weight>
Graph<Key, Weight>::Graph(const Sequence<Key>& keys, const Sequence<WeightedEdge>& edges,
                          std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
                          SequenceFactory<Neighbor> createNeighbors)
    : Graph(std::move(dictionary), createVertices, createNeighbors)
{
    this->Load(keys, edges, [](const WeightedEdge& edge) -> const Weight& { return edge.weight; });
}

template<typename Key, typename Weight>
template<typename InputEdge, typename WeightOf>
void Graph<Key, Weight>::Load(const Sequence<Key>& keys, const Sequence<InputEdge>& edges, WeightOf weightOf)
{
    for (Index i = 0; i < keys.GetLength(); ++i) { this->AddVertex(keys.Get(i)); }
    for (Index i = 0; i < edges.GetLength(); ++i)
    {
        const auto& edge = edges.Get(i);
        Index first = this->RequireVertex(edge.first);
        Index second = this->RequireVertex(edge.second);
        if (first == second) { throw std::invalid_argument("Graph::AddEdge: loop"); }
        this->AppendEdge(first, second, weightOf(edge));
    }
    this->CheckDuplicateEdges();
}

template<typename Key, typename Weight>
Graph<Key, Weight>::Graph(const Graph<Key, Weight>& other)
    : createVertices(other.createVertices), createNeighbors(other.createNeighbors),
      indices(other.indices->Clone()), vertices(MakeEmptySequence(other.createVertices)),
      edgeCount(other.edgeCount)
{
    for (Index i = 0; i < other.GetVertexCount(); ++i)
    {
        const Vertex& source = other.GetVertex(i);
        auto neighbors = MakeEmptySequence(this->createNeighbors);
        for (Index j = 0; j < source.neighbors->GetLength(); ++j)
        {
            neighbors->Append(source.neighbors->Get(j));
        }
        this->vertices->Append(std::make_shared<Vertex>(source.key, std::move(neighbors)));
    }
}

template<typename Key, typename Weight>
Graph<Key, Weight>::Graph(Graph<Key, Weight>&& other)
    : Graph(other.indices->CreateEmpty(), other.createVertices, other.createNeighbors)
{
    this->Swap(other);
}

template<typename Key, typename Weight>
Graph<Key, Weight>& Graph<Key, Weight>::operator=(const Graph<Key, Weight>& other)
{
    if (this != &other)
    {
        Graph<Key, Weight> copy(other);
        this->Swap(copy);
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
    }
    return *this;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::Swap(Graph<Key, Weight>& other) noexcept
{
    std::swap(this->createVertices, other.createVertices);
    std::swap(this->createNeighbors, other.createNeighbors);
    this->indices.swap(other.indices);
    this->vertices.swap(other.vertices);
    std::swap(this->edgeCount, other.edgeCount);
}

template<typename Key, typename Weight>
std::size_t Graph<Key, Weight>::GetVertexCount() const { return this->vertices->GetLength(); }

template<typename Key, typename Weight>
std::size_t Graph<Key, Weight>::GetEdgeCount() const { return this->edgeCount; }

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
    return this->GetVertex(index).neighbors->GetLength();
}

template<typename Key, typename Weight>
typename Graph<Key, Weight>::Index Graph<Key, Weight>::GetNeighborIndex(Index index, std::size_t position) const
{
    return this->GetVertex(index).neighbors->Get(position).index;
}

template<typename Key, typename Weight>
const Weight& Graph<Key, Weight>::GetNeighborWeight(Index index, std::size_t position) const
{
    return this->GetVertex(index).neighbors->Get(position).weight;
}

template<typename Key, typename Weight>
const Weight& Graph<Key, Weight>::GetEdgeWeight(const Key& first, const Key& second) const
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    if (this->GetNeighborCount(u) > this->GetNeighborCount(v)) { std::swap(u, v); }
    const auto& neighbors = *this->GetVertex(u).neighbors;
    Index position = FindNeighbor(neighbors, v);
    if (position == neighbors.GetLength()) { throw std::out_of_range("Graph: edge not found"); }
    return neighbors.Get(position).weight;
}

template<typename Key, typename Weight>
typename Graph<Key, Weight>::Index Graph<Key, Weight>::FindNeighbor(const Sequence<Neighbor>& neighbors, Index neighbor)
{
    Index count = neighbors.GetLength();
    for (Index i = 0; i < count; ++i)
    {
        if (neighbors.Get(i).index == neighbor) { return i; }
    }
    return count;
}

template<typename Key, typename Weight>
bool Graph<Key, Weight>::HasEdgeBetween(Index first, Index second) const
{
    if (this->GetNeighborCount(first) > this->GetNeighborCount(second)) { std::swap(first, second); }
    const auto& neighbors = *this->GetVertex(first).neighbors;
    return FindNeighbor(neighbors, second) != neighbors.GetLength();
}

template<typename Key, typename Weight>
bool Graph<Key, Weight>::ContainsEdge(const Key& first, const Key& second) const
{
    auto u = this->FindVertexIndex(first);
    auto v = this->FindVertexIndex(second);
    return u && v && *u != *v && this->HasEdgeBetween(*u, *v);
}

template<typename Key, typename Weight>
template<typename Action>
void Graph<Key, Weight>::ForEachNeighbor(const Key& key, Action action) const
{
    const Vertex& vertex = this->GetVertex(this->RequireVertex(key));
    for (Index i = 0; i < vertex.neighbors->GetLength(); ++i)
    {
        action(this->GetVertexKey(vertex.neighbors->Get(i).index));
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
        for (Index i = 0; i < vertex.neighbors->GetLength(); ++i)
        {
            const auto& neighbor = vertex.neighbors->Get(i);
            if (u < neighbor.index)
            {
                const Weight& weight = neighbor.weight;
                action(u, neighbor.index, weight);
            }
        }
    }
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::AddVertex(const Key& key)
{
    if (this->ContainsVertex(key)) { throw std::invalid_argument("Graph::AddVertex: duplicate key"); }

    Index index = this->GetVertexCount();
    auto vertex = std::make_shared<Vertex>(key, MakeEmptySequence(this->createNeighbors));
    this->vertices->Append(vertex);
    try { this->indices->Add(vertex->key, index); }
    catch (...)
    {
        // Последняя запись удаляется без выделения памяти.
        this->vertices->RemoveAt(index);
        throw;
    }
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::AppendEdge(Index first, Index second, const Weight& weight)
{
    if (this->edgeCount == std::numeric_limits<std::size_t>::max())
    {
        throw std::length_error("Graph::AddEdge: too many edges");
    }
    auto& left = *this->GetVertex(first).neighbors;
    auto& right = *this->GetVertex(second).neighbors;
    Index position = left.GetLength();
    const Neighbor leftNeighbor{second, weight};
    const Neighbor rightNeighbor{first, weight};
    left.Append(leftNeighbor);
    try { right.Append(rightNeighbor); }
    catch (...)
    {
        left.RemoveAt(position);
        throw;
    }
    ++this->edgeCount;
}

template<typename Key, typename Weight>
template<typename W, std::enable_if_t<std::is_same_v<W, Weight> && std::is_constructible_v<W, int>, int>>
void Graph<Key, Weight>::AddEdge(const Key& first, const Key& second)
{
    this->AddEdge(first, second, Weight(1));
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::AddEdge(const Key& first, const Key& second, const Weight& weight)
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    if (u == v) { throw std::invalid_argument("Graph::AddEdge: loop"); }

    if (this->HasEdgeBetween(u, v))
    {
        throw std::invalid_argument("Graph::AddEdge: duplicate edge");
    }
    this->AppendEdge(u, v, weight);
}

template<typename Key, typename Weight>
auto Graph<Key, Weight>::RequireEdgePositions(Index first, Index second) const -> std::pair<Index, Index>
{
    const auto& left = *this->GetVertex(first).neighbors;
    const auto& right = *this->GetVertex(second).neighbors;
    Index leftPosition = FindNeighbor(left, second);
    Index rightPosition = FindNeighbor(right, first);
    if (leftPosition == left.GetLength() || rightPosition == right.GetLength())
    {
        throw std::out_of_range("Graph: edge not found");
    }
    return {leftPosition, rightPosition};
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::SetEdgeWeight(const Key& first, const Key& second, const Weight& weight)
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    auto [leftPosition, rightPosition] = this->RequireEdgePositions(u, v);
    const Neighbor& left = this->GetVertex(u).neighbors->Get(leftPosition);
    const Neighbor& right = this->GetVertex(v).neighbors->Get(rightPosition);
    const Weight updated(weight);
    left.weight = updated;
    right.weight = updated;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::RemoveEdge(const Key& first, const Key& second)
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    auto [leftPosition, rightPosition] = this->RequireEdgePositions(u, v);
    this->GetVertex(u).neighbors->RemoveAt(leftPosition);
    this->GetVertex(v).neighbors->RemoveAt(rightPosition);
    --this->edgeCount;
}

template<typename Key, typename Weight>
void Graph<Key, Weight>::RemoveVertex(const Key& key)
{
    Index removed = this->RequireVertex(key);
    Graph<Key, Weight> replacement(this->indices->CreateEmpty(), this->createVertices, this->createNeighbors);
    for (Index i = 0; i < this->GetVertexCount(); ++i)
    {
        if (i == removed) { continue; }
        const Vertex& source = this->GetVertex(i);
        Index next = replacement.GetVertexCount();
        replacement.AddVertex(source.key);
        auto& neighbors = *replacement.GetVertex(next).neighbors;
        for (Index j = 0; j < source.neighbors->GetLength(); ++j)
        {
            const auto& neighbor = source.neighbors->Get(j);
            if (neighbor.index != removed)
            {
                Index mapped = neighbor.index < removed ? neighbor.index : neighbor.index - 1;
                neighbors.Append({mapped, neighbor.weight});
            }
        }
    }
    replacement.edgeCount = this->edgeCount - this->GetNeighborCount(removed);
    this->Swap(replacement);
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
        const auto& neighbors = *this->GetVertex(u).neighbors;
        for (Index i = 0; i < neighbors.GetLength(); ++i)
        {
            Index v = neighbors.Get(i).index;
            if (marks[v] == u) { throw std::invalid_argument("Graph::AddEdge: duplicate edge"); }
            marks[v] = u;
        }
    }
}

#endif
