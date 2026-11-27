#include "BenchmarkView.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <tuple>

#include "GuiStyle.hpp"

namespace
{
    constexpr const char* TopologyText[]{"Цепочка", "Цикл", "Полный граф", "Звезда", "Двоичное дерево", "Случайный"};
    constexpr const char* OrderText[]{"Текущий", "Прямой", "Обратный", "Перемешанный"};
    constexpr const char* AxisText[]{"Вершины V", "Рёбра E", "V + E"};

    const char* TopologyLabel(const BenchmarkRecord& record)
    {
        return record.input.generation ? TopologyText[static_cast<int>(record.input.generation->topology)] : "Ручной";
    }

    const char* TaskLabel(BenchmarkTask task)
    {
        return task == BenchmarkTask::SpanningTree ? "Поиск остова" : "Компоненты связности";
    }

    double Abscissa(const BenchmarkRecord& record, int axis)
    {
        if (axis == 0) { return static_cast<double>(record.vertices); }
        if (axis == 1) { return static_cast<double>(record.edges); }
        return static_cast<double>(record.vertices) + static_cast<double>(record.edges);
    }

    std::string Number(double value)
    {
        std::ostringstream text;
        text << std::setprecision(4) << value;
        return text.str();
    }

    void SizeInput(const char* label, std::size_t& value)
    {
        ImU64 current = value;
        ImGui::SetNextItemWidth(-100.0f);
        if (ImGui::InputScalar(label, ImGuiDataType_U64, &current)) { value = static_cast<std::size_t>(current); }
    }

    void ChooseTopology(Topology& topology)
    {
        int picked = static_cast<int>(topology);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##Topology", &picked, TopologyText, static_cast<int>(std::size(TopologyText))))
        {
            topology = static_cast<Topology>(picked);
        }
    }

    void NumericCell(const char* format, std::size_t value)
    {
        ImGui::TableNextColumn();
        ImGui::Text(format, value);
    }

    void TextCell(const char* text)
    {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(text);
    }

    void Legend(ImDrawList* draw, ImVec2 origin)
    {
        draw->AddCircleFilled({origin.x + 5, origin.y + 8}, 4.0f, Theme::Color(Theme::Blue), 14);
        draw->AddText({origin.x + 17, origin.y}, Theme::Color(Theme::Text), "BFS");
        draw->AddRectFilled({origin.x + 68, origin.y + 4}, {origin.x + 76, origin.y + 12}, Theme::Color(Theme::Purple));
        draw->AddText({origin.x + 84, origin.y}, Theme::Color(Theme::Text), "DFS");
    }
}

void BenchmarkView::SetMessage(std::string text, bool failed)
{
    message = std::move(text);
    error = failed;
}

std::vector<EdgeOrder> BenchmarkView::ActiveOrders() const
{
    std::vector<EdgeOrder> values;
    for (std::size_t i = currentGraph ? 0 : 1; i < orders.size(); ++i)
    {
        if (orders[i]) { values.push_back(static_cast<EdgeOrder>(i)); }
    }
    return values;
}

BenchmarkView::Point BenchmarkView::Summarize(BenchmarkReport report, std::size_t series, const std::string& source)
{
    Point point{std::move(report), {}, {}};
    point.bfs.minimum = point.dfs.minimum = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < point.report.records.GetLength(); ++i)
    {
        auto& record = point.report.records.Get(i);
        record.series = series;
        record.source = source;
        auto& values = record.algorithm == "BFS" ? point.bfs : point.dfs;
        values.minimum = std::min(values.minimum, record.milliseconds);
        values.maximum = std::max(values.maximum, record.milliseconds);
    }
    return point;
}

