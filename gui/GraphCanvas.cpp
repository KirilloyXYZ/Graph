#include "GraphCanvas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>

#include "GuiStyle.hpp"

namespace
{
    constexpr std::size_t VisibleVertices = 300;
    constexpr std::size_t VisibleEdges = 3000;
    constexpr std::size_t NeighborVisits = 180000;
    constexpr float MinimumZoom = 0.0008f;
    constexpr float MaximumZoom = 12.0f;

    struct Projection
    {
        ImVec2 middle;
        WorldPoint center;
        float scale;

        ImVec2 Screen(WorldPoint point) const
        {
            return {middle.x + (point.x - center.x) * scale, middle.y + (point.y - center.y) * scale};
        }

        WorldPoint World(ImVec2 point) const
        {
            return {center.x + (point.x - middle.x) / scale, center.y + (point.y - middle.y) / scale};
        }
    };

    float DistanceSquared(ImVec2 first, ImVec2 second)
    {
        float x = first.x - second.x, y = first.y - second.y;
        return x * x + y * y;
    }

    float DistanceToSegment(ImVec2 point, ImVec2 first, ImVec2 second)
    {
        float dx = second.x - first.x, dy = second.y - first.y;
        float length = dx * dx + dy * dy;
        if (length == 0) { return DistanceSquared(point, first); }
        float fraction = std::clamp(((point.x - first.x) * dx + (point.y - first.y) * dy) / length, 0.0f, 1.0f);
        return DistanceSquared(point, {first.x + dx * fraction, first.y + dy * fraction});
    }

    void UpdateVisibleGraph(const GraphDocument& document, CanvasState& state, const CanvasHighlights& highlight)
    {
        const auto& graph = document.graph;
        bool large = graph.GetVertexCount() > VisibleVertices || graph.GetEdgeCount() > VisibleEdges;
        auto focus = large ? highlight.station : std::nullopt;
        auto group = large ? highlight.group : std::nullopt;
        if (state.document == &document && state.revision == document.revision &&
            state.focus == focus && state.focusGroup == group)
        {
            return;
        }

        state.document = &document;
        state.revision = document.revision;
        state.focus = focus;
        state.focusGroup = group;
        state.vertices.clear();
        state.edges.clear();

        std::unordered_map<std::size_t, std::size_t> visible;
        auto append = [&](std::size_t index)
        {
            if (state.vertices.size() >= VisibleVertices) { return; }
            if (visible.emplace(index, state.vertices.size()).second)
            {
                state.vertices.push_back(graph.GetVertexKey(index));
            }
        };

        if (group && *group < document.groups.size())
        {
            for (const auto& key : document.groups[*group])
            {
                if (state.vertices.size() == VisibleVertices) { break; }
                append(*graph.FindVertexIndex(key));
            }
        }
        else if (focus)
        {
            auto index = graph.FindVertexIndex(*focus);
            if (index)
            {
                append(*index);
                for (std::size_t i = 0; i < graph.GetNeighborCount(*index) && state.vertices.size() < VisibleVertices; ++i)
                {
                    append(graph.GetNeighborIndex(*index, i));
                }
            }
        }
        else
        {
            for (std::size_t i = 0; i < graph.GetVertexCount() && state.vertices.size() < VisibleVertices; ++i)
            {
                append(i);
            }
        }

        std::size_t checked = 0;
        for (std::size_t i = 0; i < state.vertices.size(); ++i)
        {
            auto index = *graph.FindVertexIndex(state.vertices[i]);
            for (std::size_t j = 0; j < graph.GetNeighborCount(index); ++j)
            {
                if (checked++ >= NeighborVisits || state.edges.size() >= VisibleEdges) { return; }
                auto neighbor = visible.find(graph.GetNeighborIndex(index, j));
                if (neighbor != visible.end() && i < neighbor->second) { state.edges.emplace_back(i, neighbor->second); }
            }
        }
    }

