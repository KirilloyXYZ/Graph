#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

#include "../app/InterplanetaryNetwork.hpp"
#include "../benchmarks/Benchmark.hpp"
#include "../lab2/ArrayStorage.hpp"
#include "../gui/BenchmarkData.hpp"
#include "../gui/GraphViewData.hpp"

namespace
{
    using NumericGraph = Graph<std::size_t>;
    constexpr auto BFS = CreateBFSEnumerator<std::size_t>;
    constexpr auto DFS = CreateDFSEnumerator<std::size_t>;

    void Check(bool condition, const char* message)
    {
        if (!condition) { throw std::runtime_error(message); }
    }

    template<typename Exception, typename Action>
    void CheckThrows(Action action)
    {
        try { action(); }
        catch (const Exception&) { return; }
        throw std::runtime_error("Expected exception was not thrown");
    }

    template<typename Dictionary>
    NumericGraph MakeNumericGraph()
    {
        return NumericGraph(std::make_unique<Dictionary>(), CreateArraySequence, CreateArraySequence);
    }

    template<typename Dictionary>
    NumericGraph MakeNumericGraph(const Sequence<std::size_t>& vertices, const Sequence<NumericGraph::Edge>& edges)
    {
        return NumericGraph(vertices, edges, std::make_unique<Dictionary>(), CreateArraySequence, CreateArraySequence);
    }

    template<typename T>
    void CheckOrder(const TraversalKeys<T>& order, std::initializer_list<T> expected)
    {
        Check(order.GetLength() == expected.size(), "Traversal length");
        std::size_t i = 0;
        for (const T& key : expected) { Check(*order.Get(i++) == key, "Unexpected traversal order"); }
    }

    template<typename Dictionary>
    void TestNumericDictionary()
    {
        auto graph = MakeNumericGraph<Dictionary>();
        Check(graph.GetVertexCount() == 0 && graph.GetEdgeCount() == 0, "Empty graph");
        Check(!graph.ContainsVertex(0) && !graph.ContainsEdge(0, 1), "Empty graph lookup");
        CheckThrows<std::out_of_range>([&]() { graph.GetVertexKey(0); });
        CheckThrows<std::out_of_range>([&]() { graph.RemoveVertex(0); });

        const std::size_t largest = std::numeric_limits<std::size_t>::max();
        for (std::size_t key : {largest, std::size_t{20}, std::size_t{0}, std::size_t{15}})
        {
            graph.AddVertex(key);
        }
        graph.AddEdge(largest, 20);
        graph.AddEdge(largest, 0);
        graph.AddEdge(0, 15);
        graph.AddEdge(20, 0);
        Check(graph.GetEdgeCount() == 4 && graph.ContainsEdge(0, largest), "Bidirectional edge lookup");
        CheckThrows<std::invalid_argument>([&]() { graph.AddVertex(20); });
        CheckThrows<std::invalid_argument>([&]() { graph.AddEdge(0, largest); });
        CheckThrows<std::invalid_argument>([&]() { graph.AddEdge(0, 0); });
        CheckThrows<std::out_of_range>([&]() { graph.AddEdge(0, 2); });
        CheckThrows<std::out_of_range>([&]() { graph.RemoveEdge(15, 20); });
        CheckThrows<std::out_of_range>([&]() { graph.ForEachNeighbor(2, [](std::size_t) {}); });
        Check(graph.GetVertexCount() == 4 && graph.GetEdgeCount() == 4, "Invalid edits preserve the graph");

        ArraySequence<std::size_t> neighbors;
        graph.ForEachNeighbor(largest, [&](std::size_t key) { neighbors.Append(key); });
        Check(neighbors.GetLength() == 2 && neighbors.Get(0) == 20 && neighbors.Get(1) == 0,
              "Neighbors follow insertion order");
        std::size_t listed = 0;
        graph.ForEachEdge([&](std::size_t first, std::size_t second)
        {
            Check(graph.FindVertexIndex(first) < graph.FindVertexIndex(second), "Canonical edge enumeration");
            Check(graph.ContainsEdge(first, second), "Enumerated edge exists");
            ++listed;
        });
        Check(listed == 4, "Each undirected edge is enumerated once");

        graph.RemoveVertex(20);
        Check(graph.GetEdgeCount() == 2 && graph.GetVertexCount() == 3, "Delete both incident edges");
        Check(graph.FindVertexIndex(0) == 1 && graph.FindVertexIndex(15) == 2, "Renumber after vertex deletion");
        Check(graph.GetNeighborIndex(1, 0) == 0 && graph.GetNeighborIndex(1, 1) == 2, "Remap neighbor indices");
        graph.RemoveEdge(0, largest);
        Check(!graph.ContainsEdge(largest, 0) && graph.GetNeighborCount(0) == 0, "Remove both copies of an edge");

        NumericGraph copy(graph);
        copy.AddEdge(largest, 15);
        Check(!graph.ContainsEdge(largest, 15), "Copy owns separate adjacency lists");
        auto assigned = MakeNumericGraph<Dictionary>();
        assigned = copy;
        copy.RemoveVertex(15);
        Check(assigned.ContainsEdge(largest, 15) && assigned.GetEdgeCount() == 2, "Copy assignment is independent");

        NumericGraph moved(std::move(assigned));
        Check(assigned.GetVertexCount() == 0 && assigned.GetEdgeCount() == 0, "Move resets source");
        assigned.AddVertex(99);
        Check(moved.ContainsVertex(0) && assigned.ContainsVertex(99), "Moved-from graph can be reused");
        assigned = std::move(moved);
        Check(assigned.GetEdgeCount() == 2 && moved.GetVertexCount() == 0, "Move assignment");
        auto& alias = assigned;
        assigned = alias;
        assigned = std::move(alias);
        Check(assigned.GetEdgeCount() == 2, "Self-assignment");

        graph.RemoveVertex(largest);
        graph.RemoveVertex(15);
        graph.RemoveVertex(0);
        Check(graph.GetVertexCount() == 0 && graph.GetEdgeCount() == 0, "Remove first, last and only vertices");
    }

