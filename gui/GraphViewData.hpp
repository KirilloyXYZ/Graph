#ifndef GRAPHVIEWDATA_HPP
#define GRAPHVIEWDATA_HPP

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../app/NetworkDesign.hpp"
#include "../generator/GraphGenerator.hpp"
#include "../lab2/TreeDictionary.hpp"

inline WorldPoint operator+(WorldPoint left, WorldPoint right)
{
    return {left.x + right.x, left.y + right.y};
}

inline WorldPoint operator-(WorldPoint left, WorldPoint right)
{
    return {left.x - right.x, left.y - right.y};
}

inline constexpr float MapPi = 3.14159265358979323846f;

struct Planet
{
    const char* name;
    float orbitRadius;
    float angle;
    float radius;
    unsigned int color;
};

inline constexpr std::array<Planet, 8> Planets{{
    {"Меркурий", 56, -2.60f, 5, 0xbfa99a},
    {"Венера", 89, -0.38f, 9, 0xe7ba78},
    {"Земля", 128, 0.77f, 11, 0x518de3},
    {"Марс", 166, 2.47f, 8, 0xdd7c61},
    {"Юпитер", 224, -2.17f, 25, 0xcd9b76},
    {"Сатурн", 291, -0.63f, 20, 0xe0c18d},
    {"Уран", 361, 2.10f, 15, 0x83d4df},
    {"Нептун", 428, 0.29f, 15, 0x506fd3}
}};

inline constexpr float OrbitAspect = 0.66f;

inline WorldPoint PlanetPosition(std::size_t index)
{
    const auto& planet = Planets.at(index);
    return {planet.orbitRadius * std::cos(planet.angle),
            planet.orbitRadius * OrbitAspect * std::sin(planet.angle)};
}

using ChannelKey = std::pair<StationId, StationId>;

inline ChannelKey MakeChannelKey(const StationId& first, const StationId& second)
{
    return first < second ? ChannelKey(first, second) : ChannelKey(second, first);
}

struct Placement
{
    WorldPoint offset;
    std::size_t planet = 2;
    bool orbit = false;
};

inline Placement PlaceOnPlanet(std::size_t planet, bool orbit, std::size_t slot)
{
    float angle = -MapPi / 3.0f + 2.399963f * static_cast<float>(slot % 233);
    float radius = Planets.at(planet).radius + (orbit ? 27.0f : 10.0f);
    radius += 11.0f * static_cast<float>(slot / 8);
    return {{radius * std::cos(angle), radius * std::sin(angle)}, planet, orbit};
}

inline WorldPoint PositionOf(const Placement& placement, bool solar)
{
    return solar ? PlanetPosition(placement.planet) + placement.offset : placement.offset;
}

inline WorldPoint CircleLayout(std::size_t slot, std::size_t count, float radius)
{
    if (count <= 1) { return {}; }
    float angle = -MapPi / 2.0f + (2.0f * MapPi * static_cast<float>(slot)) / static_cast<float>(count);
    return {radius * std::cos(angle), radius * std::sin(angle)};
}

inline std::vector<WorldPoint> MakeLayout(Topology topology, std::size_t count, unsigned int seed)
{
    std::vector<WorldPoint> positions(count);
    if (count <= 1) { return positions; }
    std::size_t columns = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(count))));
    std::mt19937 random(seed);
    std::uniform_real_distribution<float> jitter(-9.0f, 9.0f);
    std::size_t levelStart = 0, levelWidth = 1, level = 0;
    for (std::size_t i = 0; i < count; ++i)
    {
        switch (topology)
        {
            case Topology::Chain:
            {
                std::size_t rowSize = std::min<std::size_t>(count, 9);
                std::size_t row = i / rowSize, index = i % rowSize;
                if (row % 2 != 0) { index = rowSize - 1 - index; }
                positions[i] = {static_cast<float>(index) * 73.0f, static_cast<float>(row) * 78.0f};
                break;
            }
            case Topology::Cycle:
            case Topology::Complete:
                positions[i] = CircleLayout(i, count, std::max(110.0f, static_cast<float>(count) * 8.5f));
                break;
            case Topology::Star:
                if (i != 0)
                {
                    positions[i] = CircleLayout(i - 1, count - 1, std::max(110.0f, static_cast<float>(count) * 8.5f));
                    if (count == 2) { positions[i] = {105.0f, 0}; }
                }
                break;
            case Topology::BinaryTree:
            {
                if (i - levelStart == levelWidth)
                {
                    levelStart = i;
                    ++level;
                    levelWidth = levelWidth <= count / 2 ? levelWidth * 2 : count - i;
                }
                float span = std::max(300.0f, static_cast<float>(count) * 31.0f);
                float fraction = (static_cast<float>(i - levelStart) + 0.5f) / static_cast<float>(levelWidth);
                positions[i] = {fraction * span, static_cast<float>(level) * 85.0f};
                break;
            }
            case Topology::Random:
                positions[i] = {static_cast<float>(i % columns) * 72.0f + jitter(random),
                                static_cast<float>(i / columns) * 72.0f + jitter(random)};
                break;
        }
    }
    return positions;
}

