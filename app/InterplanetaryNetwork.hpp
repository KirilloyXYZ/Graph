#ifndef INTERPLANETARYNETWORK_HPP
#define INTERPLANETARYNETWORK_HPP

#include <cstddef>
#include <string>

#include "../graph/GraphAlgorithms.hpp"

using StationId = std::string;
using InterplanetaryNetwork = Graph<StationId, double>;

enum class NetworkStatus { Empty, Connected, Disconnected };

struct NetworkAnalysis
{
    NetworkStatus status;
    ConnectedComponentsResult<StationId> components;
};

inline StationId GetGeneratedStationId(std::size_t number)
{
    return "S" + std::to_string(number);
}

inline NetworkAnalysis AnalyzeNetwork(const InterplanetaryNetwork& network,
                                      TraversalFactory<StationId> create = CreateBFSEnumerator<StationId>)
{
    auto components = FindConnectedComponents(network, create);
    std::size_t count = components.componentEnds.GetLength();
    NetworkStatus status = NetworkStatus::Disconnected;
    if (count == 0) { status = NetworkStatus::Empty; }
    else if (count == 1) { status = NetworkStatus::Connected; }
    return {status, std::move(components)};
}

#endif