    void TestDictionaryInterchangeability()
    {
        using Tree = TreeDictionary<std::size_t, std::size_t>;
        using Array = ArrayStorage<std::size_t>;
        TestNumericDictionary<Tree>();
        TestNumericDictionary<Array>();

        auto tree = MakeNumericGraph<Tree>();
        auto array = MakeNumericGraph<Array>();
        constexpr std::size_t count = 13;
        bool present[count] = {};
        bool edges[count][count] = {};
        std::mt19937 random(27);
        for (std::size_t step = 0; step < 550; ++step)
        {
            std::size_t u = random() % count;
            std::size_t v = random() % count;
            switch (random() % 4)
            {
                case 0:
                    if (!present[u])
                    {
                        tree.AddVertex(u);
                        array.AddVertex(u);
                        present[u] = true;
                    }
                    break;
                case 1:
                    if (present[u])
                    {
                        tree.RemoveVertex(u);
                        array.RemoveVertex(u);
                        present[u] = false;
                        for (std::size_t i = 0; i < count; ++i) { edges[u][i] = edges[i][u] = false; }
                    }
                    break;
                case 2:
                    if (u != v && present[u] && present[v] && !edges[u][v])
                    {
                        tree.AddEdge(u, v);
                        array.AddEdge(u, v);
                        edges[u][v] = edges[v][u] = true;
                    }
                    break;
                case 3:
                    if (edges[u][v])
                    {
                        tree.RemoveEdge(u, v);
                        array.RemoveEdge(u, v);
                        edges[u][v] = edges[v][u] = false;
                    }
                    break;
            }

            std::size_t vertices = 0;
            std::size_t edgeCount = 0;
            for (std::size_t i = 0; i < count; ++i)
            {
                Check(tree.ContainsVertex(i) == present[i] && array.ContainsVertex(i) == present[i],
                      "Both dictionaries match the independent model's vertices");
                if (present[i]) { ++vertices; }
                for (std::size_t j = 0; j < count; ++j)
                {
                    Check(tree.ContainsEdge(i, j) == edges[i][j] && array.ContainsEdge(i, j) == edges[i][j],
                          "Both dictionaries match the independent model's edges");
                    if (i < j && edges[i][j]) { ++edgeCount; }
                }
            }
            Check(tree.GetEdgeCount() == edgeCount && array.GetEdgeCount() == edgeCount &&
                  tree.GetVertexCount() == vertices && array.GetVertexCount() == vertices, "Model counts");
            for (std::size_t i = 0; i < vertices; ++i)
            {
                Check(tree.GetVertexKey(i) == array.GetVertexKey(i) &&
                      tree.GetNeighborCount(i) == array.GetNeighborCount(i), "Backend index order and degree");
                for (std::size_t j = 0; j < tree.GetNeighborCount(i); ++j)
                {
                    Check(tree.GetNeighborIndex(i, j) == array.GetNeighborIndex(i, j), "Backend neighbor order");
                }
            }
        }
    }