class GraphDocument
{
public:
    const bool solar;
    InterplanetaryNetwork graph;
    std::map<StationId, Placement> locations;
    std::optional<GenerationOptions> origin;
    std::vector<std::vector<StationId>> groups;
    std::unordered_map<StationId, std::size_t> groupOf;
    std::size_t revision = 0;

    explicit GraphDocument(bool solar)
        : solar(solar), graph(std::make_unique<TreeDictionary<StationId, std::size_t>>(), CreateArraySequence, CreateArraySequence) { }

    WorldPoint StationPosition(const StationId& key) const
    {
        auto location = locations.find(key);
        if (location == locations.end()) { throw std::out_of_range("Graph: vertex not found"); }
        return PositionOf(location->second, solar);
    }

    void AddChannel(const StationId& first, const StationId& second)
    {
        if (solar) { graph.AddEdge(first, second, ChannelDistance(StationPosition(first), StationPosition(second))); }
        else { graph.AddEdge(first, second, 1.0); }
    }

    // The editor calls this on the copy being prepared for publication.
    void Relocate(const StationId& key, Placement placement)
    {
        auto& previous = locations.at(key);
        auto position = PositionOf(placement, solar);
        if (!IsFinite(position)) { throw std::invalid_argument("Station position must have finite coordinates"); }
        if (solar)
        {
            graph.ForEachNeighbor(key, [&](const StationId& neighbor)
            {
                graph.SetEdgeWeight(key, neighbor, ChannelDistance(position, StationPosition(neighbor)));
            });
        }
        previous = placement;
        ++revision;
    }

    void UpdateConnectivity()
    {
        auto result = AnalyzeNetwork(graph);
        groups.clear();
        groupOf.clear();
        std::size_t begin = 0;
        for (std::size_t i = 0; i < result.components.componentEnds.GetLength(); ++i)
        {
            std::size_t end = result.components.componentEnds.Get(i);
            auto& group = groups.emplace_back();
            group.reserve(end - begin);
            for (std::size_t j = begin; j < end; ++j) { group.push_back(*result.components.order.Get(j)); }
            std::sort(group.begin(), group.end());
            begin = end;
        }
        std::sort(groups.begin(), groups.end(), [](const auto& left, const auto& right)
        {
            return left.front() < right.front();
        });
        groupOf.reserve(graph.GetVertexCount());
        for (std::size_t i = 0; i < groups.size(); ++i)
        {
            for (const auto& key : groups[i]) { groupOf.emplace(key, i); }
        }
    }

    void OnTopologyChange()
    {
        ++revision;
        origin.reset();
        UpdateConnectivity();
    }

    NetworkStatus Status() const
    {
        if (graph.GetVertexCount() == 0) { return NetworkStatus::Empty; }
        return groups.size() == 1 ? NetworkStatus::Connected : NetworkStatus::Disconnected;
    }
};

inline const char* NetworkStatusLabel(NetworkStatus status)
{
    switch (status)
    {
        case NetworkStatus::Empty: return "Пустая";
        case NetworkStatus::Connected: return "Связная";
        case NetworkStatus::Disconnected: return "Несвязная";
    }
    return "—";
}

inline auto BuildDocument(const Sequence<StationId>& vertices, const Sequence<ChannelKey>& edges, bool solar,
                          const std::vector<Placement>& placements = {})
{
    auto document = std::make_shared<GraphDocument>(solar);
    for (std::size_t i = 0; i < vertices.GetLength(); ++i)
    {
        Placement placement;
        if (!placements.empty()) { placement = placements.at(i); }
        else if (solar) { placement = PlaceOnPlanet((i + 2) % Planets.size(), true, i / Planets.size()); }
        else
        {
            placement.offset = {static_cast<float>(i % 8) * 78.0f, static_cast<float>(i / 8) * 78.0f};
        }
        document->locations.emplace(vertices.Get(i), placement);
    }
    ArraySequence<InterplanetaryNetwork::WeightedEdge> weighted(edges.GetLength());
    for (std::size_t i = 0; i < edges.GetLength(); ++i)
    {
        const auto& [first, second] = edges.Get(i);
        double weight = solar ? ChannelDistance(document->StationPosition(first), document->StationPosition(second)) : 1.0;
        weighted.Append({first, second, weight});
    }
    document->graph = InterplanetaryNetwork(vertices, weighted, std::make_unique<TreeDictionary<StationId, std::size_t>>(),
                                           CreateArraySequence, CreateArraySequence);
    document->UpdateConnectivity();
    return document;
}

