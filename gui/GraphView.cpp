#include "GraphView.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>
#include <sstream>

#include "GuiStyle.hpp"
#include "misc/cpp/imgui_stdlib.h"

namespace
{
    constexpr std::size_t PageLength = 36;

    constexpr const char* TopologyNames[]{
        "Цепочка", "Цикл", "Полный граф", "Звезда", "Двоичное дерево", "Случайный граф"
    };

    constexpr const char* ActionNames[]{"Обход", "Компоненты", "Остов", "Минимальный остов"};
    constexpr std::pair<const char*, TraversalFactory<StationId>> Traversals[]{
        {"BFS", CreateBFSEnumerator<StationId>}, {"DFS", CreateDFSEnumerator<StationId>}
    };

    bool IsLarge(const GraphDocument& source)
    {
        return source.graph.GetVertexCount() > 2500 || source.graph.GetEdgeCount() > 16000;
    }

    std::string Trim(const std::string& text)
    {
        auto begin = text.find_first_not_of(" \t\r\n");
        if (begin == std::string::npos) { return {}; }
        return text.substr(begin, text.find_last_not_of(" \t\r\n") - begin + 1);
    }

    std::string UserError(const std::exception& error)
    {
        std::string detail = error.what();
        if (detail.find("duplicate key") != std::string::npos) { return "Такой идентификатор уже используется."; }
        if (detail.find("duplicate edge") != std::string::npos) { return "Такой канал или ребро уже есть."; }
        if (detail.find("loop") != std::string::npos) { return "Выберите две разные вершины."; }
        if (detail.find("vertex not found") != std::string::npos) { return "Одна из указанных вершин отсутствует."; }
        if (detail.find("edge not found") != std::string::npos) { return "Связь между выбранными вершинами отсутствует."; }
        if (detail.find("cycle needs") != std::string::npos) { return "Для цикла нужны как минимум три вершины."; }
        if (detail.find("topology needs") != std::string::npos) { return "У этой топологии должна быть хотя бы одна вершина."; }
        if (detail.find("too many edges") != std::string::npos) { return "Число рёбер превышает допустимое для выбранного размера."; }
        if (detail.find("too many possible edges") != std::string::npos) { return "Размер графа слишком велик для расчёта числа рёбер."; }
        if (dynamic_cast<const std::bad_alloc*>(&error)) { return "Для этой операции не хватило памяти."; }
        return detail;
    }