    template<typename Dictionary>
    void TestConstructionAndTraversals()
    {
        const std::size_t keys[] = {1, 2, 3, 4};
        const NumericGraph::Edge cycle[] = {{1, 2}, {1, 3}, {2, 4}, {3, 4}};
        ArraySequence<std::size_t> vertices(keys, 4);
        ArraySequence<NumericGraph::Edge> edges(cycle, 4);
        auto graph = MakeNumericGraph<Dictionary>(vertices, edges);
        CheckOrder<std::size_t>(Traverse(graph, 0, BFS), {1, 2, 3, 4});
        CheckOrder<std::size_t>(Traverse(graph, 0, DFS), {1, 2, 4, 3});

        auto bfsTree = FindSpanningTree(graph, 0, BFS);
        auto dfsTree = FindSpanningTree(graph, 0, DFS);
        Check(*bfsTree.order.Get(bfsTree.edges.Get(1).second) == 3 &&
              *dfsTree.order.Get(dfsTree.edges.Get(2).first) == 4, "Both traversal strategies select their own edges");
        for (const auto* tree : {&bfsTree, &dfsTree})
        {
            Check(tree->status == SpanningTreeStatus::Found && tree->edges.GetLength() == 3 &&
                  tree->order.GetLength() == 4, "Spanning tree size");
            bool connected[4] = {true, false, false, false};
            for (std::size_t i = 0; i < tree->edges.GetLength(); ++i)
            {
                auto edge = tree->edges.Get(i);
                Check(edge.first < 4 && edge.second < 4 && connected[edge.first] && !connected[edge.second],
                      "Every tree edge connects a new vertex to the tree");
                Check(graph.ContainsEdge(*tree->order.Get(edge.first), *tree->order.Get(edge.second)),
                      "Tree edge belongs to the original graph");
                connected[edge.second] = true;
            }
        }

        graph.AddVertex(99);
        graph.AddVertex(98);
        graph.AddVertex(97);
        graph.AddEdge(97, 98);
        for (auto method : {BFS, DFS})
        {
            auto components = FindConnectedComponents(graph, method);
            Check(components.componentEnds.GetLength() == 3 &&
                  components.componentEnds.Get(0) == 4 && components.componentEnds.Get(1) == 5 &&
                  components.componentEnds.Get(2) == 7, "Connected group, isolated vertex, and two-vertex group");
            Check(*components.order.Get(4) == 99 && *components.order.Get(5) == 98 &&
                  *components.order.Get(6) == 97, "Members of smaller groups");
            auto noTree = FindSpanningTree(graph, 0, method);
            Check(noTree.status == SpanningTreeStatus::Disconnected && noTree.edges.GetLength() == 0 &&
                  noTree.order.GetLength() == 4, "Disconnected graph has no spanning tree");
        }
        CheckThrows<std::out_of_range>([&]() { FindSpanningTree(graph, std::nullopt, BFS); });
        CheckThrows<std::out_of_range>([&]() { Traverse(graph, graph.GetVertexCount(), DFS); });
        CheckThrows<std::invalid_argument>([&]()
        {
            FindConnectedComponents(graph, TraversalFactory<std::size_t>{});
        });

        graph.RemoveVertex(3);
        CheckOrder<std::size_t>(bfsTree.order, {1, 2, 3, 4});
        Check(graph.GetEdgeCount() == 3, "Analysis leaves source data intact");

        auto empty = MakeNumericGraph<Dictionary>();
        Check(FindConnectedComponents(empty, BFS).componentEnds.GetLength() == 0, "Empty partition");
        Check(FindSpanningTree(empty, std::nullopt, DFS).status == SpanningTreeStatus::Empty, "Empty spanning result");
        empty.AddVertex(11);
        auto singleton = FindSpanningTree(empty, 0, BFS);
        Check(singleton.status == SpanningTreeStatus::Found && singleton.edges.GetLength() == 0 &&
              singleton.order.GetLength() == 1 && *singleton.order.Get(0) == 11, "One-vertex spanning tree");

        edges.Append({2, 1});
        CheckThrows<std::invalid_argument>([&]() { MakeNumericGraph<Dictionary>(vertices, edges); });
        edges.Get(4) = {2, 2};
        CheckThrows<std::invalid_argument>([&]() { MakeNumericGraph<Dictionary>(vertices, edges); });
        edges.Get(4) = {2, 9};
        CheckThrows<std::out_of_range>([&]() { MakeNumericGraph<Dictionary>(vertices, edges); });
        edges.RemoveAt(4);
        vertices.Append(2);
        CheckThrows<std::invalid_argument>([&]() { MakeNumericGraph<Dictionary>(vertices, edges); });
    }

    struct Key
    {
        const int value;
        explicit Key(int value) : value(value) { }
        Key(const Key&) = default;
        Key& operator=(const Key&) = delete;
    };

    bool CompareKey(const Key& left, const Key& right) { return left.value / 10 < right.value / 10; }

    template<typename T>
    class InputSequence : public Sequence<T>
    {
    private:
        const T* values;
        std::size_t length;

    public:
        InputSequence(const T* values, std::size_t length) : values(values), length(length) { }
        std::size_t GetLength() const override { return this->length; }
        const T& Get(std::size_t index) const override
        {
            if (index >= this->length) { throw std::out_of_range("InputSequence: invalid index"); }
            return this->values[index];
        }
        void Append(const T&) override { throw std::logic_error("InputSequence: read-only"); }
        void RemoveAt(std::size_t) override { throw std::logic_error("InputSequence: read-only"); }
    };

    void TestKeyTypes()
    {
        static_assert(std::is_copy_constructible<Key>::value, "Key must support copies");
        static_assert(!std::is_default_constructible<Key>::value && !std::is_copy_assignable<Key>::value,
                      "This test exercises a key that DynamicArray cannot store directly");

        const Key keys[] = {Key(11), Key(21), Key(31)};
        const Graph<Key>::Edge edges[] = {{Key(15), Key(22)}, {Key(23), Key(31)}};
        Graph<Key> graph(InputSequence<Key>(keys, 3), InputSequence<Graph<Key>::Edge>(edges, 2),
                         std::make_unique<TreeDictionary<Key, std::size_t>>(CompareKey),
                         CreateArraySequence, CreateArraySequence);

        Check(graph.ContainsEdge(Key(19), Key(29)), "Lookup follows custom comparator");
        CheckThrows<std::invalid_argument>([&]() { graph.AddVertex(Key(19)); });
        CheckThrows<std::invalid_argument>([&]() { graph.AddEdge(Key(11), Key(19)); });
        auto tree = FindSpanningTree(graph, 0, CreateDFSEnumerator<Key>);
        Check(tree.edges.GetLength() == 2 && tree.order.Get(1)->value == 21, "Copy-constructible key in result");
        auto copy = graph;
        copy.RemoveVertex(Key(22));
        Check(!copy.ContainsVertex(Key(29)) && copy.GetEdgeCount() == 0 && graph.GetEdgeCount() == 2,
              "Clone and CreateEmpty retain the comparator");
    }

    int appendsBeforeFailure = -1;

    template<typename T>
    class FailingSequence : public ArraySequence<T>
    {
    public:
        void Append(const T& value) override
        {
            if (appendsBeforeFailure == 0) { throw std::runtime_error("Injected append failure"); }
            if (appendsBeforeFailure > 0) { --appendsBeforeFailure; }
            ArraySequence<T>::Append(value);
        }
    };

    template<typename T>
    std::unique_ptr<Sequence<T>> CreateFailingSequence() { return std::make_unique<FailingSequence<T>>(); }

    struct ThrowingKey
    {
        int value;
        static int copiesBeforeFailure;