inline auto ApplyNetworkDesign(const GraphDocument& source, const NetworkDesign& design)
{
    const auto count = source.graph.GetVertexCount();
    if (!source.solar || count == 0 || design.status != SpanningTreeStatus::Found ||
        design.channels.GetLength() != count - 1)
    {
        throw std::invalid_argument("No complete network design to apply");
    }
    ArraySequence<StationId> keys(count);
    for (std::size_t i = 0; i < count; ++i) { keys.Append(source.graph.GetVertexKey(i)); }
    auto updated = std::make_shared<GraphDocument>(true);
    updated->locations = source.locations;
    updated->graph = InterplanetaryNetwork(keys, design.channels,
        std::make_unique<TreeDictionary<StationId, std::size_t>>(), CreateArraySequence, CreateArraySequence);
    updated->revision = source.revision;
    updated->OnTopologyChange();
    return updated;
}

inline auto BuildGeneratedDocument(const GenerationOptions& options)
{
    auto data = GenerateGraphData(options);
    ArraySequence<StationId> vertices(data.vertices.GetLength());
    ArraySequence<ChannelKey> edges(data.edges.GetLength());
    for (std::size_t i = 0; i < data.vertices.GetLength(); ++i)
    {
        vertices.Append(std::to_string(data.vertices.Get(i)));
    }
    for (std::size_t i = 0; i < data.edges.GetLength(); ++i)
    {
        auto [first, second] = data.edges.Get(i);
        edges.Append({vertices.Get(first - 1), vertices.Get(second - 1)});
    }

    auto positions = MakeLayout(options.topology, vertices.GetLength(), options.seed);
    std::vector<Placement> placements;
    placements.reserve(positions.size());
    for (auto position : positions) { placements.push_back({position}); }
    auto document = BuildDocument(vertices, edges, false, placements);
    document->origin = options;
    return document;
}

inline auto BuildDemoNetwork()
{
    const StationId keys[]{
        "Земля", "Терра-1", "Венера", "Марс", "Фобос", "Юпитер", "Европа", "Сатурн", "Титан", "Нептун"
    };
    const ChannelKey edges[]{
        {"Земля", "Терра-1"}, {"Терра-1", "Венера"}, {"Венера", "Марс"},
        {"Марс", "Фобос"}, {"Фобос", "Земля"}, {"Марс", "Земля"},
        {"Фобос", "Юпитер"}, {"Юпитер", "Европа"}, {"Европа", "Сатурн"},
        {"Сатурн", "Титан"}, {"Титан", "Нептун"}, {"Нептун", "Юпитер"}
    };
    const std::size_t planet[]{2, 2, 1, 3, 3, 4, 4, 5, 5, 7};
    std::vector<Placement> placements;
    placements.reserve(std::size(keys));
    for (std::size_t i = 0; i < std::size(keys); ++i)
    {
        placements.push_back(PlaceOnPlanet(planet[i], i == 1 || i == 4 || i == 6 || i == 8,
                                            i == 0 ? 1 : i + 2));
    }
    return BuildDocument(ArraySequence<StationId>(keys, std::size(keys)),
                         ArraySequence<ChannelKey>(edges, std::size(edges)), true, placements);
}

struct SplitGroup
{
    std::size_t before;
    std::vector<std::vector<StationId>> parts;
};

inline std::vector<SplitGroup> FindSplitGroups(const GraphDocument& before, const GraphDocument& after)
{
    std::vector<SplitGroup> splits;
    for (std::size_t i = 0; i < before.groups.size(); ++i)
    {
        std::map<std::size_t, std::vector<StationId>> living;
        for (const auto& key : before.groups[i])
        {
            auto group = after.groupOf.find(key);
            if (group != after.groupOf.end()) { living[group->second].push_back(key); }
        }
        if (living.size() < 2) { continue; }
        auto& split = splits.emplace_back(SplitGroup{i, {}});
        for (auto& part : living) { split.parts.push_back(std::move(part.second)); }
    }
    return splits;
}

#endif