    void Fit(CanvasState& camera, const GraphDocument& document, ImVec2 size, bool solar, bool system)
    {
        camera.fit = false;
        camera.dragVertex.reset();
        camera.dragPlacement.reset();
        WorldPoint low{-470, -316}, high{470, 316};
        if ((!system || !solar) && !camera.vertices.empty())
        {
            low = high = document.StationPosition(camera.vertices.front());
            for (const auto& key : camera.vertices)
            {
                auto point = document.StationPosition(key);
                low.x = std::min(low.x, point.x);
                low.y = std::min(low.y, point.y);
                high.x = std::max(high.x, point.x);
                high.y = std::max(high.y, point.y);
            }
        }
        else if (!solar) { low = {-200, -130}; high = {200, 130}; }

        camera.center = {(low.x + high.x) * 0.5f, (low.y + high.y) * 0.5f};
        float horizontal = std::max(45.0f, size.x - 90.0f) / std::max(110.0f, high.x - low.x);
        float vertical = std::max(45.0f, size.y - 87.0f) / std::max(110.0f, high.y - low.y);
        camera.zoom = std::clamp(std::min(horizontal, vertical), MinimumZoom, 3.0f);
    }

    void DrawMapBackground(ImDrawList* draw, ImVec2 min, ImVec2 max, bool solar, const Projection& projection)
    {
        draw->AddRectFilledMultiColor(min, max, Theme::Color(0x111f37), Theme::Color(0x0a1527),
                                    Theme::Color(0x070e1c), Theme::Color(0x0b1628));
        if (solar)
        {
            static const auto stars = []
            {
                std::array<std::array<float, 3>, 188> points{};
                std::mt19937 engine(34);
                std::uniform_real_distribution<float> random(0.0f, 1.0f);
                for (auto& star : points) { star = {random(engine), random(engine), random(engine)}; }
                return points;
            }();

            for (const auto& star : stars)
            {
                ImVec2 point{min.x + (max.x - min.x) * star[0], min.y + (max.y - min.y) * star[1]};
                draw->AddCircleFilled(point, 0.45f + star[2] * 0.82f,
                                      Theme::Color(0xabcdff, static_cast<unsigned int>(36.0f + 102.0f * star[2])), 6);
                if (star[2] > 0.97f)
                {
                    draw->AddLine({point.x - 2.5f, point.y}, {point.x + 2.5f, point.y}, Theme::Color(0xabcdff, 86));
                    draw->AddLine({point.x, point.y - 2.5f}, {point.x, point.y + 2.5f}, Theme::Color(0xabcdff, 86));
                }
            }
        }
        else
        {
            float spacing = 36.0f * projection.scale;
            while (spacing < 23.0f) { spacing *= 2.0f; }
            auto origin = projection.Screen({});
            for (float x = min.x + std::fmod(std::fmod(origin.x - min.x, spacing) + spacing, spacing); x < max.x; x += spacing)
            {
                for (float y = min.y + std::fmod(std::fmod(origin.y - min.y, spacing) + spacing, spacing); y < max.y; y += spacing)
                {
                    draw->AddCircleFilled({x, y}, 0.9f, Theme::Color(0x466584, 121), 6);
                }
            }
        }
    }

    std::uint32_t Blend(std::uint32_t from, std::uint32_t to, float weight)
    {
        auto channel = [=](int shift)
        {
            auto first = static_cast<float>((from >> shift) & 255);
            auto second = static_cast<float>((to >> shift) & 255);
            return static_cast<std::uint32_t>(first + (second - first) * weight) << shift;
        };
        return channel(16) | channel(8) | channel(0);
    }

    void DrawBody(ImDrawList* draw, ImVec2 point, float radius, std::uint32_t color, bool sun)
    {
        if (radius < 1.1f) { return; }
        int layers = sun ? 9 : 4;
        for (int i = layers; i > 0; --i)
        {
            float extent = 1.0f + static_cast<float>(i) * (sun ? 0.215f : 0.16f);
            draw->AddCircleFilled(point, radius * extent, Theme::Color(color, sun ? 10 : 6), 38);
        }
        draw->AddCircleFilled(point, radius, Theme::Color(Blend(color, 0x101a32, 0.44f)), 40);
        for (int step = 10; step >= 1; --step)
        {
            float fraction = static_cast<float>(step) / 11.0f;
            auto shade = Blend(color, sun ? 0xfff2b2 : 0xe1edff, (1.0f - fraction) * 0.62f);
            if (!sun) { shade = Blend(shade, 0x101a32, fraction * 0.29f); }
            ImVec2 center{point.x - (1.0f - fraction) * radius * 0.46f, point.y - (1.0f - fraction) * radius * 0.46f};
            draw->AddCircleFilled(center, radius * fraction, Theme::Color(shade), 36);
        }
        if (radius >= 5.0f) { draw->AddCircle(point, radius, Theme::Color(color, 112), 38, 0.85f); }
    }