        explicit ThrowingKey(int value) : value(value) { }
        ThrowingKey(const ThrowingKey& other) : value(other.value)
        {
            if (copiesBeforeFailure == 0) { throw std::runtime_error("Injected key copy failure"); }
            if (copiesBeforeFailure > 0) { --copiesBeforeFailure; }
        }
        bool operator<(const ThrowingKey& other) const { return this->value < other.value; }
    };

    int ThrowingKey::copiesBeforeFailure = -1;

    void TestExceptionSafety()
    {
        Graph<ThrowingKey> graph(std::make_unique<TreeDictionary<ThrowingKey, std::size_t>>(),
                                 CreateArraySequence, CreateArraySequence);
        graph.AddVertex(ThrowingKey(1));
        // Первая копия создаёт запись вершины, вторая создаёт узел словаря.
        ThrowingKey::copiesBeforeFailure = 1;
        CheckThrows<std::runtime_error>([&]() { graph.AddVertex(ThrowingKey(2)); });
        ThrowingKey::copiesBeforeFailure = -1;
        Check(graph.GetVertexCount() == 1 && graph.GetVertexKey(0).value == 1 &&
              !graph.ContainsVertex(ThrowingKey(2)), "Dictionary failure rolls back vertex append");
        graph.AddVertex(ThrowingKey(2));
        Check(graph.FindVertexIndex(ThrowingKey(2)) == 1, "Index is reused after rollback");

        Graph<ThrowingKey> target(std::make_unique<TreeDictionary<ThrowingKey, std::size_t>>(),
                                  CreateArraySequence, CreateArraySequence);
        target.AddVertex(ThrowingKey(7));
        ThrowingKey::copiesBeforeFailure = 0;
        CheckThrows<std::runtime_error>([&]() { target = graph; });
        ThrowingKey::copiesBeforeFailure = -1;
        Check(target.GetVertexCount() == 1 && target.GetVertexKey(0).value == 7, "Failed copy keeps the target");

        NumericGraph numeric(std::make_unique<ArrayStorage<std::size_t>>(), CreateArraySequence, CreateFailingSequence);
        for (std::size_t vertex : {1, 2, 3, 4}) { numeric.AddVertex(vertex); }
        numeric.AddEdge(1, 3);
        numeric.AddEdge(2, 4);
        appendsBeforeFailure = 1;
        CheckThrows<std::runtime_error>([&]() { numeric.AddEdge(1, 2); });
        appendsBeforeFailure = -1;
        Check(numeric.GetEdgeCount() == 2 && !numeric.ContainsEdge(1, 2) && !numeric.ContainsEdge(2, 1) &&
              numeric.GetNeighborCount(0) == 1 && numeric.GetNeighborCount(1) == 1 &&
              numeric.ContainsEdge(1, 3) && numeric.ContainsEdge(2, 4), "Second append rolls back the first");
        numeric.AddEdge(1, 2);
        appendsBeforeFailure = 1;
        CheckThrows<std::runtime_error>([&]() { numeric.RemoveVertex(4); });
        appendsBeforeFailure = -1;
        Check(numeric.GetVertexCount() == 4 && numeric.GetEdgeCount() == 3 && numeric.ContainsEdge(2, 4),
              "Failed vertex rebuild preserves graph");
        numeric.RemoveVertex(4);
        Check(!numeric.ContainsVertex(4) && numeric.GetEdgeCount() == 2, "Retry after failed rebuild");
    }

    template<typename Dictionary>
    void TestWeightedGraph()
    {
        const std::size_t keys[]{10, 20, 30};
        const NumericGraph::WeightedEdge edges[]{{10, 30, 4.5}, {20, 30, -2}};
        NumericGraph graph(ArraySequence<std::size_t>(keys, 3), ArraySequence<NumericGraph::WeightedEdge>(edges, 2),
                           std::make_unique<Dictionary>(), CreateArraySequence, CreateArraySequence);
        Check(graph.GetNeighborWeight(0, 0) == 4.5 && graph.GetNeighborWeight(2, 0) == 4.5,
              "Both adjacency entries carry the weight");
        Check(graph.GetEdgeWeight(20, 30) == -2 && graph.GetEdgeWeight(30, 20) == -2, "Weight lookup is symmetric");
        CheckThrows<std::invalid_argument>([&] { graph.AddEdge(30, 10, 100); });
        CheckThrows<std::out_of_range>([&] { graph.SetEdgeWeight(10, 20, 1); });
        CheckThrows<std::out_of_range>([&] { graph.GetEdgeWeight(10, 10); });

        graph.AddEdge(10, 20);
        Check(graph.GetEdgeWeight(10, 20) == 1, "Unit weight for an implicit edge");
        auto copy = graph;
        copy.SetEdgeWeight(30, 10, 0);
        Check(copy.GetNeighborWeight(0, 0) == 0 && copy.GetNeighborWeight(2, 0) == 0 &&
              graph.GetEdgeWeight(10, 30) == 4.5, "Update changes both entries and keeps the original graph");
        copy.RemoveVertex(20);
        Check(copy.GetNeighborIndex(0, 0) == 1 && copy.GetEdgeWeight(30, 10) == 0, "Reindexing preserves the weight");
        copy.RemoveEdge(10, 30);
        Check(copy.GetEdgeCount() == 0 && copy.GetNeighborCount(0) == 0 && copy.GetNeighborCount(1) == 0,
              "Weighted removal clears both entries");

        std::size_t seen = 0;
        graph.ForEachWeightedEdge([&](std::size_t u, std::size_t v, double weight)
        {
            Check(u < v && weight == graph.GetEdgeWeight(graph.GetVertexKey(u), graph.GetVertexKey(v)),
                  "Enumeration yields canonical indices and original weights");
            ++seen;
        });
        Check(seen == graph.GetEdgeCount(), "Enumeration yields each edge once");
    }