    bool DrawPlanetPicker(const char* label, int& planet)
    {
        bool changed = false;
        if (!ImGui::BeginCombo(label, Planets[static_cast<std::size_t>(planet)].name)) { return false; }
        for (std::size_t i = 0; i < Planets.size(); ++i)
        {
            if (ImGui::Selectable(Planets[i].name, static_cast<int>(i) == planet))
            {
                planet = static_cast<int>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
        return changed;
    }

    void DrawKeys(const std::vector<StationId>& keys, float height, std::optional<StationId>& selection)
    {
        if (ImGui::BeginChild("KeyList", {0.0f, height}, ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(std::min<std::size_t>(keys.size(), INT_MAX)));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    ImGui::PushID(i);
                    const auto& key = keys[static_cast<std::size_t>(i)];
                    if (ImGui::Selectable(key.c_str(), selection == key)) { selection = key; }
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
    }

    void ColumnText(const char* text)
    {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(text);
    }

    ArraySequence<StationId> ParseKeys(const std::string& lines)
    {
        ArraySequence<StationId> keys;
        std::istringstream input(lines);
        std::string row;
        while (std::getline(input, row))
        {
            auto key = Trim(row);
            if (!key.empty()) { keys.Append(key); }
        }
        return keys;
    }

    ArraySequence<ChannelKey> ParseChannels(const std::string& lines)
    {
        ArraySequence<ChannelKey> edges;
        std::istringstream input(lines);
        std::string row;
        std::size_t line = 0;
        while (std::getline(input, row))
        {
            ++line;
            if (Trim(row).empty()) { continue; }
            StationId first, second;
            auto tab = row.find('\t');
            if (tab != std::string::npos)
            {
                first = Trim(row.substr(0, tab));
                second = Trim(row.substr(tab + 1));
            }
            else
            {
                std::istringstream tokens(row);
                std::string extra;
                if (!(tokens >> first >> second) || tokens >> extra) { first.clear(); }
            }
            if (first.empty() || second.empty())
            {
                throw std::invalid_argument("Строка " + std::to_string(line) + " должна содержать два ключа.");
            }
            edges.Append({std::move(first), std::move(second)});
        }
        return edges;
    }

    void DrawMetric(const char* name, const std::string& value, std::uint32_t color, float width)
    {
        ImGui::PushID(name);
        if (ImGui::BeginChild("Metric", {width, 63.0f}, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::PushFont(nullptr, 12.0f);
            ImGui::TextColored(Theme::Tint(Theme::Muted), "%s", name);
            ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(color));
            Theme::Heading(value.c_str(), 21.0f);
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::PopID();
    }
}

bool DrawStationPicker(const char* label, const GraphDocument& document, std::optional<StationId>& selection)
{
    if (document.graph.GetVertexCount() == 0)
    {
        ImGui::TextDisabled("%s · список пуст", label);
        return false;
    }

    bool changed = false;
    ImGui::PushID(label);
    ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##Key", selection ? selection->c_str() : "Выбрать…"))
    {
        static std::string filter;
        if (ImGui::IsWindowAppearing()) { filter.clear(); }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##KeySearch", "Поиск по идентификатору", &filter);
        auto draw = [&](std::size_t index)
        {
            const auto& key = document.graph.GetVertexKey(index);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(key.c_str(), selection == key)) { selection = key; changed = true; }
            ImGui::PopID();
        };
        if (filter.empty())
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(std::min<std::size_t>(document.graph.GetVertexCount(), INT_MAX)));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) { draw(static_cast<std::size_t>(i)); }
            }
        }
        else
        {
            std::size_t matches = 0;
            for (std::size_t i = 0; i < document.graph.GetVertexCount(); ++i)
            {
                if (document.graph.GetVertexKey(i).find(filter) == std::string::npos) { continue; }
                if (++matches > 75) { ImGui::TextDisabled("Уточните поиск"); break; }
                draw(i);
            }
            if (matches == 0) { ImGui::TextDisabled("Совпадений нет"); }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

void DrawTopologyPicker(Topology& selected)
{
    auto gap = ImGui::GetStyle().ItemSpacing.x;
    float width = (ImGui::GetContentRegionAvail().x - gap) * 0.5f;
    for (int i = 0; i < 6; ++i)
    {
        ImGui::PushID(i);
        if (i % 2 != 0) { ImGui::SameLine(); }
        auto type = static_cast<Topology>(i);
        auto p = ImGui::GetCursorScreenPos();
        bool current = selected == type;
        if (ImGui::InvisibleButton("Topology", {width, 55.0f})) { selected = type; }
        auto* draw = ImGui::GetWindowDrawList();
        ImVec2 end{p.x + width, p.y + 55.0f};
        draw->AddRectFilled(p, end, Theme::Color(current ? 0x1b3953 : (ImGui::IsItemHovered() ? 0x1c2c44 : Theme::Inset)), 6);
        draw->AddRect(p, end, Theme::Color(current ? 0x4f9cc6 : Theme::Border), 6);
        DrawTopologyPreview(type, draw, {p.x + width * 0.19f, p.y + 3}, {width * 0.62f, 29}, current);
        ImGui::PushFont(nullptr, 12.0f);
        auto size = ImGui::CalcTextSize(TopologyNames[i]);
        draw->AddText({p.x + (width - size.x) * 0.5f, p.y + 36}, Theme::Color(current ? Theme::Text : Theme::Muted),
                      TopologyNames[i]);
        ImGui::PopFont();
        ImGui::PopID();
    }
}

GraphView::GraphView(bool network)
    : solar(network)
{
    document = network ? BuildDemoNetwork() : BuildGeneratedDocument(generation);
    if (solar) { station = "Земля"; }
    else { lastGeneration = generation; }
    start = document->graph.GetVertexKey(0);
}

void GraphView::Say(std::string detail, bool failed)
{
    message = std::move(detail);
    error = failed;
}

void GraphView::Try(const std::function<void()>& operation)
{
    try { operation(); }
    catch (const std::exception& exception) { Say(UserError(exception), true); }
}

void GraphView::ConfirmReplacement(std::string text, std::function<void()> operation)
{
    confirmation = std::move(operation);
    confirmationText = std::move(text);
    openConfirmation = true;
}

void GraphView::ResetSelection(bool keep)
{
    auto active = Current();
    auto exists = [&](const auto& key) { return key && active->graph.ContainsVertex(*key); };
    if (!keep || !exists(station)) { station.reset(); }
    if (!keep || !channel || !active->graph.ContainsEdge(channel->first, channel->second)) { channel.reset(); }
    if (!keep || !exists(start))
    {
        if (active->graph.GetVertexCount() != 0) { start = active->graph.GetVertexKey(0); }
        else { start.reset(); }
    }
    if (!keep || !exists(first)) { first.reset(); }
    if (!keep || !exists(second)) { second.reset(); }
    linkMode = pickStart = false;
    group.reset();
    analysis.reset();
    canvas.document = nullptr;
    canvas.dragVertex.reset();
    canvas.dragPlacement.reset();
    edgePageSource = nullptr;
    edgePageNumber = 0;
}

void GraphView::SetDocument(std::shared_ptr<GraphDocument> next, bool replaceView)
{
    document = std::move(next);
    ResetSelection(!replaceView);
    if (replaceView) { canvas.fit = true; canvas.fitSystem = solar; }
}

void GraphView::Change(GuiTask& job, const char* name, std::function<void(GraphDocument&)> operation,
                       std::optional<StationId> select, bool topologyChanged)
{
    auto source = topologyChanged ? document : Current();
    auto change = [source, operation = std::move(operation), topologyChanged]()
    {
        auto updated = std::make_shared<GraphDocument>(*source);
        operation(*updated);
        if (topologyChanged) { updated->OnTopologyChange(); }
        return updated;
    };
    auto publish = [this, source, topologyChanged, select = std::move(select), name = std::string(name)](
                       std::shared_ptr<GraphDocument> updated)
    {
        if (topologyChanged) { SetDocument(std::move(updated), false); }
        else
        {
            if (Current() != source) { return; }
            if (trial) { trial = std::move(updated); }
            else { document = std::move(updated); }
            if (analysis && analysis->design) { analysis.reset(); }
            canvas.document = nullptr;
            edgePageSource = nullptr;
        }
        if (select) { station = select; channel.reset(); }
        Say(name);
    };
    if (IsLarge(*source))
    {
        job.Start(this, name, 0, 0, [change = std::move(change), publish = std::move(publish)](GuiTask& task)
        {
            auto updated = change();
            return [updated = std::move(updated), publish = std::move(publish), &task]() mutable
            {
                if (!task.Stopping()) { publish(std::move(updated)); }
            };
        });
    }
    else { publish(change()); }
}

void GraphView::AddConnection(GuiTask& job, const StationId& u, const StationId& v)
{
    Change(job, solar ? "Канал добавлен" : "Ребро добавлено", [u, v](GraphDocument& next) { next.AddChannel(u, v); });
    first.reset();
    second.reset();
    linkMode = false;
}

void GraphView::MoveStation(GuiTask& job, const StationId& key, Placement placement)
{
    if (trial && !showTrial) { return; }
    Change(job, solar ? "Станция перемещена" : "Вершина перемещена",
           [key, placement](GraphDocument& next) { next.Relocate(key, placement); }, std::nullopt, false);
}

void GraphView::Generate(GuiTask& job, GenerationOptions options)
{
    GetGeneratedEdgeCount(options);
    job.Start(this, "Генерация графа", 0, 0, [this, options](GuiTask& task)
    {
        auto next = BuildGeneratedDocument(options);
        return [this, options, next = std::move(next), stopped = task.Stopping()]
        {
            if (stopped) { Say("Генерация остановлена"); return; }
            SetDocument(next, true);
            lastGeneration = options;
            Say("Граф готов");
        };
    });
}

void GraphView::Transfer(GuiTask& job, GraphView& target)
{
    auto source = document;
    job.Start(&target, "Подготовка сети", 0, 0, [source, &target](GuiTask& task)
    {
        const auto& graph = source->graph;
        ArraySequence<StationId> keys(graph.GetVertexCount());
        ArraySequence<ChannelKey> edges(graph.GetEdgeCount());
        for (std::size_t i = 0; i < graph.GetVertexCount(); ++i) { keys.Append(graph.GetVertexKey(i)); }
        graph.ForEachEdge([&](const StationId& u, const StationId& v) { edges.Append({u, v}); });
        auto next = BuildDocument(keys, edges, true);
        next->origin = source->origin;
        return [&target, next = std::move(next), stopped = task.Stopping()]
        {
            if (stopped) { target.Say("Перенос остановлен"); return; }
            target.SetDocument(next, true);
            target.Say("Сеть создана из обычного графа");
        };
    });
}

void GraphView::StartAnalysis(GuiTask& job)
{
    auto source = Current();
    auto seed = start ? source->graph.FindVertexIndex(*start) : std::nullopt;
    const bool design = action == AnalysisAction::MinimumSpanningTree;
    if (action == AnalysisAction::Traversal && !seed)
    {
        throw std::invalid_argument("Выберите начальную вершину.");
    }
    auto compute = [source, chosen = action, traversal = Traversals[traversalChoice], seed](
                       const std::function<bool()>& shouldStop) -> std::shared_ptr<Analysis>
    {
        auto result = std::make_shared<Analysis>();
        result->action = chosen;
        result->algorithm = traversal.first;
        result->revision = source->revision;
        switch (chosen)
        {
            case AnalysisAction::Traversal:
                result->keys = Traverse(source->graph, *seed, traversal.second);
                break;
            case AnalysisAction::Components:
                result->keys = FindConnectedComponents(source->graph, traversal.second).order;
                break;
            case AnalysisAction::SpanningTree:
            {
                auto tree = FindSpanningTree(source->graph, seed, traversal.second);
                result->status = tree.status;
                result->keys = std::move(tree.order);
                for (std::size_t i = 0; i < tree.edges.GetLength(); ++i)
                {
                    auto [parent, child] = tree.edges.Get(i);
                    result->tree.emplace(MakeChannelKey(*result->keys.Get(parent), *result->keys.Get(child)), child);
                }
                break;
            }
            case AnalysisAction::MinimumSpanningTree:
            {
                ArraySequence<WorldPoint> positions(source->graph.GetVertexCount());
                for (std::size_t i = 0; i < source->graph.GetVertexCount(); ++i)
                {
                    positions.Append(source->StationPosition(source->graph.GetVertexKey(i)));
                }
                result->design = DesignNetwork(source->graph, positions, shouldStop);
                return result->design ? result : nullptr;
            }
        }
        result->steps.reserve(result->keys.GetLength());
        for (std::size_t i = 0; i < result->keys.GetLength(); ++i) { result->steps.emplace(*result->keys.Get(i), i); }
        result->shown = result->keys.GetLength() == 0 ? 0 : 1;
        result->playing = result->keys.GetLength() > 1;
        return result;
    };
    auto publish = [this, source](std::shared_ptr<Analysis> result)
    {
        if (Current() != source) { return; }
        if (!result) { Say("Расчёт остановлен"); return; }
        if (source->revision != result->revision) { return; }
        analysis = std::move(result);
        showInspector = true;
        group.reset();
        Say("Результат анализа готов");
    };
    const bool large = design
        ? GetGeneratedEdgeCount({Topology::Complete, source->graph.GetVertexCount()}) > 16000
        : IsLarge(*source);
    if (large)
    {
        job.Start(this, design ? "Проектирование сети" : "Анализ графа", 0, 0,
                  [compute = std::move(compute), publish = std::move(publish)](GuiTask& task)
        {
            auto result = compute([&task] { return task.Stopping(); });
            return [result = std::move(result), publish = std::move(publish), &task]() mutable
            {
                publish(task.Stopping() ? nullptr : std::move(result));
            };
        });
    }
    else { publish(compute({})); }
}

void GraphView::BeginTrial(GuiTask& job)
{
    auto publish = [this](std::shared_ptr<GraphDocument> next)
    {
        trial = std::move(next);
        showTrial = true;
        offlineStation.reset();
        offlineChannel.reset();
        splits.clear();
        ResetSelection(true);
        Say("Эксперимент начат. Выберите объект для отключения.");
    };
    if (IsLarge(*document))
    {
        job.Start(this, "Подготовка эксперимента", 0, 0, [source = document, publish = std::move(publish)](GuiTask& task)
        {
            auto copy = std::make_shared<GraphDocument>(*source);
            return [copy = std::move(copy), publish = std::move(publish), stopped = task.Stopping()]() mutable
            {
                if (!stopped) { publish(std::move(copy)); }
            };
        });
    }
    else { publish(std::make_shared<GraphDocument>(*document)); }
}

void GraphView::ApplyFailure(GuiTask& job, std::optional<StationId> key, std::optional<ChannelKey> edge)
{
    if (!trial || offlineStation || offlineChannel || (!key && !edge)) { return; }
    auto compute = [original = document, source = trial, key, edge]()
    {
        auto next = std::make_shared<GraphDocument>(*source);
        if (key)
        {
            next->graph.RemoveVertex(*key);
            next->locations.erase(*key);
        }
        else { next->graph.RemoveEdge(edge->first, edge->second); }
        next->OnTopologyChange();
        auto affected = FindSplitGroups(*original, *next);
        return std::make_pair(std::move(next), std::move(affected));
    };
    auto publish = [this, key = std::move(key), edge = std::move(edge)](auto result)
    {
        trial = std::move(result.first);
        splits = std::move(result.second);
        showTrial = true;
        offlineStation = key;
        offlineChannel = edge;
        ResetSelection(true);
        Say("Отказ применён к копии сети");
    };
    if (IsLarge(*trial))
    {
        job.Start(this, "Моделирование отказа", 0, 0, [compute = std::move(compute), publish = std::move(publish)](GuiTask& task)
        {
            auto result = compute();
            return [result = std::move(result), publish = std::move(publish), stopped = task.Stopping()]() mutable
            {
                if (!stopped) { publish(std::move(result)); }
            };
        });
    }
    else { publish(compute()); }
}

void GraphView::FinishTrial(GuiTask& job, bool restore)
{
    if (restore) { BeginTrial(job); return; }
    trial.reset();
    offlineStation.reset();
    offlineChannel.reset();
    splits.clear();
    ResetSelection(true);
    Say("Исходная сеть восстановлена");
}

void GraphView::DrawSummary()
{
    auto current = Current();
    float gap = ImGui::GetStyle().ItemSpacing.x;
    float width = (ImGui::GetContentRegionAvail().x - 3.0f * gap) / 4.0f;
    DrawMetric(solar ? "Станции" : "Вершины", std::to_string(current->graph.GetVertexCount()), Theme::Text, width);
    ImGui::SameLine();
    DrawMetric(solar ? "Каналы связи" : "Рёбра", std::to_string(current->graph.GetEdgeCount()), Theme::Text, width);
    ImGui::SameLine();
    DrawMetric("Компоненты", std::to_string(current->groups.size()), Theme::Blue, width);
    ImGui::SameLine();
    auto status = current->Status();
    auto color = status == NetworkStatus::Connected ? Theme::Green :
        (status == NetworkStatus::Disconnected ? Theme::Red : Theme::Muted);
    std::string label = NetworkStatusLabel(status);
    if (!solar)
    {
        if (status == NetworkStatus::Connected) { label = "Связный"; }
        if (status == NetworkStatus::Empty) { label = "Пустой"; }
        if (status == NetworkStatus::Disconnected) { label = "Несвязный"; }
    }
    DrawMetric("Состояние", label, color, width);
}

void GraphView::DrawEditor(GuiTask& job, GraphView* network)
{
    auto current = Current();
    Theme::Heading(solar ? "Управление сетью" : "Настройка графа", 19.0f);

    ImGui::BeginDisabled(job.Busy() || IsExperiment());
    if (!solar && ImGui::CollapsingHeader("Генерация", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawTopologyPicker(generation.topology);
        ImU64 count = generation.vertexCount;
        ImGui::SetNextItemWidth(-90.0f);
        if (ImGui::InputScalar("Вершины", ImGuiDataType_U64, &count))
        {
            generation.vertexCount = static_cast<std::size_t>(count);
        }
        if (generation.topology == Topology::Random)
        {
            ImU64 edges = generation.edgeCount;
            ImGui::SetNextItemWidth(-90.0f);
            if (ImGui::InputScalar("Рёбра", ImGuiDataType_U64, &edges))
            {
                generation.edgeCount = static_cast<std::size_t>(edges);
            }
            ImGui::SetNextItemWidth(-90.0f);
            ImGui::InputScalar("Seed", ImGuiDataType_U32, &generation.seed);
            if (ImGui::SmallButton("Новый seed"))
            {
                Try([&] { generation.seed = std::random_device{}(); });
            }
        }
        std::string parameterError;
        try { GetGeneratedEdgeCount(generation); }
        catch (const std::exception& exception) { parameterError = UserError(exception); }
        if (!parameterError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(Theme::Red));
            ImGui::TextWrapped("%s", parameterError.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::BeginDisabled(!parameterError.empty());
        if (Theme::Primary("Сгенерировать", {-1.0f, 0.0f}))
        {
            auto options = generation;
            if (!document->origin && document->graph.GetVertexCount() > 0)
            {
                ConfirmReplacement("Сгенерировать граф вместо текущего?", [this, &job, options] { Generate(job, options); });
            }
            else { Try([&] { Generate(job, options); }); }
        }
        ImGui::EndDisabled();
        ImGui::BeginDisabled(!lastGeneration.has_value());
        if (ImGui::Button("Повторить параметры", {-1.0f, 0.0f}))
        {
            auto options = *lastGeneration;
            auto repeat = [this, &job, options] { generation = options; Generate(job, options); };
            if (!document->origin && document->graph.GetVertexCount() > 0)
            {
                ConfirmReplacement("Заменить вручную изменённый граф?", repeat);
            }
            else { Try(repeat); }
        }
        ImGui::EndDisabled();

        ImGui::BeginDisabled(!network || network->IsExperiment() || document->graph.GetVertexCount() == 0);
        if (ImGui::Button("Использовать как сеть…", {-1.0f, 0.0f}) && network)
        {
            auto build = [this, &job, network] { Transfer(job, *network); };
            auto label = "Создать сеть из " + std::to_string(document->graph.GetVertexCount()) +
                " вершин и " + std::to_string(document->graph.GetEdgeCount()) + " каналов?";
            if (network->Original()->graph.GetVertexCount() != 0) { label += " Текущая сеть будет заменена."; }
            ConfirmReplacement(std::move(label), build);
        }
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader(solar ? "Сеть" : "Создание вручную", solar ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        if (ImGui::Button(solar ? "Новая пустая сеть" : "Новый пустой граф", {-1.0f, 0.0f}))
        {
            auto create = [this] { SetDocument(std::make_shared<GraphDocument>(solar), true); Say("Создан пустой граф"); };
            if (document->graph.GetVertexCount() == 0) { Try(create); }
            else { ConfirmReplacement("Заменить текущие данные пустым графом?", create); }
        }
        if (solar && ImGui::Button("Загрузить пример", {-1.0f, 0.0f}))
        {
            auto demo = [this] { SetDocument(BuildDemoNetwork(), true); station = "Земля"; Say("Пример загружен"); };
            if (document->graph.GetVertexCount() == 0) { Try(demo); }
            else { ConfirmReplacement("Заменить текущую сеть демонстрационным примером?", demo); }
        }
        if (ImGui::Button("Создать по спискам…", {-1.0f, 0.0f}))
        {
            auto open = [this] { bulkError.clear(); openBulk = true; };
            if (document->graph.GetVertexCount() == 0) { open(); }
            else { ConfirmReplacement("Заменить текущий граф данными из списков?", open); }
        }
    }

    if (ImGui::CollapsingHeader(solar ? "Станции и каналы" : "Вершины и рёбра", solar ? ImGuiTreeNodeFlags_DefaultOpen : 0))
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##NewKey", "Идентификатор", &newKey);
        if (solar)
        {
            ImGui::SetNextItemWidth(-74.0f);
            DrawPlanetPicker("Планета", newPlanet);
            ImGui::Checkbox("На орбите", &newOrbit);
        }
        if (ImGui::Button(solar ? "Добавить станцию" : "Добавить вершину", {-1.0f, 0.0f}))
        {
            Try([&]
            {
                StationId key = Trim(newKey);
                if (key.empty()) { throw std::invalid_argument("Введите идентификатор."); }
                Change(job, solar ? "Станция добавлена" : "Вершина добавлена",
                       [key, planetary = solar, planet = static_cast<std::size_t>(newPlanet), orbit = newOrbit](GraphDocument& next)
                {
                    next.graph.AddVertex(key);
                    Placement placement;
                    if (planetary)
                    {
                        std::size_t used = 0;
                        for (const auto& [id, where] : next.locations)
                        {
                            static_cast<void>(id);
                            if (where.planet == planet) { ++used; }
                        }
                        placement = PlaceOnPlanet(planet, orbit, used);
                    }
                    else
                    {
                        std::size_t i = next.graph.GetVertexCount() - 1;
                        if (!next.locations.empty())
                        {
                            auto location = next.locations.rbegin()->second.offset;
                            placement.offset = {location.x + 55.0f, location.y + 36.0f};
                        }
                        else { placement.offset = {static_cast<float>(i), 0.0f}; }
                    }
                    next.locations.emplace(key, placement);
                }, key);
                newKey.clear();
            });
        }

        ImGui::Spacing();
        if (ImGui::TreeNode(solar ? "Добавить канал" : "Добавить ребро"))
        {
            DrawStationPicker("Первый конец", *current, first);
            DrawStationPicker("Второй конец", *current, second);
            ImGui::BeginDisabled(!(first && second));
            if (Theme::Primary("Соединить", {-1.0f, 0.0f}))
            {
                Try([&] { AddConnection(job, *first, *second); });
            }
            ImGui::EndDisabled();
            ImGui::TreePop();
        }
        ImGui::BeginDisabled(current->graph.GetVertexCount() < 2);
        if (ImGui::Button(linkMode ? "Отменить выбор пары" : "Соединить на карте", {-1.0f, 0.0f}))
        {
            linkMode = !linkMode;
            pickStart = false;
            first.reset();
            second.reset();
        }
        ImGui::EndDisabled();
    }
    ImGui::EndDisabled();

    if (ImGui::CollapsingHeader("Анализ", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::BeginDisabled(job.Busy());
        int selected = static_cast<int>(action);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##Operation", &selected, ActionNames, static_cast<int>(std::size(ActionNames)) - (solar ? 0 : 1)))
        {
            action = static_cast<AnalysisAction>(selected);
            analysis.reset();
            group.reset();
            pickStart = false;
        }
        if (action != AnalysisAction::MinimumSpanningTree)
        {
            bool changed = ImGui::RadioButton(Traversals[0].first, &traversalChoice, 0);
            ImGui::SameLine();
            changed = ImGui::RadioButton(Traversals[1].first, &traversalChoice, 1) || changed;
            if (changed)
            {
                analysis.reset();
                group.reset();
            }
        }
        if (action == AnalysisAction::Traversal || action == AnalysisAction::SpanningTree)
        {
            if (DrawStationPicker("Начальная вершина", *current, start)) { analysis.reset(); }
            ImGui::BeginDisabled(current->graph.GetVertexCount() == 0);
            if (ImGui::SmallButton(pickStart ? "Отменить выбор старта" : "Выбрать старт на карте"))
            {
                pickStart = !pickStart;
                linkMode = false;
            }
            ImGui::EndDisabled();
        }
        ImGui::BeginDisabled(action == AnalysisAction::Traversal && !start);
        if (Theme::Primary("Запустить анализ", {-1.0f, 0.0f})) { Try([&] { StartAnalysis(job); }); }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
    }

    if (solar && ImGui::CollapsingHeader("Моделирование отказа", ImGuiTreeNodeFlags_DefaultOpen))
    {
        DrawTrial(job);
    }
    if (!message.empty())
    {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(error ? Theme::Red : Theme::Green));
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::PopStyleColor();
    }
}

void GraphView::DrawTrial(GuiTask& job)
{
    ImGui::BeginDisabled(job.Busy());
    if (!trial)
    {
        ImGui::BeginDisabled(document->graph.GetVertexCount() == 0);
        if (ImGui::Button("Начать эксперимент", {-1.0f, 0.0f})) { Try([&] { BeginTrial(job); }); }
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        return;
    }

    ImGui::TextColored(Theme::Tint(Theme::Blue), "Эксперимент активен");
    if (ImGui::RadioButton("До", !showTrial) && showTrial) { showTrial = false; ResetSelection(true); }
    ImGui::SameLine();
    if (ImGui::RadioButton("После", showTrial) && !showTrial) { showTrial = true; ResetSelection(true); }

    ImGui::BeginDisabled(!showTrial || (offlineStation || offlineChannel) || (!station && !channel));
    if (ImGui::Button("Отключить выбранное", {-1.0f, 0.0f}))
    {
        Try([&] { ApplyFailure(job, station, channel); });
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!offlineStation && !offlineChannel);
    if (ImGui::Button("Сбросить отказ", {-1.0f, 0.0f})) { Try([&] { FinishTrial(job, true); }); }
    ImGui::EndDisabled();
    if (ImGui::Button("Завершить эксперимент", {-1.0f, 0.0f})) { Try([&] { FinishTrial(job, false); }); }
    ImGui::EndDisabled();
}

void GraphView::DrawLists(const GraphDocument& current)
{
    if (ImGui::BeginTabBar("InspectorLists"))
    {
        if (ImGui::BeginTabItem("Сведения"))
        {
            showStations = false;
            showChannels = false;
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(solar ? "Станции" : "Вершины"))
        {
            showStations = true;
            showChannels = false;
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##StationFilter", "Поиск по ключу", &stationFilter);
            if (ImGui::BeginChild("Stations", {0.0f, 152.0f}, ImGuiChildFlags_Borders))
            {
                auto draw = [&](std::size_t i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    const auto& key = current.graph.GetVertexKey(i);
                    if (ImGui::Selectable(key.c_str(), station == key)) { station = key; channel.reset(); }
                    ImGui::PopID();
                };
                if (stationFilter.empty())
                {
                    ImGuiListClipper clipper;
                    clipper.Begin(static_cast<int>(std::min<std::size_t>(current.graph.GetVertexCount(), INT_MAX)));
                    while (clipper.Step())
                    {
                        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) { draw(static_cast<std::size_t>(i)); }
                    }
                }
                else
                {
                    for (std::size_t i = 0; i < current.graph.GetVertexCount(); ++i)
                    {
                        if (current.graph.GetVertexKey(i).find(stationFilter) != std::string::npos) { draw(i); }
                    }
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(solar ? "Каналы" : "Рёбра"))
        {
            showStations = false;
            showChannels = true;
            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::InputTextWithHint("##EdgeFilter", "Поиск по концам", &edgeFilter)) { edgePageNumber = 0; }
            if (edgePageSource != &current || edgePageFilter != edgeFilter)
            {
                edgePageSource = &current;
                edgePageFilter = edgeFilter;
                edgePage.clear();
                std::size_t seen = 0;
                std::size_t offset = edgePageNumber * PageLength;
                const auto& graph = current.graph;
                for (std::size_t u = 0; u < graph.GetVertexCount() && edgePage.size() <= PageLength; ++u)
                {
                    const auto& left = graph.GetVertexKey(u);
                    for (std::size_t i = 0; i < graph.GetNeighborCount(u) && edgePage.size() <= PageLength; ++i)
                    {
                        auto v = graph.GetNeighborIndex(u, i);
                        if (u >= v) { continue; }
                        const auto& right = graph.GetVertexKey(v);
                        if (!edgeFilter.empty() && left.find(edgeFilter) == std::string::npos &&
                            right.find(edgeFilter) == std::string::npos)
                        {
                            continue;
                        }
                        if (seen++ >= offset) { edgePage.push_back(MakeChannelKey(left, right)); }
                    }
                }
                edgePageMore = edgePage.size() > PageLength;
                if (edgePageMore) { edgePage.pop_back(); }
            }
            if (ImGui::BeginChild("Edges", {0.0f, 146.0f}, ImGuiChildFlags_Borders))
            {
                for (std::size_t i = 0; i < edgePage.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    const auto& key = edgePage[i];
                    auto label = key.first + " — " + key.second;
                    if (ImGui::Selectable(label.c_str(), channel == key)) { channel = key; station.reset(); }
                    ImGui::PopID();
                }
                if (edgePage.empty()) { Theme::Note("Каналов на этой странице нет."); }
            }
            ImGui::EndChild();
            ImGui::BeginDisabled(edgePageNumber == 0);
            if (ImGui::SmallButton("Назад")) { --edgePageNumber; edgePageSource = nullptr; }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("%zu", edgePageNumber + 1);
            ImGui::SameLine();
            ImGui::BeginDisabled(!edgePageMore);
            if (ImGui::SmallButton("Далее")) { ++edgePageNumber; edgePageSource = nullptr; }
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void GraphView::DrawNetworkDesign(GuiTask& job, const NetworkDesign& design)
{
    if (design.status == SpanningTreeStatus::Empty) { Theme::Note("Добавьте станции на карту."); return; }
    if (design.status == SpanningTreeStatus::Disconnected) { Theme::Note("Общего остова нет."); return; }

    Theme::Note("Проект связный. Пунктир на карте показывает предложенные каналы.");
    ImGui::Text("Каналов  %zu", design.channels.GetLength());
    ImGui::Text("Суммарная длина  %.2f ед. карты", design.totalLength);
    if (design.channels.GetLength() &&
        ImGui::BeginChild("ProposedChannels", {0, 150}, ImGuiChildFlags_Borders))
    {
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(std::min<std::size_t>(design.channels.GetLength(), INT_MAX)));
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& edge = design.channels.Get(static_cast<std::size_t>(i));
                ImGui::Text("%s — %s  ·  %.2f", edge.first.c_str(), edge.second.c_str(), edge.weight);
            }
        }
    }
    if (design.channels.GetLength()) { ImGui::EndChild(); }

    ImGui::BeginDisabled(job.Busy() || trial || canvas.dragPlacement.has_value() || design.channels.GetLength() == 0);
    if (Theme::Primary("Применить каналы…", {-1.0f, 0.0f}))
    {
        auto source = Current();
        auto result = analysis;
        ConfirmReplacement(source->graph.GetEdgeCount() == 0
                               ? "Добавить предложенные каналы в текущую сеть?"
                               : "Заменить все текущие каналы каналами проекта?",
            [this, source, result]
            {
                if (trial || Current() != source || source->revision != result->revision || analysis != result)
                {
                    throw std::invalid_argument("Сеть изменилась. Постройте проект заново.");
                }
                SetDocument(ApplyNetworkDesign(*source, *result->design), false);
                Say("Каналы минимального остова применены");
            });
    }
    if (trial) { Theme::Note("Применение доступно после завершения эксперимента."); }
    ImGui::EndDisabled();
}

void GraphView::DrawAnalysis(GuiTask& job)
{
    if (!analysis) { return; }
    ImGui::Separator();
    Theme::Heading("Результат", 18.0f);
    auto current = Current();
    auto& result = *analysis;
    if (result.design) { DrawNetworkDesign(job, *result.design); return; }

    std::size_t count = result.keys.GetLength();
    ImGui::TextColored(Theme::Tint(Theme::Blue), "%s · %s", result.algorithm,
                       ActionNames[static_cast<int>(result.action)]);

    if (result.action == AnalysisAction::SpanningTree)
    {
        if (result.status == SpanningTreeStatus::Found)
        {
            const auto total = current->graph.GetEdgeCount();
            ImGui::Text(solar ? "Всего каналов  %zu" : "Всего рёбер  %zu", total);
            ImGui::TextColored(Theme::Tint(Theme::Green), "В остове  %zu", result.tree.size());
            ImGui::Text("Вне остова  %zu", total - result.tree.size());
            if (count == 1) { Theme::Note("Остов состоит из одной вершины."); }
            if (ImGui::Checkbox("Только остов", &result.treeOnly))
            {
                if (result.treeOnly) { result.overlay = true; }
            }
            if (ImGui::TreeNode("Рёбра остова"))
            {
                if (ImGui::BeginChild("TreeEdges", {0, 109}, ImGuiChildFlags_Borders))
                {
                    std::size_t drawn = 0;
                    for (const auto& [edge, step] : result.tree)
                    {
                        static_cast<void>(step);
                        auto label = edge.first + " — " + edge.second;
                        ImGui::PushID(static_cast<int>(drawn++));
                        if (ImGui::Selectable(label.c_str(), channel == edge)) { channel = edge; station.reset(); }
                        ImGui::PopID();
                        if (drawn >= 350) { Theme::Note("Для поиска канала используйте список рёбер."); break; }
                    }
                }
                ImGui::EndChild();
                ImGui::TreePop();
            }
        }
        else if (result.status == SpanningTreeStatus::Disconnected)
        {
            ImGui::TextColored(Theme::Tint(Theme::Red), "Общего остова нет");
            ImGui::Text("Достигнуто %zu из %zu", count, current->graph.GetVertexCount());
        }
        else { Theme::Note(solar ? "Сеть пуста." : "Граф пуст."); }
    }

    if (result.action == AnalysisAction::Components)
    {
        ImGui::Text("Группы связности  %zu", current->groups.size());
        if (current->groups.size() > 1 && ImGui::SmallButton("Показать все группы")) { group.reset(); }
        if (ImGui::BeginChild("Groups", {0, 116}, ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(std::min<std::size_t>(current->groups.size(), INT_MAX)));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    auto index = static_cast<std::size_t>(i);
                    std::string label = "Группа " + std::to_string(index + 1) + "  ·  " + std::to_string(current->groups[index].size());
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(GetComponentColor(index)));
                    ImGui::PushID(i);
                    if (ImGui::Selectable(label.c_str(), group == index))
                    {
                        if (group == index) { group.reset(); }
                        else { group = index; }
                        result.overlay = true;
                    }
                    ImGui::PopID();
                    ImGui::PopStyleColor();
                }
            }
        }
        ImGui::EndChild();
        if (group && *group < current->groups.size())
        {
            Theme::Note("Состав выбранной группы");
            ImGui::PushID("GroupMembers");
            auto previous = station;
            DrawKeys(current->groups[*group], 96.0f, station);
            if (station != previous) { channel.reset(); }
            ImGui::PopID();
        }
    }

    if (count == 0) { return; }
    ImGui::Spacing();
    ImGui::Text("Шаг %zu из %zu", result.shown, count);
    ImGui::TextDisabled("Посещено %zu из %zu", result.shown, current->graph.GetVertexCount());
    ImGui::ProgressBar(static_cast<float>(result.shown) / static_cast<float>(count), {-1, 5.0f}, "");
    bool atEnd = result.shown >= count;
    ImGui::BeginDisabled(atEnd && !result.playing);
    if (ImGui::Button(result.playing ? "Пауза" : "Продолжить"))
    {
        if (!result.playing && result.shown == 0) { result.shown = 1; }
        result.playing = !result.playing;
        result.overlay = true;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(atEnd);
    if (ImGui::Button("Шаг вперёд"))
    {
        ++result.shown;
        result.playing = false;
        result.elapsed = 0;
        result.overlay = true;
    }
    ImGui::EndDisabled();
    if (ImGui::SmallButton("Повторить"))
    {
        result.shown = 1;
        result.elapsed = 0;
        result.playing = count > 1;
        result.overlay = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Сбросить подсветку"))
    {
        result.shown = 0;
        result.elapsed = 0;
        result.playing = false;
        result.overlay = false;
        result.treeOnly = false;
        group.reset();
    }
    ImGui::SetNextItemWidth(-84);
    ImGui::SliderFloat("Скорость", &speed, 0.5f, 16.0f, "%.1f шага/с");

    if (ImGui::TreeNodeEx("Порядок обхода", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (ImGui::BeginChild("Order", {0.0f, 123.0f}, ImGuiChildFlags_Borders))
        {
            ImGuiListClipper clipper;
            clipper.Begin(static_cast<int>(std::min<std::size_t>(count, INT_MAX)));
            while (clipper.Step())
            {
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    auto index = static_cast<std::size_t>(i);
                    const auto& key = *result.keys.Get(index);
                    ImGui::PushID(i);
                    auto color = index + 1 == result.shown ? Theme::Blue : (index < result.shown ? Theme::Text : Theme::Muted);
                    ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(color));
                    auto label = std::to_string(index + 1) + "  " + key;
                    if (ImGui::Selectable(label.c_str(), station == key)) { station = key; channel.reset(); }
                    ImGui::PopStyleColor();
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        ImGui::TreePop();
    }
}

void GraphView::DrawInspector(GuiTask& job)
{
    auto current = Current();
    Theme::Heading("Инспектор", 19.0f);
    DrawLists(*current);
    if (station && current->graph.ContainsVertex(*station))
    {
        auto key = *station;
        Theme::Overline(solar ? "ВЫБРАННАЯ СТАНЦИЯ" : "ВЫБРАННАЯ ВЕРШИНА");
        Theme::Heading(key.c_str(), 21.0f);

        if (solar)
        {
            const auto& place = current->locations.at(key);
            int planet = static_cast<int>(place.planet);
            bool onOrbit = place.orbit;
            ImGui::BeginDisabled(job.Busy() || (trial && !showTrial));
            ImGui::SetNextItemWidth(-74);
            bool moved = DrawPlanetPicker("Планета##Location", planet);
            moved = ImGui::Checkbox("На орбите##Location", &onOrbit) || moved;
            if (moved)
            {
                Try([&]
                {
                    auto placement = PlaceOnPlanet(static_cast<std::size_t>(planet), onOrbit,
                                                   *current->graph.FindVertexIndex(key));
                    MoveStation(job, key, placement);
                });
            }
            ImGui::EndDisabled();
        }

        auto index = *current->graph.FindVertexIndex(key);
        auto degree = current->graph.GetNeighborCount(index);
        ImGui::Text("Соседи  %zu", degree);
        if (!current->groups.empty())
        {
            auto number = current->groupOf.at(key);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GetComponentColor(number)), "Группа  %zu", number + 1);
        }
        if (degree && ImGui::TreeNode("Соседние вершины"))
        {
            float neighborsHeight = std::min(115.0f, static_cast<float>(std::min<std::size_t>(degree, 12)) *
                                                          ImGui::GetTextLineHeightWithSpacing() + 16.0f);
            if (ImGui::BeginChild("Neighbors", {0, neighborsHeight}, ImGuiChildFlags_Borders))
            {
                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(std::min<std::size_t>(degree, INT_MAX)));
                while (clipper.Step())
                {
                    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                    {
                        const auto& neighbor = current->graph.GetVertexKey(current->graph.GetNeighborIndex(index, static_cast<std::size_t>(i)));
                        ImGui::PushID(i);
                        if (ImGui::Selectable(neighbor.c_str())) { station = neighbor; channel.reset(); }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
        ImGui::BeginDisabled(job.Busy());
        if (action != AnalysisAction::MinimumSpanningTree && ImGui::Button("Сделать начальной", {-1.0f, 0.0f}))
        {
            start = key;
            if (analysis && analysis->action != AnalysisAction::Components) { analysis.reset(); }
        }
        if (!trial && ImGui::SmallButton(solar ? "Удалить станцию" : "Удалить вершину"))
        {
            Try([&]
            {
                Change(job, solar ? "Станция и её каналы удалены" : "Вершина и её рёбра удалены",
                       [key](GraphDocument& next) { next.graph.RemoveVertex(key); next.locations.erase(key); });
            });
        }
        ImGui::EndDisabled();
    }
    else if (channel && current->graph.ContainsEdge(channel->first, channel->second))
    {
        auto key = *channel;
        Theme::Overline(solar ? "ВЫБРАННЫЙ КАНАЛ" : "ВЫБРАННОЕ РЕБРО");
        ImGui::TextWrapped("%s — %s", key.first.c_str(), key.second.c_str());
        if (solar) { ImGui::Text("Длина  %.2f ед. карты", current->graph.GetEdgeWeight(key.first, key.second)); }
        if (analysis && analysis->status == SpanningTreeStatus::Found)
        {
            if (analysis->tree.count(key)) { ImGui::TextColored(Theme::Tint(Theme::Green), "Входит в остов"); }
            else { Theme::Note("Вне остова"); }
        }
        ImGui::BeginDisabled(job.Busy());
        if (!trial && ImGui::SmallButton(solar ? "Удалить канал" : "Удалить ребро"))
        {
            Try([&]
            {
                Change(job, solar ? "Канал удалён" : "Ребро удалено",
                       [key](GraphDocument& next) { next.graph.RemoveEdge(key.first, key.second); });
            });
        }
        else if (trial && showTrial && !offlineStation && !offlineChannel && ImGui::SmallButton("Отключить канал"))
        {
            Try([&] { ApplyFailure(job, std::nullopt, key); });
        }
        ImGui::EndDisabled();
    }
    else if (!showChannels && !showStations)
    {
        Theme::Note(solar ? "Выберите на карте станцию или канал." : "Выберите на схеме вершину или ребро.");
    }

    if (trial)
    {
        ImGui::Spacing();
        Theme::Heading("До и после отказа", 17.0f);
        if (ImGui::BeginTable("TrialComparison", 3, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
        {
            ImGui::TableSetupColumn("Показатель", ImGuiTableColumnFlags_WidthStretch, 1.3f);
            ImGui::TableSetupColumn("До");
            ImGui::TableSetupColumn("После");
            ImGui::TableHeadersRow();
            auto row = [](const char* label, const std::string& before, const std::string& after)
            {
                ImGui::TableNextRow();
                ColumnText(label);
                ColumnText(before.c_str());
                ColumnText(after.c_str());
            };
            row("Станции", std::to_string(document->graph.GetVertexCount()), std::to_string(trial->graph.GetVertexCount()));
            row("Каналы", std::to_string(document->graph.GetEdgeCount()), std::to_string(trial->graph.GetEdgeCount()));
            row("Группы", std::to_string(document->groups.size()), std::to_string(trial->groups.size()));
            row("Сеть", NetworkStatusLabel(document->Status()), NetworkStatusLabel(trial->Status()));
            ImGui::EndTable();
        }
        if (offlineStation) { ImGui::TextColored(Theme::Tint(Theme::Red), "Отключена: %s", offlineStation->c_str()); }
        if (offlineChannel)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(Theme::Red));
            ImGui::TextWrapped("Отключён канал %s — %s", offlineChannel->first.c_str(), offlineChannel->second.c_str());
            ImGui::PopStyleColor();
        }
        if ((offlineStation || offlineChannel) && splits.empty()) { Theme::Note("У оставшихся станций группы не разделились."); }
        if (!splits.empty() && ImGui::TreeNodeEx("Станции, потерявшие связь", ImGuiTreeNodeFlags_DefaultOpen))
        {
            for (const auto& split : splits)
            {
                ImGui::Text("Исходная группа %zu", split.before + 1);
                for (std::size_t i = 0; i < split.parts.size(); ++i)
                {
                    const auto& part = split.parts[i];
                    std::string label = std::to_string(i + 1) + ". ";
                    for (std::size_t j = 0; j < std::min<std::size_t>(part.size(), 8); ++j)
                    {
                        if (j) { label += ", "; }
                        label += part[j];
                    }
                    if (part.size() > 8) { label += " … (" + std::to_string(part.size()) + " станций)"; }
                    ImGui::TextWrapped("%s", label.c_str());
                }
            }
            ImGui::TreePop();
        }
    }
    DrawAnalysis(job);
}

void GraphView::DrawDialogs(GuiTask& job)
{
    if (openConfirmation)
    {
        ImGui::OpenPopup("Подтверждение");
        openConfirmation = false;
    }
    ImGui::SetNextWindowSize({400.0f, 0.0f}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Подтверждение", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextWrapped("%s", confirmationText.c_str());
        ImGui::Spacing();
        ImGui::BeginDisabled(job.Busy());
        if (Theme::Primary("Продолжить"))
        {
            if (confirmation) { Try(confirmation); }
            confirmation = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Отмена")) { confirmation = {}; ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }

    if (openBulk)
    {
        ImGui::OpenPopup("Создание по спискам");
        openBulk = false;
    }
    ImGui::SetNextWindowSize({490.0f, 0.0f}, ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Создание по спискам", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        Theme::Note("Один идентификатор на строку. Рёбра вводите парами через пробел, для ключей с пробелами используйте табуляцию.");
        ImGui::TextUnformatted("Идентификаторы");
        ImGui::InputTextMultiline("##Vertices", &bulkVertices, {-1.0f, 115.0f}, ImGuiInputTextFlags_AllowTabInput);
        ImGui::TextUnformatted("Рёбра");
        ImGui::InputTextMultiline("##Edges", &bulkEdges, {-1.0f, 115.0f}, ImGuiInputTextFlags_AllowTabInput);
        if (!bulkError.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(Theme::Red));
            ImGui::TextWrapped("%s", bulkError.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::BeginDisabled(job.Busy());
        if (Theme::Primary("Построить"))
        {
            try
            {
                auto keys = ParseKeys(bulkVertices);
                auto edges = ParseChannels(bulkEdges);
                SetDocument(BuildDocument(keys, edges, solar), true);
                bulkError.clear();
                Say("Граф построен по спискам");
                ImGui::CloseCurrentPopup();
            }
            catch (const std::exception& exception) { bulkError = UserError(exception); }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Отмена")) { ImGui::CloseCurrentPopup(); }
        ImGui::EndPopup();
    }
}

void GraphView::HandleCanvas(GuiTask& job, CanvasEvent event)
{
    if (event.move)
    {
        Try([&] { MoveStation(job, event.move->first, event.move->second); });
    }
    if (event.station)
    {
        station = event.station;
        channel.reset();
        if (pickStart && !job.Busy())
        {
            start = station;
            pickStart = false;
            analysis.reset();
            Say("Начальная вершина выбрана");
        }
        else if (linkMode && !job.Busy() && !trial)
        {
            if (!first) { first = station; }
            else
            {
                Try([&] { AddConnection(job, *first, *station); });
            }
        }
    }
    else if (event.channel)
    {
        channel = std::move(event.channel);
        station.reset();
    }
    else if (event.background)
    {
        station.reset();
        channel.reset();
    }
}

void GraphView::Draw(GuiTask& job, GraphView* network)
{
    ImGui::PushID(solar ? "NetworkPage" : "GeneratorPage");
    DrawSummary();
    if (error && !message.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(Theme::Red));
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::PopStyleColor();
    }
    auto total = ImGui::GetContentRegionAvail();
    bool wide = total.x >= 1165.0f;
    float left = wide ? 264.0f : 245.0f;
    float right = 298.0f;
    float gap = ImGui::GetStyle().ItemSpacing.x;
    if (!wide)
    {
        if (ImGui::SmallButton(showInspector ? "Открыть управление" : "Открыть сведения")) { showInspector = !showInspector; }
        total = ImGui::GetContentRegionAvail();
    }

    auto flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (ImGui::BeginChild("Tools", {(!wide && showInspector) ? right : left, total.y}, flags))
    {
        if (!wide && showInspector) { DrawInspector(job); }
        else { DrawEditor(job, network); }
    }
    ImGui::EndChild();

    if ((linkMode || pickStart) && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        pickStart = false;
        linkMode = false;
        first.reset();
        second.reset();
    }

    if (analysis && analysis->playing)
    {
        auto& result = *analysis;
        result.elapsed += ImGui::GetIO().DeltaTime * speed;
        if (result.elapsed >= 1.0f)
        {
            auto steps = static_cast<std::size_t>(result.elapsed);
            result.elapsed -= static_cast<float>(steps);
            result.shown += std::min(steps, result.keys.GetLength() - result.shown);
            if (result.shown == result.keys.GetLength()) { result.playing = false; }
        }
    }

    float width = total.x - ((!wide && showInspector) ? right : left) - gap;
    if (wide) { width -= right + gap; }
    ImGui::SameLine();
    if (ImGui::BeginChild("Scene", {width, total.y}, flags,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
    {
        CanvasHighlights highlight;
        highlight.station = station;
        highlight.channel = channel;
        if (action != AnalysisAction::MinimumSpanningTree) { highlight.start = start; }
        highlight.linkStart = linkMode ? first : std::nullopt;
        highlight.group = group;
        highlight.linkMode = linkMode;
        highlight.chooseStart = pickStart;
        if (analysis && analysis->overlay)
        {
            highlight.steps = &analysis->steps;
            highlight.shown = analysis->shown;
            highlight.components = analysis->action == AnalysisAction::Components;
            highlight.treeOnly = analysis->treeOnly;
            if (analysis->status == SpanningTreeStatus::Found) { highlight.treeEdges = &analysis->tree; }
            if (analysis->design) { highlight.design = &*analysis->design; }
        }
        if (trial && showTrial)
        {
            highlight.beforeFailure = document.get();
            highlight.offlineStation = offlineStation;
            highlight.offlineChannel = offlineChannel;
        }

        auto active = Current();
        auto event = DrawGraphCanvas(*active, canvas, solar, highlight,
                                     !job.Busy() && (!trial || showTrial));
        HandleCanvas(job, std::move(event));
    }
    ImGui::EndChild();

    if (wide)
    {
        ImGui::SameLine();
        if (ImGui::BeginChild("Details", {right, total.y}, flags)) { DrawInspector(job); }
        ImGui::EndChild();
    }

    DrawDialogs(job);
    ImGui::PopID();
}