    void DrawSolarSystem(ImDrawList* draw, const Projection& projection, const GraphDocument& document)
    {
        ImVec2 sun = projection.Screen({});
        for (const auto& planet : Planets)
        {
            float radius = planet.orbitRadius * projection.scale;
            if (radius < 4.0f) { continue; }
            draw->AddEllipse(sun, {radius, radius * OrbitAspect}, Theme::Color(0x5274a1, 75), 0.0f, 128, 0.86f);
        }

        DrawBody(draw, sun, 23.0f * projection.scale, 0xffb758, true);
        if (projection.scale >= 0.50f)
        {
            draw->AddText({sun.x - 23.0f, sun.y + 23.0f * projection.scale + 7.0f},
                          Theme::Color(0xfbd090, 207), "Солнце");
        }

        for (std::size_t i = 0; i < Planets.size(); ++i)
        {
            const auto& planet = Planets[i];
            auto position = projection.Screen(PlanetPosition(i));
            float radius = planet.radius * projection.scale;
            if (i == 5 && radius > 2)
            {
                draw->AddEllipse(position, {radius * 1.78f, radius * 0.48f}, Theme::Color(0xeac99d, 158),
                                 -0.39f, 48, 2.25f);
            }
            DrawBody(draw, position, radius, planet.color, false);

            if ((i == 4 || i == 5) && radius > 6)
            {
                for (float height : {-0.24f, 0.10f, 0.36f})
                {
                    float width = radius * std::sqrt(1.0f - height * height) * 0.95f;
                    float y = position.y + radius * height;
                    draw->AddLine({position.x - width, y - 1.0f}, {position.x + width, y + 1.0f},
                                  Theme::Color(0x6b4d50, 78), std::max(1.0f, radius * 0.11f));
                }
            }
            if (i == 5 && radius > 2)
            {
                for (int step = 0; step <= 22; ++step)
                {
                    float theta = MapPi * static_cast<float>(step) / 22.0f;
                    float x = std::cos(theta) * radius * 1.74f, y = std::sin(theta) * radius * 0.49f;
                    draw->PathLineTo({position.x + x * 0.925f + y * 0.380f,
                                      position.y - x * 0.380f + y * 0.925f});
                }
                draw->PathStroke(Theme::Color(0xf2d3aa, 183), 0, 1.8f);
            }

            auto sameName = document.locations.find(planet.name);
            bool stationLabelsBody = sameName != document.locations.end() && sameName->second.planet == i;
            if (projection.scale >= 0.43f && !stationLabelsBody)
            {
                auto width = ImGui::CalcTextSize(planet.name).x;
                draw->AddText({position.x - width / 2.0f, position.y + radius + 7.0f},
                              Theme::Color(0x98a7be, 213), planet.name);
            }
        }
    }

    void Dash(ImDrawList* draw, ImVec2 first, ImVec2 second, ImU32 color)
    {
        // Ограничение длины штриховой линии держит стоимость кадра при приближении.
        float distance = std::sqrt(DistanceSquared(first, second));
        if (distance < 1.0f) { return; }
        int pieces = static_cast<int>(std::clamp(distance / 12.0f, 1.0f, 180.0f));
        for (int i = 0; i < pieces; ++i)
        {
            float start = static_cast<float>(i) / static_cast<float>(pieces);
            float end = (static_cast<float>(i) + 0.58f) / static_cast<float>(pieces);
            draw->AddLine({first.x + (second.x - first.x) * start, first.y + (second.y - first.y) * start},
                          {first.x + (second.x - first.x) * end, first.y + (second.y - first.y) * end}, color, 1.6f);
        }
    }