    struct CustomWeight
    {
        struct Value { int number; };
        int number = 0;
        static int copiesBeforeFailure;

        CustomWeight() noexcept = default;
        explicit CustomWeight(Value value) : number(value.number) { }
        CustomWeight(const CustomWeight& other) : number(other.number)
        {
            if (copiesBeforeFailure == 0) { throw std::runtime_error("Injected weight copy failure"); }
            if (copiesBeforeFailure > 0) { --copiesBeforeFailure; }
        }
        CustomWeight& operator=(const CustomWeight&) noexcept = default;
    };

    int CustomWeight::copiesBeforeFailure = -1;

    void TestCustomWeightAndMinimumSpanningTree()
    {
        static_assert(!std::is_constructible_v<CustomWeight, int>);
        using GraphType = Graph<std::size_t, CustomWeight>;
        GraphType graph(std::make_unique<ArrayStorage<std::size_t>>(), CreateArraySequence, CreateArraySequence);
        auto compare = [](const CustomWeight& left, const CustomWeight& right) { return left.number < right.number; };
        Check(FindMinimumSpanningTree(graph, compare).status == SpanningTreeStatus::Empty, "Empty minimum spanning tree");
        graph.AddVertex(1);
        auto single = FindMinimumSpanningTree(graph, compare);
        Check(single.status == SpanningTreeStatus::Found && single.vertices.GetLength() == 1 &&
              single.edges.GetLength() == 0, "A singleton is its own minimum spanning tree");

        for (std::size_t key : {2, 3, 4}) { graph.AddVertex(key); }
        graph.AddEdge(1, 2, CustomWeight({9}));
        CustomWeight::copiesBeforeFailure = 1;
        CheckThrows<std::runtime_error>([&] { graph.AddEdge(1, 3, CustomWeight({0})); });
        Check(graph.GetEdgeCount() == 1 && !graph.ContainsEdge(1, 3), "Failed weight preparation leaves no half-edge");
        CheckThrows<std::runtime_error>([&] { graph.SetEdgeWeight(1, 2, CustomWeight({0})); });
        CustomWeight::copiesBeforeFailure = -1;
        Check(graph.GetNeighborWeight(0, 0).number == 9 && graph.GetNeighborWeight(1, 0).number == 9,
              "Failed replacement keeps both weights");
        graph.AddEdge(1, 3, CustomWeight({0}));
        graph.AddEdge(2, 3, CustomWeight({-2}));
        auto disconnected = FindMinimumSpanningTree(graph, compare);
        Check(disconnected.status == SpanningTreeStatus::Disconnected && disconnected.edges.GetLength() == 0,
              "A partial forest is not a spanning tree");

        graph.AddEdge(3, 4, CustomWeight({0}));
        graph.AddEdge(1, 4, CustomWeight({0}));
        auto ordinary = FindSpanningTree(graph, 0, CreateBFSEnumerator<std::size_t, CustomWeight>);
        auto tree = FindMinimumSpanningTree(graph, compare);
        Check(ordinary.edges.GetLength() == 3 && tree.status == SpanningTreeStatus::Found &&
              tree.edges.GetLength() == 3, "Both kinds of spanning tree cover all vertices");
        Check(tree.edges.Get(0).weight.number == -2 &&
              tree.edges.Get(1).first == 0 && tree.edges.Get(1).second == 2 &&
              tree.edges.Get(2).first == 0 && tree.edges.Get(2).second == 3,
              "Kruskal chooses the minimum weights and resolves ties by endpoint indices");
        graph.SetEdgeWeight(2, 3, CustomWeight({8}));
        Check(tree.edges.Get(0).weight.number == -2 && graph.GetEdgeWeight(2, 3).number == 8,
              "Minimum spanning tree owns its weights");
    }

    void TestTraversalEnumerators()
    {
        auto graph = MakeNumericGraph<ArrayStorage<std::size_t>>();
        for (std::size_t key : {10, 20, 30, 40}) { graph.AddVertex(key); }
        graph.AddEdge(10, 20, 40);
        graph.AddEdge(10, 30, 8);
        graph.AddEdge(20, 40, 1);
        graph.AddEdge(30, 40, 2);
        for (auto create : {BFS, DFS})
        {
            auto marks = graph_detail::CreateMarks(4);
            auto cursor = create(graph, 0, marks);
            CheckThrows<std::out_of_range>([&] { cursor->GetCurrent(); });
            CheckThrows<std::out_of_range>([&] { cursor->GetCurrentKey(); });
            Check(cursor->MoveNext() && cursor->GetCurrentKey() == 10 && !cursor->GetCurrent().parent &&
                  marks.Get(0) && !marks.Get(1) && !marks.Get(2) && !marks.Get(3),
                  "First step discovers only the root");
            Check(cursor->MoveNext() && cursor->GetCurrent().vertex == 1 && cursor->GetCurrent().parent == 0,
                  "Second step carries its parent index");
            auto next = create == BFS ? std::size_t{2} : std::size_t{3};
            auto parent = create == BFS ? std::size_t{0} : std::size_t{1};
            Check(cursor->MoveNext() && cursor->GetCurrent().vertex == next && cursor->GetCurrent().parent == parent,
                  "Enumerator resumes its frontier");
            Check(cursor->MoveNext() && !cursor->MoveNext() && !cursor->MoveNext(), "Completed enumeration stays ended");
            CheckThrows<std::out_of_range>([&] { cursor->GetCurrent(); });
            CheckThrows<std::out_of_range>([&] { cursor->GetCurrentKey(); });
        }
    }

