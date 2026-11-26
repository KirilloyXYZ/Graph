#ifndef GRAPH_HPP
#define GRAPH_HPP

#include <cstddef>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>

#include "../lab1/Sequence.hpp"
#include "../lab2/IDictionary.hpp"

template<typename Key>
class Graph
{
public:
    using Index = std::size_t;
    using Edge = std::pair<Key, Key>;
    using Dictionary = IDictionary<Key, Index>;

private:
    struct Vertex
    {
        const Key key;
        std::unique_ptr<Sequence<Index>> neighbors;

        Vertex(const Key& key, std::unique_ptr<Sequence<Index>> neighbors)
            : key(key), neighbors(std::move(neighbors)) { }
    };

    using VertexHandle = std::shared_ptr<Vertex>;

    template<typename T>
    using SequenceFactory = std::unique_ptr<Sequence<T>> (*)();

    SequenceFactory<VertexHandle> createVertices;
    SequenceFactory<Index> createNeighbors;
    std::unique_ptr<Dictionary> indices;
    std::unique_ptr<Sequence<VertexHandle>> vertices;
    std::size_t edgeCount = 0;

    template<typename T>
    static std::unique_ptr<Sequence<T>> MakeEmptySequence(SequenceFactory<T> factory);

    const Vertex& GetVertex(Index index) const;
    Index RequireVertex(const Key& key) const;
    static Index FindNeighbor(const Sequence<Index>& neighbors, Index neighbor);
    bool HasEdgeBetween(Index first, Index second) const;
    void AppendEdge(Index first, Index second);
    void CheckDuplicateEdges() const;
    void Swap(Graph<Key>& other) noexcept;

public:
    // Фабрики возвращают отдельные пустые последовательности. Для индексов и
    // указателей Append сохраняет содержимое при сбое, а RemoveAt по верному
    // индексу не бросает исключений. Add словаря сохраняет данные при отказе.
    Graph(std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
          SequenceFactory<Index> createNeighbors);
    Graph(const Sequence<Key>& keys, const Sequence<Edge>& edges, std::unique_ptr<Dictionary> dictionary,
          SequenceFactory<VertexHandle> createVertices, SequenceFactory<Index> createNeighbors);
    Graph(const Graph<Key>& other);
    Graph(Graph<Key>&& other);
    Graph<Key>& operator=(const Graph<Key>& other);
    Graph<Key>& operator=(Graph<Key>&& other);

    std::size_t GetVertexCount() const;
    std::size_t GetEdgeCount() const;
    std::optional<Index> FindVertexIndex(const Key& key) const;
    bool ContainsVertex(const Key& key) const;
    bool ContainsEdge(const Key& first, const Key& second) const;

    const Key& GetVertexKey(Index index) const;
    std::size_t GetNeighborCount(Index index) const;
    Index GetNeighborIndex(Index index, std::size_t position) const;

    template<typename Action>
    void ForEachNeighbor(const Key& key, Action action) const;

    template<typename Action>
    void ForEachEdge(Action action) const;

    void AddVertex(const Key& key);
    void RemoveVertex(const Key& key);
    void AddEdge(const Key& first, const Key& second);
    void RemoveEdge(const Key& first, const Key& second);
};

template<typename Key>
template<typename T>
std::unique_ptr<Sequence<T>> Graph<Key>::MakeEmptySequence(SequenceFactory<T> factory)
{
    auto sequence = factory();
    if (sequence == nullptr || sequence->GetLength() != 0)
    {
        throw std::invalid_argument("Graph: a sequence factory must return an empty sequence");
    }
    return sequence;
}

template<typename Key>
Graph<Key>::Graph(std::unique_ptr<Dictionary> dictionary, SequenceFactory<VertexHandle> createVertices,
                  SequenceFactory<Index> createNeighbors)
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