    void DrawGhost(ImDrawList* draw, const CanvasHighlights& highlight, const Projection& projection)
    {
        if (!highlight.beforeFailure) { return; }
        const auto& original = *highlight.beforeFailure;
        auto screen = [&](const StationId& id)
        {
            return projection.Screen(original.StationPosition(id));
        };

        if (highlight.offlineChannel)
        {
            const auto& [first, second] = *highlight.offlineChannel;
            Dash(draw, screen(first), screen(second), Theme::Color(Theme::Red, 170));
        }
        if (highlight.offlineStation)
        {
            auto point = screen(*highlight.offlineStation);
            auto at = *original.graph.FindVertexIndex(*highlight.offlineStation);
            auto degree = std::min<std::size_t>(original.graph.GetNeighborCount(at), 32);
            for (std::size_t i = 0; i < degree; ++i)
            {
                const auto& key = original.graph.GetVertexKey(original.graph.GetNeighborIndex(at, i));
                Dash(draw, point, screen(key), Theme::Color(Theme::Red, 118));
            }
            draw->AddCircleFilled(point, 9.0f, Theme::Color(0x281628), 18);
            draw->AddCircle(point, 9.0f, Theme::Color(Theme::Red), 18, 1.7f);
            draw->AddLine({point.x - 4, point.y - 4}, {point.x + 4, point.y + 4}, Theme::Color(Theme::Red), 1.5f);
            draw->AddLine({point.x + 4, point.y - 4}, {point.x - 4, point.y + 4}, Theme::Color(Theme::Red), 1.5f);
            draw->AddText({point.x + 13, point.y - 8}, Theme::Color(Theme::Red), highlight.offlineStation->c_str());
        }
    }

    std::optional<std::size_t> HitStation(ImVec2 mouse, const std::vector<ImVec2>& points)
    {
        float nearest = 12.0f * 12.0f;
        std::optional<std::size_t> hit;
        for (std::size_t i = 0; i < points.size(); ++i)
        {
            float distance = DistanceSquared(mouse, points[i]);
            if (distance < nearest) { hit = i; nearest = distance; }
        }
        return hit;
    }

    void DrawCaption(ImDrawList* draw, ImVec2 top, const std::string& text, ImU32 color)
    {
        auto size = ImGui::CalcTextSize(text.c_str());
        draw->AddRectFilled({top.x - 3, top.y - 1}, {top.x + size.x + 4, top.y + size.y + 1},
                            Theme::Color(0x0a1425, 224), 4.0f);
        draw->AddText(top, color, text.c_str());
    }
}

ImU32 GetComponentColor(std::size_t group)
{
    constexpr std::uint32_t colors[]{
        0x5bdcf8, 0xfbc477, 0xc3a4ff, 0xf3a0d1, 0xa8db8a, 0x81a7ff, 0xffa48d, 0x85e2cb
    };
    return Theme::Color(colors[group % std::size(colors)]);
}