    void TestNetworkDesign()
    {
        const StationId keys[]{"A", "B", "C"};
        const ChannelKey longEdge[]{{"A", "C"}};
        ArraySequence<StationId> vertices(keys, 3);
        const std::size_t earth = 2;
        std::vector<Placement> placements{{{0, 0}, earth}, {{3, 0}, earth}, {{3, 4}, earth}};
        auto source = BuildDocument(vertices, ArraySequence<ChannelKey>{}, true, placements);
        ArraySequence<WorldPoint> positions;
        for (const auto& key : keys) { positions.Append(source->StationPosition(key)); }
        auto design = DesignNetwork(source->graph, positions);
        Check(design && design->status == SpanningTreeStatus::Found && design->channels.GetLength() == 2 &&
              design->totalLength == 7 && source->graph.GetEdgeCount() == 0,
              "Project connects isolated stations with the minimum total map distance");
        auto applied = ApplyNetworkDesign(*source, *design);
        Check(applied->Status() == NetworkStatus::Connected && applied->graph.GetEdgeWeight("A", "B") == 3 &&
              applied->graph.GetEdgeWeight("B", "C") == 4, "Applying the project builds a weighted network");

        auto existing = BuildDocument(vertices, ArraySequence<ChannelKey>(longEdge, 1), true, placements);
        Check(existing->graph.GetEdgeWeight("A", "C") == 5, "Bulk input uses final positions");
        existing->AddChannel("A", "B");
        Check(existing->graph.GetEdgeWeight("A", "B") == 3, "Manual channel uses the same distance");
        auto replaced = ApplyNetworkDesign(*existing, *DesignNetwork(existing->graph, positions));
        Check(replaced->graph.GetEdgeCount() == 2 && !replaced->graph.ContainsEdge("A", "C") &&
              existing->graph.ContainsEdge("A", "C"), "Applying a new project replaces existing channels on a copy");
        auto edited = *applied;
        edited.Relocate("A", {{3, 0}, earth});
        Check(edited.graph.GetEdgeWeight("A", "B") == 0 && edited.graph.GetEdgeWeight("B", "C") == 4 &&
              applied->graph.GetEdgeWeight("A", "B") == 3 && edited.revision == applied->revision + 1,
              "Relocation updates incident channels without changing the source or other weights");
        auto placement = PlaceOnPlanet(6, true, 0);
        edited.Relocate("A", placement);
        Check(edited.graph.GetEdgeWeight("A", "B") ==
              ChannelDistance(edited.StationPosition("A"), edited.StationPosition("B")),
              "Planet and orbit changes update weights");

        Check(!DesignNetwork(source->graph, positions, [] { return true; }), "Cancelled design yields no project");
        positions.Get(0).x = std::numeric_limits<float>::infinity();
        CheckThrows<std::invalid_argument>([&] { DesignNetwork(source->graph, positions); });
    }

    void TestGeneration()
    {
        using Tree = TreeDictionary<std::size_t, std::size_t>;
        for (auto topology : {Topology::Chain, Topology::Cycle, Topology::Complete, Topology::Star, Topology::BinaryTree})
        {
            auto data = GenerateGraphData({topology, 7});
            auto graph = MakeNumericGraph<Tree>(data.vertices, data.edges);
            Check(graph.GetVertexCount() == 7 && graph.GetVertexKey(6) == 7, "Generated vertex labels");
            for (std::size_t u = 1; u <= 7; ++u)
            {
                for (std::size_t v = u + 1; v <= 7; ++v)
                {
                    bool expected = false;
                    switch (topology)
                    {
                        case Topology::Chain: expected = v == u + 1; break;
                        case Topology::Cycle: expected = v == u + 1 || (u == 1 && v == 7); break;
                        case Topology::Complete: expected = true; break;
                        case Topology::Star: expected = u == 1; break;
                        case Topology::BinaryTree: expected = u == v / 2; break;
                        case Topology::Random: break;
                    }
                    Check(graph.ContainsEdge(u, v) == expected, "Exact edges for the selected topology");
                }
            }
            Check(FindConnectedComponents(graph, BFS).componentEnds.GetLength() == 1,
                  "Deterministic non-empty topology is connected");
        }

        for (auto topology : {Topology::Chain, Topology::Complete, Topology::Random})
        {
            auto empty = GenerateGraphData({topology, 0});
            Check(empty.vertices.GetLength() == 0 && empty.edges.GetLength() == 0, "Empty generated graph");
        }
        for (auto topology : {Topology::Star, Topology::BinaryTree})
        {
            Check(GenerateGraphData({topology, 1}).edges.GetLength() == 0, "Single generated vertex");
            CheckThrows<std::invalid_argument>([&]() { GenerateGraphData({topology, 0}); });
        }
        Check(GenerateGraphData({Topology::Cycle, 3}).edges.GetLength() == 3, "Smallest cycle");
        CheckThrows<std::invalid_argument>([]() { GenerateGraphData({Topology::Cycle, 2}); });
        CheckThrows<std::invalid_argument>([]() { GenerateGraphData({Topology::Random, 3, 4, 0}); });
        CheckThrows<std::invalid_argument>([]() { GenerateGraphData({static_cast<Topology>(7), 3}); });
        CheckThrows<std::length_error>([]()
        {
            GenerateGraphData({Topology::Complete, std::numeric_limits<std::size_t>::max()});
        });

        for (std::size_t m : {0, 5, 43, 44, 45})
        {
            GenerationOptions options{Topology::Random, 10, m, 42};
            auto data = GenerateGraphData(options);
            auto repeated = GenerateGraphData(options);
            Check(data.edges.GetLength() == m, "Requested random edge count");
            for (std::size_t i = 0; i < m; ++i)
            {
                const auto& edge = data.edges.Get(i);
                Check(edge.first >= 1 && edge.first < edge.second && edge.second <= 10 &&
                      edge == repeated.edges.Get(i), "Random pairs are canonical and reproducible");
                if (i != 0) { Check(data.edges.Get(i - 1) < edge, "No duplicate random pair"); }
            }
            auto graph = MakeNumericGraph<Tree>(data.vertices, data.edges);
            if (m < 9)
            {
                Check(FindConnectedComponents(graph, DFS).componentEnds.GetLength() > 1, "Sparse random case disconnected");
            }
        }
    }