void BenchmarkView::Start(GuiTask& job, const GraphView& network, const GraphView& graph)
{
    auto selectedOrders = ActiveOrders();
    if (selectedOrders.empty()) { throw std::invalid_argument("Отметьте хотя бы один порядок рёбер."); }
    if (settings.repeats == 0) { throw std::invalid_argument("Укажите хотя бы один повтор."); }

    const GraphView* owner = nullptr;
    std::shared_ptr<const GraphDocument> input;
    std::string label = "Серия генераций";
    if (currentGraph)
    {
        owner = currentSource == 1 ? &graph : &network;
        input = currentSource == 2 ? owner->Original() : owner->Current();
        label = currentSource == 1 ? "Обычный граф" : (currentSource == 2 ? "Исходная сеть" : "Сеть на карте");
    }
    auto seed = input && start ? input->graph.FindVertexIndex(*start) : std::nullopt;
    if (input && settings.task == BenchmarkTask::SpanningTree && input->graph.GetVertexCount() > 0 && !seed)
    {
        throw std::invalid_argument("Выберите начальную вершину.");
    }
    auto cases = CheckedExperimentProduct(input ? 1 : ValidateExperiment(settings), selectedOrders.size());
    auto total = CheckedExperimentProduct(cases, CheckedExperimentProduct(2, settings.repeats));
    auto experiment = std::make_shared<Series>();
    experiment->source = label;
    experiment->task = settings.task;
    experiment->id = history.empty() ? 1 : history.back()->id + 1;

    job.Start(owner, label, total, cases,
        [this, input = std::move(input), experiment, config = settings, selectedOrders = std::move(selectedOrders), seed](GuiTask& task)
    {
        BenchmarkControl control{
            [&task] { return task.Stopping(); },
            [&task] { task.stage = GuiTask::Stage::Measuring; ++task.completed; }
        };
        auto add = [&](BenchmarkReport report)
        {
            if (!report.stopped) { ++task.completedCases; }
            if (report.records.GetLength() != 0)
            {
                experiment->points.push_back(Summarize(std::move(report), experiment->id, experiment->source));
            }
        };

        if (input)
        {
            const auto& graph = input->graph;
            ArraySequence<StationId> keys;
            ArraySequence<InterplanetaryNetwork::WeightedEdge> edges;
            bool needCopies = std::any_of(selectedOrders.begin(), selectedOrders.end(),
                                          [](auto value) { return value != EdgeOrder::Current; });
            if (needCopies && !task.Stopping())
            {
                keys.Reserve(graph.GetVertexCount());
                edges.Reserve(graph.GetEdgeCount());
                for (std::size_t i = 0; i < graph.GetVertexCount(); ++i) { keys.Append(graph.GetVertexKey(i)); }
                graph.ForEachWeightedEdge([&](std::size_t u, std::size_t v, double weight)
                {
                    auto [first, second] = MakeChannelKey(graph.GetVertexKey(u), graph.GetVertexKey(v));
                    edges.Append({std::move(first), std::move(second), weight});
                });
            }
            for (auto order : selectedOrders)
            {
                if (task.Stopping()) { break; }
                task.stage = GuiTask::Stage::Preparing;
                BenchmarkInput parameters{input->origin, order, config.shuffleSeed};
                if (order == EdgeOrder::Current) { add(CompareAlgorithms(graph, config.task, seed, config.repeats, parameters, control)); }
                else
                {
                    auto arranged = benchmark_detail::ArrangeEdges(edges, order, config.shuffleSeed,
                        [](const auto& left, const auto& right)
                        {
                            return std::tie(left.first, left.second) < std::tie(right.first, right.second);
                        });
                    InterplanetaryNetwork reordered(keys, arranged, std::make_unique<TreeDictionary<StationId, std::size_t>>(), CreateArraySequence, CreateArraySequence);
                    add(CompareAlgorithms(reordered, config.task, seed, config.repeats, parameters, control));
                }
            }
        }
        else
        {
            auto sizes = ExperimentSizes(config);
            for (auto topology : ExperimentTopologies(config))
            {
                for (std::size_t i = 0; i < sizes && !task.Stopping(); ++i)
                {
                    auto options = OptionsAt(config, topology, i);
                    task.stage = GuiTask::Stage::Preparing;
                    auto data = GenerateGraphData(options);
                    for (auto order : selectedOrders)
                    {
                        if (task.Stopping()) { break; }
                        task.stage = GuiTask::Stage::Preparing;
                        add(BenchmarkGeneratedGraph(data, options, config.task, order, config.shuffleSeed, config.repeats, control));
                    }
                }
            }
        }

        experiment->stopped = task.completedCases.load() < task.Cases();
        return [this, experiment, axis = config.sweep == Sweep::Edges && !input ? 1 : 0]
        {
            running = false;
            if (experiment->points.empty())
            {
                SetMessage("Серия остановлена до завершения первой пары.");
                return;
            }
            history.push_back(experiment);
            displayed = history.size() - 1;
            selected = 0;
            xAxis = axis;
            topologyFilter = orderFilter = -1;
            SetMessage(experiment->stopped ? "Серия остановлена. Завершённые пары доступны." : "Измерения завершены.");
        };
    });
    running = true;
    message.clear();
}