CanvasEvent DrawGraphCanvas(const GraphDocument& document, CanvasState& camera, bool solar,
                            const CanvasHighlights& highlight, bool allowDrag)
{
    UpdateVisibleGraph(document, camera, highlight);
    ImGui::PushID("GraphCanvas");
    ImGui::AlignTextToFramePadding();
    Theme::Overline(solar ? "СОЛНЕЧНАЯ СИСТЕМА" : "СХЕМА ГРАФА");
    ImGui::SameLine();
    bool zoomOut = ImGui::SmallButton("−##zoom");
    ImGui::SameLine();
    bool zoomIn = ImGui::SmallButton("+##zoom");
    ImGui::SameLine();
    if (ImGui::SmallButton(solar ? "Вся сеть" : "Вписать"))
    {
        camera.fit = true;
        camera.fitSystem = false;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Исходный вид"))
    {
        camera.fit = true;
        camera.fitSystem = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%.0f %%", camera.zoom * 100.0f);

    auto root = ImGui::GetCursorScreenPos();
    auto area = ImGui::GetContentRegionAvail();
    area.x = std::max(1.0f, area.x);
    area.y = std::max(170.0f, area.y);
    ImVec2 bottom{root.x + area.x, root.y + area.y};
    ImVec2 middle{root.x + area.x * 0.5f, root.y + area.y * 0.5f};
    if (camera.fit) { Fit(camera, document, area, solar, camera.fitSystem); }
    ImGui::InvisibleButton("canvas", area, ImGuiButtonFlags_MouseButtonLeft |
                                          ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);

    const auto& io = ImGui::GetIO();
    bool hovered = ImGui::IsItemHovered();
    auto mouse = io.MousePos;
    Projection projection{middle, camera.center, camera.zoom};
    ImVec2 anchor = hovered ? mouse : middle;
    float zoomFactor = zoomOut ? 0.82f : (zoomIn ? 1.22f : 1.0f);
    if (hovered && io.MouseWheel != 0) { zoomFactor *= std::pow(1.16f, io.MouseWheel); }
    if (zoomFactor != 1.0f)
    {
        auto atCursor = projection.World(anchor);
        projection.scale = std::clamp(projection.scale * zoomFactor, MinimumZoom, MaximumZoom);
        camera.zoom = projection.scale;
        camera.center = projection.center + (atCursor - projection.World(anchor));
        projection.center = camera.center;
    }

    bool space = ImGui::IsKeyDown(ImGuiKey_Space);
    bool alternatePan = ImGui::IsMouseDown(ImGuiMouseButton_Right) || ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle) ||
                    (space && ImGui::IsMouseClicked(ImGuiMouseButton_Left))))
    {
        camera.panning = true;
        camera.dragVertex.reset();
    }
    if (camera.panning && (alternatePan || (space && ImGui::IsMouseDown(ImGuiMouseButton_Left))))
    {
        camera.center.x -= io.MouseDelta.x / camera.zoom;
        camera.center.y -= io.MouseDelta.y / camera.zoom;
        projection.center = camera.center;
    }
    else { camera.panning = false; }

    std::vector<ImVec2> points;
    points.reserve(camera.vertices.size());
    for (const auto& key : camera.vertices)
    {
        points.push_back(projection.Screen(document.StationPosition(key)));
    }
    auto hit = hovered ? HitStation(mouse, points) : std::nullopt;

    CanvasEvent event;
    if (hovered && !space && !camera.panning && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        if (hit)
        {
            event.station = camera.vertices[*hit];
            if (allowDrag && !highlight.linkMode && !highlight.chooseStart) { camera.dragVertex = event.station; }
        }
        else if (!highlight.linkMode && !highlight.chooseStart)
        {
            float nearest = 7.5f * 7.5f;
            for (const auto& [first, second] : camera.edges)
            {
                auto key = MakeChannelKey(camera.vertices[first], camera.vertices[second]);
                if (highlight.treeOnly && highlight.treeEdges && !highlight.treeEdges->count(key)) { continue; }
                float distance = DistanceToSegment(mouse, points[first], points[second]);
                if (distance < nearest)
                {
                    event.channel = std::move(key);
                    nearest = distance;
                }
            }
            event.background = !event.channel;
        }
    }

    if (camera.dragVertex && allowDrag)
    {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 3.0f))
        {
            if (!camera.dragPlacement) { camera.dragPlacement = document.locations.at(*camera.dragVertex); }
            camera.dragPlacement->offset.x += io.MouseDelta.x / camera.zoom;
            camera.dragPlacement->offset.y += io.MouseDelta.y / camera.zoom;
        }
        if (camera.dragPlacement)
        {
            auto at = std::find(camera.vertices.begin(), camera.vertices.end(), *camera.dragVertex);
            if (at != camera.vertices.end())
            {
                auto index = static_cast<std::size_t>(at - camera.vertices.begin());
                points[index] = projection.Screen(PositionOf(*camera.dragPlacement, solar));
            }
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            {
                event.move.emplace(*camera.dragVertex, *camera.dragPlacement);
            }
        }
    }
    else { camera.dragPlacement.reset(); }
    if (hovered && (highlight.linkMode || highlight.chooseStart)) { ImGui::SetMouseCursor(ImGuiMouseCursor_Hand); }

    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(root, bottom, true);
    DrawMapBackground(draw, root, bottom, solar, projection);
    if (solar) { DrawSolarSystem(draw, projection, document); }

    for (const auto& [first, second] : camera.edges)
    {
        auto key = MakeChannelKey(camera.vertices[first], camera.vertices[second]);
        bool selected = highlight.channel == key;
        bool incident = highlight.station && (key.first == *highlight.station || key.second == *highlight.station);
        std::optional<std::size_t> edgeStep;
        if (highlight.treeEdges)
        {
            auto edge = highlight.treeEdges->find(key);
            if (edge != highlight.treeEdges->end()) { edgeStep = edge->second; }
        }
        if (highlight.treeOnly && !edgeStep) { continue; }
        bool revealed = edgeStep && *edgeStep < highlight.shown;
        ImU32 color = Theme::Color(0x76b6ec, incident ? 222 : 127);
        float thickness = incident ? 2.0f : 1.35f;
        if (highlight.components)
        {
            std::size_t group = document.groupOf.at(key.first);
            color = (GetComponentColor(group) & 0x00ffffffU) | IM_COL32(0, 0, 0, (highlight.group && *highlight.group != group) ? 31 : 171);
        }
        if (highlight.treeEdges)
        {
            if (revealed)
            {
                draw->AddLine(points[first], points[second], Theme::Color(Theme::Green, 42), 6.0f);
                color = Theme::Color(Theme::Green);
                thickness = 2.55f;
            }
            else { color = Theme::Color(0x557699, edgeStep ? 127 : 56); }
        }
        if (selected)
        {
            draw->AddLine(points[first], points[second], Theme::Color(Theme::Blue, 77), 7.0f);
            color = revealed ? Theme::Color(Theme::Green) : Theme::Color(Theme::Blue);
            thickness = 3.2f;
        }
        draw->AddLine(points[first], points[second], color, thickness);
    }

    if (highlight.design && !camera.dragPlacement)
    {
        std::unordered_map<StationId, std::size_t> visible;
        visible.reserve(camera.vertices.size());
        for (std::size_t i = 0; i < camera.vertices.size(); ++i) { visible.emplace(camera.vertices[i], i); }
        const auto& proposed = highlight.design->channels;
        for (std::size_t i = 0; i < proposed.GetLength(); ++i)
        {
            const auto& edge = proposed.Get(i);
            auto first = visible.find(edge.first);
            auto second = visible.find(edge.second);
            if (first != visible.end() && second != visible.end())
            {
                Dash(draw, points[first->second], points[second->second], Theme::Color(Theme::Green));
            }
        }
    }

    if (solar)
    {
        DrawGhost(draw, highlight, projection);
        if (highlight.station && document.locations.count(*highlight.station))
        {
            const auto& placement = camera.dragVertex == highlight.station && camera.dragPlacement
                ? *camera.dragPlacement : document.locations.at(*highlight.station);
            auto marker = projection.Screen(PositionOf(placement, true));
            auto body = projection.Screen(PlanetPosition(placement.planet));
            draw->AddLine(body, marker, Theme::Color(0xa9c5df, 82));
        }
    }
    bool showNames = camera.vertices.size() < 28 || (camera.vertices.size() < 100 && camera.zoom >= 0.58f);
    for (std::size_t i = 0; i < camera.vertices.size(); ++i)
    {
        const auto& key = camera.vertices[i];
        auto point = points[i];
        bool selected = highlight.station == key || highlight.linkStart == key;
        bool underPointer = hit && *hit == i;
        bool current = false, visited = false;
        if (highlight.steps)
        {
            auto step = highlight.steps->find(key);
            if (step != highlight.steps->end())
            {
                visited = step->second < highlight.shown;
                current = visited && step->second + 1 == highlight.shown;
            }
        }
        auto color = Theme::Color(Theme::Blue);
        if (highlight.components)
        {
            auto group = document.groupOf.at(key);
            color = GetComponentColor(group);
            if (highlight.group && *highlight.group != group) { color = Theme::Color(0x586c84, 130); }
        }
        if (visited && !highlight.components) { color = Theme::Color(0x8be8f9); }
        if (current) { color = Theme::Color(0xedf8ff); }
        if (highlight.start == key) { draw->AddCircle(point, 10.0f, Theme::Color(Theme::Purple, 188), 22, 1.8f); }

        float radius = selected || current ? 6.4f : 4.5f;
        if (selected || current || underPointer || visited)
        {
            draw->AddCircleFilled(point, radius + 9.0f, Theme::Color(Theme::Blue, current ? 50 : 26), 24);
            draw->AddCircle(point, radius + 4.0f, Theme::Color(Theme::Blue, selected || current ? 195 : 65), 24, 1.25f);
        }
        draw->AddCircleFilled(point, radius + 2.1f, Theme::Color(0x0a1322), 20);
        draw->AddCircleFilled(point, radius, color, 20);
        draw->AddCircleFilled({point.x - 1.25f, point.y - 1.45f}, 1.45f, Theme::Color(0xeafaff, 199), 8);
        if (showNames || selected || underPointer || current)
        {
            DrawCaption(draw, {point.x + radius + 7.0f, point.y - 9.0f}, key,
                        selected || current ? Theme::Color(0xf4f8ff) : Theme::Color(0xc6dcf3));
        }
    }

    if (camera.vertices.empty())
    {
        DrawCaption(draw, {root.x + 18, root.y + 15},
                    solar ? "Добавьте станцию или загрузите пример" : "Создайте граф вручную или выберите топологию",
                    Theme::Color(Theme::Muted));
    }
    else if (camera.vertices.size() < document.graph.GetVertexCount() || camera.edges.size() < document.graph.GetEdgeCount())
    {
        std::string label = "На схеме " + std::to_string(camera.vertices.size()) + " / " +
                            std::to_string(document.graph.GetVertexCount()) + " вершин · " +
                            std::to_string(camera.edges.size()) + " / " + std::to_string(document.graph.GetEdgeCount()) + " рёбер";
        DrawCaption(draw, {root.x + 14, root.y + 12}, label, Theme::Color(Theme::Muted));
    }

    const char* hint = highlight.linkMode ? (highlight.linkStart ? "Выберите вторую станцию · Esc — отмена" :
                         "Выберите первую станцию · Esc — отмена") :
                       (highlight.chooseStart ? "Выберите начальную станцию · Esc — отмена" :
                         "Прокрутка — масштаб · Пробел и перетаскивание — обзор");
    DrawCaption(draw, {root.x + 14, bottom.y - 29}, hint, Theme::Color(Theme::Muted));
    draw->PopClipRect();
    draw->AddRect(root, bottom, Theme::Color(Theme::Border), 2.0f);
    ImGui::PopID();
    if (!allowDrag || !ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        camera.dragVertex.reset();
        camera.dragPlacement.reset();
    }
    return event;
}