    void TestLargeTraversals()
    {
        auto data = GenerateGraphData({Topology::Chain, 15000});
        auto graph = MakeNumericGraph<TreeDictionary<std::size_t, std::size_t>>(data.vertices, data.edges);
        Check(Traverse(graph, 0, DFS).GetLength() == 15000, "Iterative DFS handles a deep chain");
        Check(FindSpanningTree(graph, 0, BFS).edges.GetLength() == 14999, "BFS on a large chain");
        auto isolated = GenerateGraphData({Topology::Random, 8000, 0, 3});
        auto graphOfIsolates = MakeNumericGraph<TreeDictionary<std::size_t, std::size_t>>(isolated.vertices, isolated.edges);
        for (auto method : {BFS, DFS})
        {
            auto components = FindConnectedComponents(graphOfIsolates, method);
            Check(components.componentEnds.GetLength() == 8000 && components.componentEnds.Get(7999) == 8000,
                  "Each isolated vertex forms a separate component");
        }
    }

    void TestNetworkAndBenchmarks()
    {
        InterplanetaryNetwork network(std::make_unique<TreeDictionary<StationId, std::size_t>>(),
                                      CreateArraySequence, CreateArraySequence);
        Check(AnalyzeNetwork(network, CreateBFSEnumerator<StationId>).status == NetworkStatus::Empty, "Empty network");
        const StationId earth = "Земля-орбита";
        const StationId mars = "Марс";
        const StationId start = "R,\"one\"";
        network.AddVertex(start);
        network.AddVertex(earth);
        network.AddVertex(mars);
        network.AddEdge(start, earth);
        network.AddEdge(earth, mars);

        Check(GetGeneratedStationId(15) == "S15", "Generated station identifier");
        Check(AnalyzeNetwork(network, CreateDFSEnumerator<StationId>).status == NetworkStatus::Connected, "Connected network");
        auto tree = FindSpanningTree(network, network.FindVertexIndex(start), CreateDFSEnumerator<StationId>);
        Check(tree.status == SpanningTreeStatus::Found && tree.edges.GetLength() == 2 &&
              *tree.order.Get(0) == start, "Network spanning tree uses station IDs");

        auto report = CompareAlgorithms(network, BenchmarkTask::SpanningTree, 0, 2);
        Check(report.records.GetLength() == 4 && report.records.Get(0).algorithm == "BFS" &&
              report.records.Get(1).algorithm == "DFS" && report.records.Get(2).algorithm == "DFS" &&
              report.records.Get(3).algorithm == "BFS", "Paired repetitions use alternate algorithm order");
        Check(report.bfsMedian >= 0 && report.dfsMedian >= 0 && report.records.Get(0).startKey == start,
              "Medians and start key");
        std::ostringstream csv;
        WriteBenchmarkCsv(csv, report.records);
        Check(csv.str().find("task,algorithm,topology,V,E,n,m,seed,edge_order,shuffle_seed,start,repeat,time_ms\n") == 0,
              "CSV columns");
        Check(csv.str().find("spanning_tree,BFS,manual,3,2,,,,current,,\"R,\"\"one\"\"\",1,") != std::string::npos,
              "CSV escapes station ID and retains the parameters");
        const double odd[] = {7, 1, 3};
        const double even[] = {2, 6, 8, 4};
        Check(Median(ArraySequence<double>(odd, 3)) == 3 && Median(ArraySequence<double>(even, 4)) == 5,
              "Odd and even medians");

        network.RemoveEdge(earth, mars);
        auto analysis = AnalyzeNetwork(network, CreateBFSEnumerator<StationId>);
        Check(analysis.status == NetworkStatus::Disconnected && analysis.components.componentEnds.GetLength() == 2,
              "Removed channel splits the network");
        CompareAlgorithms(network, BenchmarkTask::SpanningTree, 0, 1);
        CompareAlgorithms(network, BenchmarkTask::Components, std::nullopt, 1);

        GenerationOptions options{Topology::Random, 7, 4, 19};
        auto data = GenerateGraphData(options);
        for (auto order : {EdgeOrder::Ascending, EdgeOrder::Descending, EdgeOrder::Shuffled})
        {
            auto series = BenchmarkGeneratedGraph(data, options, BenchmarkTask::Components, order, 12, 1);
            Check(series.records.GetLength() == 2 && series.records.Get(0).input.order == order &&
                  series.records.Get(1).edges == 4 && series.records.Get(0).input.generation->seed == 19,
                  "Benchmark series retains topology, order and generation parameters");
        }
    }
    void TestPresentationData()
    {
        auto original = BuildDemoNetwork();
        Check(original->graph.GetVertexCount() == 10 && original->graph.GetEdgeCount() == 12 &&
              original->Status() == NetworkStatus::Connected, "Demo network is connected");
        Check(original->graph.GetEdgeWeight("Фобос", "Юпитер") ==
              ChannelDistance(original->StationPosition("Фобос"), original->StationPosition("Юпитер")),
              "The demo uses final station locations for its weights");
        auto trial = *original;
        trial.graph.RemoveEdge("Фобос", "Юпитер");
        trial.OnTopologyChange();
        auto groups = FindSplitGroups(*original, trial);
        Check(trial.groups.size() == 2 && groups.size() == 1 && groups.front().parts.size() == 2 &&
              groups.front().parts[0].size() == 5 && groups.front().parts[1].size() == 5, "Bridge removal divides the network");
        Check(original->graph.ContainsEdge("Фобос", "Юпитер") && original->graph.GetEdgeCount() == 12,
              "The original network owns its channels");
        auto byStation = *original;
        byStation.graph.RemoveVertex("Фобос");
        byStation.locations.erase("Фобос");
        byStation.OnTopologyChange();
        auto stationFailure = FindSplitGroups(*original, byStation);
        Check(byStation.graph.GetVertexCount() == 9 && byStation.groups.size() == 2 &&
              !byStation.locations.count("Фобос") && stationFailure.size() == 1, "Station failure updates the working copy");
        auto redundant = *original;
        redundant.graph.RemoveEdge("Сатурн", "Титан");
        redundant.OnTopologyChange();
        Check(FindSplitGroups(*original, redundant).empty(), "Redundant channel does not split a group");

        auto options = GenerationOptions{Topology::Random, 35, 71, 42};
        auto first = BuildGeneratedDocument(options);
        auto second = BuildGeneratedDocument(options);
        Check(first->groups == second->groups && first->locations.size() == 35, "Seed reproduces the generated graph");
        for (const auto& [key, placement] : first->locations)
        {
            auto other = second->locations.at(key);
            Check(other.offset.x == placement.offset.x && other.offset.y == placement.offset.y,
                  "Seed reproduces the drawing coordinates");
        }
        first->graph.ForEachEdge([&](const auto& u, const auto& v)
        {
            Check(second->graph.ContainsEdge(u, v), "Seed reproduces the actual edges");
        });

        ExperimentSettings series;
        Check(ValidateExperiment(series) == 5 && OptionsAt(series, Topology::Cycle, 4).vertexCount == 5000,
              "Vertex sweep follows the selected range");
        series.sweep = Sweep::Topologies;
        Check(ValidateExperiment(series) == 15, "One series covers the three selected topologies");
        series.sweep = Sweep::Edges;
        series.vertices = 40;
        series.edgeFrom = 0;
        series.edgeTo = 350;
        series.edgeStep = 100;
        Check(ValidateExperiment(series) == 4 && OptionsAt(series, Topology::Random, 3).edgeCount == 300,
              "Edge sweep preserves fixed V and does not overshoot E");
        series.edgeTo = 900;
        CheckThrows<std::invalid_argument>([&] { ValidateExperiment(series); });
        CheckThrows<std::invalid_argument>([] { CountExperimentRange(7, 10, 0); });
        CheckThrows<std::invalid_argument>([] { CountExperimentRange(10, 7, 1); });
    }