void BenchmarkView::Save(GuiTask& job)
{
    auto path = ChooseBenchmarkCsv();
    if (!path) { return; }
    auto data = history.at(displayed);
    job.Start(nullptr, "Экспорт CSV", 0, 0, [this, data, path = *path](GuiTask& task)
    {
        task.stage = GuiTask::Stage::Saving;
        ArraySequence<BenchmarkRecord> records;
        for (const auto& point : data->points)
        {
            for (std::size_t i = 0; i < point.report.records.GetLength(); ++i) { records.Append(point.report.records.Get(i)); }
        }
        SaveBenchmarkCsvTo(records, path, true);
        return [this, path] { SetMessage("CSV сохранён: " + path.string()); };
    });
}

void BenchmarkView::DrawForm(GuiTask& job, const GraphView& network, const GraphView& graph)
{
    Theme::Heading("Эксперимент", 20.0f);
    ImGui::BeginDisabled(job.Busy());
    int mode = currentGraph ? 0 : 1;
    ImGui::SetNextItemWidth(-1);
    const char* modes[]{"Текущий граф", "Серия генераций"};
    if (ImGui::Combo("##Mode", &mode, modes, 2)) { currentGraph = mode == 0; }
    ImGui::TextUnformatted("Задача");
    int task = settings.task == BenchmarkTask::SpanningTree ? 0 : 1;
    const char* tasks[]{"Поиск остова", "Компоненты связности"};
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##Task", &task, tasks, 2)) { settings.task = static_cast<BenchmarkTask>(task); }

    if (currentGraph)
    {
        ImGui::TextUnformatted("Источник");
        ImGui::SetNextItemWidth(-1);
        const char* sources[]{"Сеть на карте", "Обычный граф", "Исходная сеть"};
        if (!network.IsExperiment() && currentSource == 2) { currentSource = 0; }
        ImGui::Combo("##Source", &currentSource, sources, network.IsExperiment() ? 3 : 2);
        auto input = currentSource == 1 ? graph.Current() : (currentSource == 2 ? network.Original() : network.Current());
        ImGui::TextDisabled("%zu вершин · %zu рёбер", input->graph.GetVertexCount(), input->graph.GetEdgeCount());
        if (settings.task == BenchmarkTask::SpanningTree)
        {
            if (input->graph.GetVertexCount() > 0 && (!start || !input->graph.ContainsVertex(*start)))
            {
                start = input->graph.GetVertexKey(0);
            }
            DrawStationPicker("Стартовая вершина", *input, start);
        }
    }
    else
    {
        ImGui::TextUnformatted("Серия");
        const char* sweeps[]{"По вершинам V", "По рёбрам E", "Сравнение топологий"};
        int sweep = static_cast<int>(settings.sweep);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##Sweep", &sweep, sweeps, 3)) { settings.sweep = static_cast<Sweep>(sweep); }
        if (settings.sweep == Sweep::Edges)
        {
            Theme::Note("Случайный граф при фиксированном V");
            SizeInput("Вершины", settings.vertices);
            SizeInput("Рёбра от", settings.edgeFrom);
            SizeInput("Рёбра до", settings.edgeTo);
            SizeInput("Шаг E", settings.edgeStep);
        }
        else
        {
            ImGui::TextUnformatted("Топология");
            if (settings.sweep == Sweep::Topologies)
            {
                for (std::size_t i = 0; i < settings.topologies.size(); ++i)
                {
                    ImGui::Checkbox(TopologyText[i], &settings.topologies[i]);
                }
            }
            else { ChooseTopology(settings.topology); }
            SizeInput("Вершины от", settings.from);
            SizeInput("Вершины до", settings.to);
            SizeInput("Шаг V", settings.step);
            if ((settings.sweep == Sweep::Vertices && settings.topology == Topology::Random) ||
                (settings.sweep == Sweep::Topologies && settings.topologies[static_cast<int>(Topology::Random)]))
            {
                SizeInput("Рёбра для Random", settings.edges);
            }
        }
        ImGui::SetNextItemWidth(-100.0f);
        ImGui::InputScalar("Seed", ImGuiDataType_U32, &settings.seed);
        if (settings.task == BenchmarkTask::SpanningTree) { Theme::Note("Старт серии — вершина 1."); }
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Порядок входных рёбер");
    for (std::size_t i = currentGraph ? 0 : 1; i < orders.size(); ++i)
    {
        ImGui::Checkbox(OrderText[i], &orders[i]);
    }
    if (orders[static_cast<int>(EdgeOrder::Shuffled)])
    {
        ImGui::SetNextItemWidth(-113.0f);
        ImGui::InputScalar("Seed порядка", ImGuiDataType_U32, &settings.shuffleSeed);
    }
    SizeInput("Повторы", settings.repeats);

    std::string inputError;
    try
    {
        auto modesCount = ActiveOrders().size();
        auto points = CheckedExperimentProduct(currentGraph ? 1 : ValidateExperiment(settings), modesCount);
        if (settings.repeats && points)
        {
            auto operations = CheckedExperimentProduct(points, CheckedExperimentProduct(2, settings.repeats));
            ImGui::TextDisabled("%zu точек · %zu измерений", points, operations);
        }
    }
    catch (const std::exception& exception) { inputError = exception.what(); }
    if (inputError.find("cycle needs") != std::string::npos) { inputError = "Для цикла нужно минимум три вершины."; }
    if (inputError.find("topology needs") != std::string::npos) { inputError = "У выбранной топологии должно быть хотя бы по одной вершине."; }
    if (inputError.find("too many edges") != std::string::npos) { inputError = "Для одной из точек указано слишком много рёбер."; }
    if (!inputError.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(Theme::Red));
        ImGui::TextWrapped("%s", inputError.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::BeginDisabled(!inputError.empty());
    if (Theme::Primary("Запустить замеры", {-1.0f, 0.0f}))
    {
        try { Start(job, network, graph); }
        catch (const std::exception& exception) { SetMessage(exception.what(), true); }
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (job.Busy() && running && job.Total() != 0)
    {
        ImGui::Spacing();
        const char* label = job.Stopping() ? "Остановка после текущей операции" :
            (job.stage.load() == GuiTask::Stage::Preparing ? "Подготовка графа" : "Измерение BFS и DFS");
        Theme::Note(label);
        ImGui::ProgressBar(static_cast<float>(job.completed.load()) / static_cast<float>(std::max<std::size_t>(1, job.Total())),
                           {-1, 9}, "");
        ImGui::TextDisabled("%zu / %zu измерений", job.completed.load(), job.Total());
        ImGui::TextDisabled("%zu / %zu завершённых точек", job.completedCases.load(), job.Cases());
        ImGui::BeginDisabled(job.Stopping());
        if (ImGui::Button("Остановить", {-1.0f, 0.0f})) { job.RequestStop(); }
        ImGui::EndDisabled();
    }
    if (!message.empty())
    {
        ImGui::PushStyleColor(ImGuiCol_Text, Theme::Tint(error ? Theme::Red : Theme::Green));
        ImGui::TextWrapped("%s", message.c_str());
        ImGui::PopStyleColor();
    }
}

void BenchmarkView::DrawLineChart(const Series& data, const std::vector<std::size_t>& points, ImVec2 size)
{
    auto top = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("LineChart", size);
    auto* draw = ImGui::GetWindowDrawList();
    ImVec2 bottom{top.x + size.x, top.y + size.y};
    draw->AddRectFilled(top, bottom, Theme::Color(Theme::Inset), 8.0f);
    draw->AddRect(top, bottom, Theme::Color(Theme::Border), 8.0f);
    if (points.empty() || size.x < 190) { return; }
    Legend(draw, {top.x + 16, top.y + 13});

    ImVec2 origin{top.x + 58.0f, top.y + 51.0f};
    ImVec2 end{bottom.x - 25.0f, bottom.y - 47.0f};
    double minimumX = std::numeric_limits<double>::infinity(), maximumX = 0.0, maximumY = 0.0;
    for (auto index : points)
    {
        const auto& result = data.points[index].report;
        double x = Abscissa(result.records.Get(0), xAxis);
        minimumX = std::min(x, minimumX);
        maximumX = std::max(x, maximumX);
        maximumY = std::max({maximumY, result.bfsMedian, result.dfsMedian});
    }
    maximumY = std::max(maximumY * 1.14, 1e-8);
    bool sameX = minimumX == maximumX;
    auto project = [&](double x, double y)
    {
        return ImVec2{origin.x + static_cast<float>(sameX ? 0.5 : (x - minimumX) / (maximumX - minimumX)) * (end.x - origin.x),
                      end.y - static_cast<float>(y / maximumY) * (end.y - origin.y)};
    };

    draw->AddText({origin.x - 37, origin.y - 22}, Theme::Color(Theme::Muted), "мс");
    for (int tick = 0; tick <= 4; ++tick)
    {
        double yValue = maximumY * tick / 4.0;
        float y = project(minimumX, yValue).y;
        draw->AddLine({origin.x, y}, {end.x, y}, Theme::Color(Theme::Border, 178));
        auto label = Number(yValue);
        draw->AddText({origin.x - ImGui::CalcTextSize(label.c_str()).x - 6, y - 8}, Theme::Color(Theme::Muted), label.c_str());
    }
    if (sameX)
    {
        auto label = Number(minimumX);
        draw->AddText({(origin.x + end.x - ImGui::CalcTextSize(label.c_str()).x) * 0.5f, end.y + 7},
                      Theme::Color(Theme::Muted), label.c_str());
    }
    else
    {
        int ticks = size.x > 500 ? 4 : 3;
        for (int tick = 0; tick <= ticks; ++tick)
        {
            double xValue = minimumX + (maximumX - minimumX) * tick / ticks;
            auto x = project(xValue, 0).x;
            draw->AddLine({x, origin.y}, {x, end.y}, Theme::Color(Theme::Border, 139));
            auto label = Number(xValue);
            draw->AddText({x - ImGui::CalcTextSize(label.c_str()).x * 0.5f, end.y + 7}, Theme::Color(Theme::Muted), label.c_str());
        }
    }
    auto axis = AxisText[xAxis];
    draw->AddText({(origin.x + end.x - ImGui::CalcTextSize(axis).x) * 0.5f, bottom.y - 20}, Theme::Color(Theme::Muted), axis);

    std::map<std::pair<std::string, EdgeOrder>, std::vector<std::size_t>> lines;
    for (auto index : points)
    {
        const auto& record = data.points[index].report.records.Get(0);
        lines[{TopologyLabel(record), record.input.order}].push_back(index);
    }
    ImVec2 mouse = ImGui::GetIO().MousePos;
    float nearest = 17.0f * 17.0f;
    std::optional<std::size_t> hovered;
    for (auto& [identity, line] : lines)
    {
        static_cast<void>(identity);
        std::stable_sort(line.begin(), line.end(), [&](auto left, auto right)
        {
            return Abscissa(data.points[left].report.records.Get(0), xAxis) <
                   Abscissa(data.points[right].report.records.Get(0), xAxis);
        });
        ImVec2 previousBfs{}, previousDfs{};
        bool first = true;
        for (auto index : line)
        {
            const auto& point = data.points[index].report;
            double x = Abscissa(point.records.Get(0), xAxis);
            auto bfs = project(x, point.bfsMedian);
            auto dfs = project(x, point.dfsMedian);
            auto blue = Theme::Color(Theme::Blue, lines.size() == 1 ? 231 : 151);
            auto purple = Theme::Color(Theme::Purple, lines.size() == 1 ? 231 : 151);
            if (!first)
            {
                draw->AddLine(previousBfs, bfs, blue, 1.9f);
                draw->AddLine(previousDfs, dfs, purple, 1.9f);
            }
            first = false;
            previousBfs = bfs;
            previousDfs = dfs;
            draw->AddCircleFilled(bfs, selected == index ? 5.0f : 3.7f, Theme::Color(Theme::Blue), 18);
            float radius = selected == index ? 4.8f : 3.45f;
            draw->AddRectFilled({dfs.x - radius, dfs.y - radius}, {dfs.x + radius, dfs.y + radius}, Theme::Color(Theme::Purple), 1.1f);

            if (ImGui::IsItemHovered())
            {
                auto dist = [&](ImVec2 pointOnChart)
                {
                    return (pointOnChart.x - mouse.x) * (pointOnChart.x - mouse.x) +
                           (pointOnChart.y - mouse.y) * (pointOnChart.y - mouse.y);
                };
                float distance = std::min(dist(bfs), dist(dfs));
                if (distance < nearest) { nearest = distance; hovered = index; }
            }
        }
    }

    if (hovered)
    {
        const auto& result = data.points[*hovered].report;
        const auto& record = result.records.Get(0);
        ImGui::BeginTooltip();
        ImGui::Text("%s · %s", TopologyLabel(record), OrderText[static_cast<int>(record.input.order)]);
        ImGui::Text("V = %zu   E = %zu", record.vertices, record.edges);
        ImGui::Text("BFS %.9f мс   DFS %.9f мс", result.bfsMedian, result.dfsMedian);
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) { selected = *hovered; }
    }
}

void BenchmarkView::DrawBars(const Point& point, ImVec2 size)
{
    auto top = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("BarChart", size);
    auto* draw = ImGui::GetWindowDrawList();
    ImVec2 bottom{top.x + size.x, top.y + size.y};
    draw->AddRectFilled(top, bottom, Theme::Color(Theme::Inset), 8.0f);
    draw->AddRect(top, bottom, Theme::Color(Theme::Border), 8.0f);
    const auto& report = point.report;
    const auto& record = report.records.Get(0);
    std::string subtitle = "V = " + std::to_string(record.vertices) + "  ·  E = " + std::to_string(record.edges);
    draw->AddText({top.x + 12, top.y + 13}, Theme::Color(Theme::Muted), subtitle.c_str());

    float y0 = bottom.y - 35;
    float base = top.y + 58;
    double maximum = std::max({report.bfsMedian * 1.15, report.dfsMedian * 1.15, 1e-8});
    draw->AddLine({top.x + 15, y0}, {bottom.x - 15, y0}, Theme::Color(Theme::Border));
    draw->AddLine({top.x + 15, base}, {bottom.x - 15, base}, Theme::Color(Theme::Border, 130));
    auto bar = [&](const char* name, double value, float x, std::uint32_t color)
    {
        float height = static_cast<float>(value / maximum) * (y0 - base);
        float width = std::min(45.0f, size.x * 0.15f);
        draw->AddRectFilled({x - width, y0 - std::max(height, 2.0f)}, {x + width, y0}, Theme::Color(color, 204), 5.0f);
        draw->AddText({x - 15.0f, y0 + 9.0f}, Theme::Color(Theme::Text), name);
        auto label = Number(value) + " мс";
        draw->AddText({x - ImGui::CalcTextSize(label.c_str()).x * 0.5f, y0 - height - 22}, Theme::Color(color), label.c_str());
    };
    bar("BFS", report.bfsMedian, top.x + size.x * 0.28f, Theme::Blue);
    bar("DFS", report.dfsMedian, top.x + size.x * 0.72f, Theme::Purple);
}

void BenchmarkView::DrawTable(const Series& data, const std::vector<std::size_t>& points)
{
    if (!ImGui::BeginTable("ResultsTable", 11, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                           ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable |
                           ImGuiTableFlags_SizingFixedFit, {0, 172}))
    {
        return;
    }
    for (const char* label : {"V", "E", "Топология", "Порядок", "Повторы", "BFS, мс", "DFS, мс",
                             "DFS / BFS", "Группы", "Посещено", "Старт"})
    {
        ImGui::TableSetupColumn(label);
    }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(std::min<std::size_t>(points.size(), INT_MAX)));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            auto index = points[static_cast<std::size_t>(i)];
            const auto& point = data.points[index];
            const auto& report = point.report;
            const auto& record = report.records.Get(0);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            auto label = std::to_string(record.vertices);
            if (ImGui::Selectable(label.c_str(), selected == index, ImGuiSelectableFlags_SpanAllColumns))
            {
                selected = index;
            }
            ImGui::PopID();
            NumericCell("%zu", record.edges);
            TextCell(TopologyLabel(record));
            TextCell(OrderText[static_cast<int>(record.input.order)]);
            NumericCell("%zu", report.records.GetLength() / 2);
            TextCell(Number(report.bfsMedian).c_str());
            TextCell(Number(report.dfsMedian).c_str());
            TextCell(report.bfsMedian != 0 ? Number(report.dfsMedian / report.bfsMedian).c_str() : "—");
            NumericCell("%zu", record.components);
            NumericCell("%zu", record.visited);
            TextCell(record.startKey.empty() ? "—" : record.startKey.c_str());
        }
    }
    ImGui::EndTable();
}

