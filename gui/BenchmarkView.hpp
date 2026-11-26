#ifndef BENCHMARKVIEW_HPP
#define BENCHMARKVIEW_HPP

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "BenchmarkData.hpp"
#include "GraphView.hpp"

std::optional<std::filesystem::path> ChooseBenchmarkCsv();

class BenchmarkView
{
private:
    struct Coordinates
    {
        double minimum = 0;
        double maximum = 0;
    };

    struct Point
    {
        BenchmarkReport report;
        Coordinates bfs;
        Coordinates dfs;
    };

    struct Series
    {
        std::size_t id = 0;
        std::string source;
        BenchmarkTask task = BenchmarkTask::SpanningTree;
        std::vector<Point> points;
        bool stopped = false;
    };

    ExperimentSettings settings;
    bool currentGraph = false;
    int currentSource = 0;
    std::optional<StationId> start;
    std::array<bool, 4> orders{true, true, false, false};
    int xAxis = 0;
    int topologyFilter = -1;
    int orderFilter = -1;
    std::vector<std::shared_ptr<const Series>> history;
    std::size_t displayed = 0;
    std::size_t selected = 0;
    bool raw = false;
    std::string message;
    bool error = false;
    bool running = false;

    void Start(GuiTask& job, const GraphView& network, const GraphView& graph);
    void Save(GuiTask& job);
    void DrawForm(GuiTask& job, const GraphView& network, const GraphView& graph);
    void DrawResults(GuiTask& job);
    void DrawLineChart(const Series& data, const std::vector<std::size_t>& points, ImVec2 size);
    void DrawBars(const Point& point, ImVec2 size);
    void DrawTable(const Series& data, const std::vector<std::size_t>& points);
    void DrawSamples(const Point& point);
    void SetMessage(std::string text, bool failed = false);

    std::vector<EdgeOrder> ActiveOrders() const;
    static Point Summarize(BenchmarkReport report, std::size_t series, const std::string& source);

public:
    void Draw(GuiTask& job, const GraphView& network, const GraphView& graph);
};

#endif