    void TestBenchmarkCancellation()
    {
        auto data = BuildDemoNetwork();
        data->graph.RemoveEdge("Фобос", "Юпитер");
        auto start = data->graph.FindVertexIndex("Земля");
        for (std::size_t stopAfter : {0, 1, 2, 3, 4, 6})
        {
            std::size_t measured = 0;
            BenchmarkControl control{[&] { return measured >= stopAfter; }, [&] { ++measured; }};
            auto report = CompareAlgorithms(data->graph, BenchmarkTask::SpanningTree, start, 3, {}, control);
            Check(measured == stopAfter && report.records.GetLength() == (stopAfter / 2) * 2 &&
                  report.stopped == (stopAfter < 6), "Cancellation occurs between measurements, with complete pairs");
            if (report.records.GetLength() == 0) { continue; }
            auto& first = report.records.Get(0);
            first.series = 17;
            first.source = "Сеть, после отказа";
            Check(first.components == 2 && first.visited == 5, "Disconnected launch records actual coverage");
            std::ostringstream output;
            WriteBenchmarkCsv(output, report.records, true);
            auto csv = output.str();
            Check(csv.find(",series,source,components,visited,graph_status") != std::string::npos &&
                  csv.find(R"(,17,"Сеть, после отказа",2,5,disconnected)") != std::string::npos,
                  "GUI CSV includes the series and disconnected launch details");
        }
    }

}

int main()
{
    try
    {
        TestDictionaryInterchangeability();
        TestConstructionAndTraversals<TreeDictionary<std::size_t, std::size_t>>();
        TestConstructionAndTraversals<ArrayStorage<std::size_t>>();
        TestKeyTypes();
        TestExceptionSafety();
        TestWeightedGraph<TreeDictionary<std::size_t, std::size_t>>();
        TestWeightedGraph<ArrayStorage<std::size_t>>();
        TestCustomWeightAndMinimumSpanningTree();
        TestTraversalEnumerators();
        TestNetworkDesign();
        TestGeneration();
        TestLargeTraversals();
        TestNetworkAndBenchmarks();
        TestPresentationData();
        TestBenchmarkCancellation();
        std::cout << "All tests passed (weighted graph, rollback, enumerators, spanning trees, network design, generation and benchmarks)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}