void BenchmarkView::DrawSamples(const Point& point)
{
    const auto& result = point.report;
    const auto& record = result.records.Get(0);
    ImGui::Text("%s · %s · %s", TaskLabel(record.task), TopologyLabel(record),
                OrderText[static_cast<int>(record.input.order)]);
    const char* status = record.vertices == 0 ? "Пустой" : (record.components == 1 ? "Связный" : "Несвязный");
    ImGui::TextDisabled("%s · %zu компонент · посещено %zu из %zu", status, record.components, record.visited, record.vertices);
    if (record.input.generation && record.input.generation->topology == Topology::Random)
    {
        ImGui::TextDisabled("n = %zu · m = %zu · seed %u", record.input.generation->vertexCount,
                            record.input.generation->edgeCount, record.input.generation->seed);
    }
    if (record.input.order == EdgeOrder::Shuffled) { ImGui::TextDisabled("Seed порядка %u", record.input.shuffleSeed); }

    if (ImGui::BeginTable("Summary", 4, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg))
    {
        for (const char* label : {"Алгоритм", "Медиана, мс", "Мин., мс", "Макс., мс"}) { ImGui::TableSetupColumn(label); }
        ImGui::TableHeadersRow();
        auto row = [](const char* name, double median, const Coordinates& times)
        {
            ImGui::TableNextRow();
            TextCell(name);
            TextCell(Number(median).c_str());
            TextCell(Number(times.minimum).c_str());
            TextCell(Number(times.maximum).c_str());
        };
        row("BFS", result.bfsMedian, point.bfs);
        row("DFS", result.dfsMedian, point.dfs);
        ImGui::EndTable();
    }

    ImGui::Checkbox("Показать отдельные измерения", &raw);
    if (!raw) { return; }
    if (!ImGui::BeginTable("RawTimes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, {0, 150})) { return; }
    for (const char* label : {"Повтор", "Алгоритм", "Время, мс"}) { ImGui::TableSetupColumn(label); }
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(std::min<std::size_t>(result.records.GetLength(), INT_MAX)));
    while (clipper.Step())
    {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
        {
            const auto& run = result.records.Get(static_cast<std::size_t>(i));
            ImGui::TableNextRow();
            NumericCell("%zu", run.repeat);
            TextCell(run.algorithm.data());
            ImGui::TableNextColumn();
            ImGui::Text("%.9f", run.milliseconds);
        }
    }
    ImGui::EndTable();
}

void BenchmarkView::DrawResults(GuiTask& job)
{
    Theme::Heading("Время выполнения", 23.0f);
    if (history.empty())
    {
        ImGui::Spacing();
        Theme::Note("Выберите задачу и запустите замеры. Здесь появятся графики BFS и DFS и отдельные повторы.");
        return;
    }

    auto displayName = [](const std::shared_ptr<const Series>& entry)
    {
        return "Серия " + std::to_string(entry->id) + " · " + entry->source;
    };
    ImGui::SetNextItemWidth(std::min(260.0f, ImGui::GetContentRegionAvail().x * 0.57f));
    if (ImGui::BeginCombo("##Series", displayName(history[displayed]).c_str()))
    {
        for (std::size_t i = 0; i < history.size(); ++i)
        {
            if (ImGui::Selectable(displayName(history[i]).c_str(), i == displayed))
            {
                displayed = i;
                selected = 0;
                topologyFilter = orderFilter = -1;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(job.Busy());
    if (ImGui::Button("Экспорт CSV…"))
    {
        try { Save(job); }
        catch (const std::exception& exception) { SetMessage(exception.what(), true); }
    }
    ImGui::EndDisabled();

    const auto& series = *history[displayed];
    if (series.stopped) { Theme::Note("Серия остановлена. Показаны завершённые пары."); }

    std::array<bool, 7> topologies{};
    std::array<bool, 4> edgeOrders{};
    for (const auto& point : series.points)
    {
        const auto& record = point.report.records.Get(0);
        auto kind = record.input.generation ? static_cast<int>(record.input.generation->topology) : 6;
        topologies[kind] = true;
        edgeOrders[static_cast<int>(record.input.order)] = true;
    }
    float filterWidth = std::max(115.0f, ImGui::GetContentRegionAvail().x * 0.29f);
    ImGui::SetNextItemWidth(filterWidth);
    if (ImGui::BeginCombo("##TopologyFilter", topologyFilter < 0 ? "Все топологии" :
                                           (topologyFilter == 6 ? "Ручной" : TopologyText[topologyFilter])))
    {
        if (ImGui::Selectable("Все топологии", topologyFilter == -1)) { topologyFilter = -1; }
        for (std::size_t i = 0; i < topologies.size(); ++i)
        {
            if (topologies[i] && ImGui::Selectable(i == 6 ? "Ручной" : TopologyText[i], topologyFilter == static_cast<int>(i)))
            {
                topologyFilter = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(filterWidth);
    if (ImGui::BeginCombo("##OrderFilter", orderFilter < 0 ? "Все порядки" : OrderText[orderFilter]))
    {
        if (ImGui::Selectable("Все порядки", orderFilter == -1)) { orderFilter = -1; }
        for (std::size_t i = 0; i < edgeOrders.size(); ++i)
        {
            if (edgeOrders[i] && ImGui::Selectable(OrderText[i], orderFilter == static_cast<int>(i)))
            {
                orderFilter = static_cast<int>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(filterWidth);
    ImGui::Combo("##XAxis", &xAxis, AxisText, 3);

    std::vector<std::size_t> points;
    for (std::size_t i = 0; i < series.points.size(); ++i)
    {
        const auto& record = series.points[i].report.records.Get(0);
        int kind = record.input.generation ? static_cast<int>(record.input.generation->topology) : 6;
        if ((topologyFilter < 0 || topologyFilter == kind) &&
            (orderFilter < 0 || orderFilter == static_cast<int>(record.input.order)))
        {
            points.push_back(i);
        }
    }
    if (points.empty()) { Theme::Note("Для выбранных фильтров нет измерений."); return; }
    if (std::find(points.begin(), points.end(), selected) == points.end()) { selected = points.front(); }

    float width = ImGui::GetContentRegionAvail().x;
    bool sideBySide = width >= 630;
    if (sideBySide)
    {
        float lineWidth = width * 0.65f;
        DrawLineChart(series, points, {lineWidth, 236});
        ImGui::SameLine();
        DrawBars(series.points[selected], {width - lineWidth - ImGui::GetStyle().ItemSpacing.x, 236});
    }
    else
    {
        DrawLineChart(series, points, {width, 217});
        DrawBars(series.points[selected], {width, 173});
    }
    ImGui::Spacing();
    DrawTable(series, points);
    ImGui::Spacing();
    DrawSamples(series.points[selected]);
}

void BenchmarkView::Draw(GuiTask& job, const GraphView& network, const GraphView& graph)
{
    if (!job.Busy()) { running = false; }
    ImGui::PushID("PerformancePage");
    auto space = ImGui::GetContentRegionAvail();
    float left = space.x >= 1050 ? 284.0f : 254.0f;
    auto flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding;
    if (ImGui::BeginChild("BenchmarkForm", {left, space.y}, flags)) { DrawForm(job, network, graph); }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("BenchmarkResults", {0, space.y}, flags)) { DrawResults(job); }
    ImGui::EndChild();
    ImGui::PopID();
}