template<typename Key>
Graph<Key>::Graph(const Sequence<Key>& keys, const Sequence<Edge>& edges, std::unique_ptr<Dictionary> dictionary,
                  SequenceFactory<VertexHandle> createVertices, SequenceFactory<Index> createNeighbors)
    : Graph(std::move(dictionary), createVertices, createNeighbors)
{
    for (Index i = 0; i < keys.GetLength(); ++i) { this->AddVertex(keys.Get(i)); }
    for (Index i = 0; i < edges.GetLength(); ++i)
    {
        const Edge& edge = edges.Get(i);
        Index first = this->RequireVertex(edge.first);
        Index second = this->RequireVertex(edge.second);
        if (first == second) { throw std::invalid_argument("Graph::AddEdge: loop"); }
        this->AppendEdge(first, second);
    }
    this->CheckDuplicateEdges();
}

template<typename Key>
Graph<Key>::Graph(const Graph<Key>& other)
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

template<typename Key>
Graph<Key>::Graph(Graph<Key>&& other)
    : Graph(other.indices->CreateEmpty(), other.createVertices, other.createNeighbors)
{
    this->Swap(other);
}

template<typename Key>
Graph<Key>& Graph<Key>::operator=(const Graph<Key>& other)
{
    if (this != &other)
    {
        Graph<Key> copy(other);
        this->Swap(copy);
    }
    return *this;
}

template<typename Key>
Graph<Key>& Graph<Key>::operator=(Graph<Key>&& other)
{
    if (this != &other)
    {
        Graph<Key> moved(std::move(other));
        this->Swap(moved);
    }
    return *this;
}

template<typename Key>
void Graph<Key>::Swap(Graph<Key>& other) noexcept
{
    std::swap(this->createVertices, other.createVertices);
    std::swap(this->createNeighbors, other.createNeighbors);
    this->indices.swap(other.indices);
    this->vertices.swap(other.vertices);
    std::swap(this->edgeCount, other.edgeCount);
}

template<typename Key>
std::size_t Graph<Key>::GetVertexCount() const { return this->vertices->GetLength(); }

template<typename Key>
std::size_t Graph<Key>::GetEdgeCount() const { return this->edgeCount; }

template<typename Key>
std::optional<typename Graph<Key>::Index> Graph<Key>::FindVertexIndex(const Key& key) const
{
    const Index* found = this->indices->Find(key);
    if (found == nullptr) { return std::nullopt; }
    return *found;
}

template<typename Key>
bool Graph<Key>::ContainsVertex(const Key& key) const { return this->FindVertexIndex(key).has_value(); }

template<typename Key>
typename Graph<Key>::Index Graph<Key>::RequireVertex(const Key& key) const
{
    auto index = this->FindVertexIndex(key);
    if (!index) { throw std::out_of_range("Graph: vertex not found"); }
    return *index;
}

template<typename Key>
const typename Graph<Key>::Vertex& Graph<Key>::GetVertex(Index index) const
{
    return *this->vertices->Get(index);
}

template<typename Key>
const Key& Graph<Key>::GetVertexKey(Index index) const { return this->GetVertex(index).key; }

template<typename Key>
std::size_t Graph<Key>::GetNeighborCount(Index index) const
{
    return this->GetVertex(index).neighbors->GetLength();
}

template<typename Key>
typename Graph<Key>::Index Graph<Key>::GetNeighborIndex(Index index, std::size_t position) const
{
    return this->GetVertex(index).neighbors->Get(position);
}

template<typename Key>
typename Graph<Key>::Index Graph<Key>::FindNeighbor(const Sequence<Index>& neighbors, Index neighbor)
{
    Index count = neighbors.GetLength();
    for (Index i = 0; i < count; ++i)
    {
        if (neighbors.Get(i) == neighbor) { return i; }
    }
    return count;
}

template<typename Key>
bool Graph<Key>::HasEdgeBetween(Index first, Index second) const
{
    if (this->GetNeighborCount(first) > this->GetNeighborCount(second)) { std::swap(first, second); }
    const auto& neighbors = *this->GetVertex(first).neighbors;
    return FindNeighbor(neighbors, second) != neighbors.GetLength();
}