void DrawTopologyPreview(Topology topology, ImDrawList* draw, ImVec2 position, ImVec2 size, bool selected)
{
    struct Mini
    {
        Topology topology;
        GeneratedGraphData data;
        std::vector<WorldPoint> points;
    };
    static const auto previews = []
    {
        std::vector<Mini> result;
        for (auto shape : {Topology::Chain, Topology::Cycle, Topology::Complete, Topology::Star, Topology::BinaryTree, Topology::Random})
        {
            std::size_t count = shape == Topology::Complete ? 5 : (shape == Topology::BinaryTree ? 7 : 6);
            result.push_back({shape, GenerateGraphData({shape, count, 7, 11}), MakeLayout(shape, count, 11)});
        }
        return result;
    }();
    auto at = std::find_if(previews.begin(), previews.end(), [=](const auto& item) { return item.topology == topology; });
    if (at == previews.end()) { return; }
    auto low = at->points.front(), high = low;
    for (auto point : at->points)
    {
        low.x = std::min(low.x, point.x); low.y = std::min(low.y, point.y);
        high.x = std::max(high.x, point.x); high.y = std::max(high.y, point.y);
    }
    Projection projection{{position.x + size.x * 0.5f, position.y + size.y * 0.5f},
                           {(low.x + high.x) / 2.0f, (low.y + high.y) / 2.0f},
                           std::min(size.x / std::max(25.0f, high.x - low.x),
                                    size.y / std::max(25.0f, high.y - low.y)) * 0.8f};
    for (std::size_t i = 0; i < at->data.edges.GetLength(); ++i)
    {
        auto [u, v] = at->data.edges.Get(i);
        draw->AddLine(projection.Screen(at->points[u - 1]), projection.Screen(at->points[v - 1]),
                      Theme::Color(selected ? 0x70c6f7 : 0x6089b1, 194), 1.12f);
    }
    for (auto point : at->points)
    {
        draw->AddCircleFilled(projection.Screen(point), 2.6f, Theme::Color(selected ? Theme::Blue : 0x91b7d9), 10);
    }
}
