#ifndef NETWORKDESIGN_HPP
#define NETWORKDESIGN_HPP

#include <cmath>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>

#include "InterplanetaryNetwork.hpp"
#include "../generator/GraphGenerator.hpp"
#include "../graph/MinimumSpanningTree.hpp"
#include "../lab2/TreeDictionary.hpp"

struct WorldPoint
{
    float x = 0.0f;
    float y = 0.0f;
};

inline bool IsFinite(WorldPoint point)
{
    return std::isfinite(point.x) && std::isfinite(point.y);
}

inline double ChannelDistance(WorldPoint first, WorldPoint second)
{
    if (!IsFinite(first) || !IsFinite(second))
    {
        throw std::invalid_argument("Station position must have finite coordinates");
    }
    return std::hypot(static_cast<double>(second.x) - first.x, static_cast<double>(second.y) - first.y);
}

struct NetworkDesign
{
    SpanningTreeStatus status = SpanningTreeStatus::Empty;
    ArraySequence<InterplanetaryNetwork::WeightedEdge> channels;
    double totalLength = 0;
};

inline std::optional<NetworkDesign> DesignNetwork(const InterplanetaryNetwork& source,
                                                   const Sequence<WorldPoint>& positions,
                                                   const std::function<bool()>& shouldStop = {})
{
    const auto count = source.GetVertexCount();
    if (positions.GetLength() != count) { throw std::invalid_argument("Missing station positions"); }
    const auto stopped = [&] { return shouldStop && shouldStop(); };
    if (stopped()) { return std::nullopt; }

    ArraySequence<StationId> keys(count);
    for (std::size_t i = 0; i < count; ++i)
    {
        if (!IsFinite(positions.Get(i))) { throw std::invalid_argument("Station position must have finite coordinates"); }
        keys.Append(source.GetVertexKey(i));
    }

    const auto edgeCount = GetGeneratedEdgeCount({Topology::Complete, count});
    InterplanetaryNetwork potential(std::make_unique<TreeDictionary<StationId, std::size_t>>(),
                                    CreateArraySequence, CreateArraySequence);
    {
        ArraySequence<InterplanetaryNetwork::WeightedEdge> edges(edgeCount);
        for (std::size_t u = 0; u < count; ++u)
        {
            if (stopped()) { return std::nullopt; }
            for (std::size_t v = u + 1; v < count; ++v)
            {
                edges.Append({keys.Get(u), keys.Get(v), ChannelDistance(positions.Get(u), positions.Get(v))});
            }
        }
        if (stopped()) { return std::nullopt; }
        potential = InterplanetaryNetwork(keys, edges, std::make_unique<TreeDictionary<StationId, std::size_t>>(),
                                         CreateArraySequence, CreateArraySequence);
    }

    if (stopped()) { return std::nullopt; }
    auto tree = FindMinimumSpanningTree(potential);
    if (stopped()) { return std::nullopt; }

    NetworkDesign design;
    design.status = tree.status;
    design.channels.Reserve(tree.edges.GetLength());
    for (std::size_t i = 0; i < tree.edges.GetLength(); ++i)
    {
        const auto& edge = tree.edges.Get(i);
        design.channels.Append({*tree.vertices.Get(edge.first), *tree.vertices.Get(edge.second), edge.weight});
        design.totalLength += edge.weight;
    }
    return design;
}

#endif