template<typename Key>
bool Graph<Key>::ContainsEdge(const Key& first, const Key& second) const
{
    auto u = this->FindVertexIndex(first);
    auto v = this->FindVertexIndex(second);
    return u && v && *u != *v && this->HasEdgeBetween(*u, *v);
}

template<typename Key>
template<typename Action>
void Graph<Key>::ForEachNeighbor(const Key& key, Action action) const
{
    const Vertex& vertex = this->GetVertex(this->RequireVertex(key));
    for (Index i = 0; i < vertex.neighbors->GetLength(); ++i)
    {
        action(this->GetVertexKey(vertex.neighbors->Get(i)));
    }
}

template<typename Key>
template<typename Action>
void Graph<Key>::ForEachEdge(Action action) const
{
    for (Index u = 0; u < this->GetVertexCount(); ++u)
    {
        const Vertex& vertex = this->GetVertex(u);
        for (Index i = 0; i < vertex.neighbors->GetLength(); ++i)
        {
            Index v = vertex.neighbors->Get(i);
            if (u < v) { action(vertex.key, this->GetVertexKey(v)); }
        }
    }
}

template<typename Key>
void Graph<Key>::AddVertex(const Key& key)
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

template<typename Key>
void Graph<Key>::AppendEdge(Index first, Index second)
{
    if (this->edgeCount == std::numeric_limits<std::size_t>::max())
    {
        throw std::length_error("Graph::AddEdge: too many edges");
    }
    auto& left = *this->GetVertex(first).neighbors;
    auto& right = *this->GetVertex(second).neighbors;
    Index position = left.GetLength();
    left.Append(second);
    try { right.Append(first); }
    catch (...)
    {
        left.RemoveAt(position);
        throw;
    }
    ++this->edgeCount;
}

template<typename Key>
void Graph<Key>::AddEdge(const Key& first, const Key& second)
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    if (u == v) { throw std::invalid_argument("Graph::AddEdge: loop"); }

    if (this->HasEdgeBetween(u, v))
    {
        throw std::invalid_argument("Graph::AddEdge: duplicate edge");
    }
    this->AppendEdge(u, v);
}

template<typename Key>
void Graph<Key>::RemoveEdge(const Key& first, const Key& second)
{
    Index u = this->RequireVertex(first);
    Index v = this->RequireVertex(second);
    auto& left = *this->GetVertex(u).neighbors;
    auto& right = *this->GetVertex(v).neighbors;
    Index leftPosition = FindNeighbor(left, v);
    Index rightPosition = FindNeighbor(right, u);
    if (leftPosition == left.GetLength() || rightPosition == right.GetLength())
    {
        throw std::out_of_range("Graph::RemoveEdge: edge not found");
    }

    left.RemoveAt(leftPosition);
    right.RemoveAt(rightPosition);
    --this->edgeCount;
}

template<typename Key>
void Graph<Key>::RemoveVertex(const Key& key)
{
    Index removed = this->RequireVertex(key);
    Graph<Key> replacement(this->indices->CreateEmpty(), this->createVertices, this->createNeighbors);
    for (Index i = 0; i < this->GetVertexCount(); ++i)
    {
        if (i == removed) { continue; }
        const Vertex& source = this->GetVertex(i);
        Index next = replacement.GetVertexCount();
        replacement.AddVertex(source.key);
        auto& neighbors = *replacement.GetVertex(next).neighbors;
        for (Index j = 0; j < source.neighbors->GetLength(); ++j)
        {
            Index neighbor = source.neighbors->Get(j);
            if (neighbor != removed) { neighbors.Append(neighbor < removed ? neighbor : neighbor - 1); }
        }
    }
    replacement.edgeCount = this->edgeCount - this->GetNeighborCount(removed);
    this->Swap(replacement);
}

template<typename Key>
void Graph<Key>::CheckDuplicateEdges() const
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
            Index v = neighbors.Get(i);
            if (marks[v] == u) { throw std::invalid_argument("Graph::AddEdge: duplicate edge"); }
            marks[v] = u;
        }
    }
}

#endif
