#ifndef GRAPHCANVAS_HPP
#define GRAPHCANVAS_HPP

#include <map>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

#include "GraphViewData.hpp"
#include "imgui.h"

struct CanvasState
{
    WorldPoint center;
    float zoom = 1.0f;
    bool fit = true;
    bool fitSystem = true;
    std::optional<StationId> dragVertex;
    bool panning = false;

    const GraphDocument* document = nullptr;
    std::size_t revision = 0;
    std::optional<StationId> focus;
    std::optional<std::size_t> focusGroup;
    std::vector<StationId> vertices;
    std::vector<std::pair<std::size_t, std::size_t>> edges;
};

struct CanvasHighlights
{
    std::optional<StationId> station;
    std::optional<ChannelKey> channel;
    std::optional<StationId> start;
    std::optional<StationId> linkStart;
    std::optional<std::size_t> group;
    const std::unordered_map<StationId, std::size_t>* steps = nullptr;
    const std::map<ChannelKey, std::size_t>* treeEdges = nullptr;
    std::size_t shown = 0;
    bool components = false;
    bool treeOnly = false;
    bool linkMode = false;
    bool chooseStart = false;
    const GraphDocument* beforeFailure = nullptr;
    std::optional<StationId> offlineStation;
    std::optional<ChannelKey> offlineChannel;
};

struct CanvasEvent
{
    std::optional<StationId> station;
    std::optional<ChannelKey> channel;
    bool background = false;
};

CanvasEvent DrawGraphCanvas(GraphDocument& document, CanvasState& camera, bool solar,
                            const CanvasHighlights& highlight, bool allowDrag);
void DrawTopologyPreview(Topology topology, ImDrawList* draw, ImVec2 position, ImVec2 size, bool selected);
ImU32 GetComponentColor(std::size_t group);

#endif
