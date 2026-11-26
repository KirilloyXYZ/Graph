#ifndef GRAPHVIEW_HPP
#define GRAPHVIEW_HPP

#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "GraphCanvas.hpp"
#include "GuiTask.hpp"

enum class AnalysisAction { Traversal, Components, SpanningTree };

bool DrawStationPicker(const char* label, const GraphDocument& document, std::optional<StationId>& selection);
void DrawTopologyPicker(Topology& selected);

class GraphView
{
private:
    struct Analysis
    {
        TraversalKeys<StationId> keys;
        std::unordered_map<StationId, std::size_t> steps;
        std::map<ChannelKey, std::size_t> tree;
        AnalysisAction action = AnalysisAction::Traversal;
        TraversalMethod method = TraversalMethod::BreadthFirst;
        SpanningTreeStatus status = SpanningTreeStatus::Empty;
        std::size_t shown = 0;
        float elapsed = 0;
        bool playing = false;
        bool overlay = true;
        bool treeOnly = false;
    };

    bool solar;
    std::shared_ptr<GraphDocument> document;
    std::shared_ptr<GraphDocument> trial;
    bool showTrial = true;
    std::optional<StationId> offlineStation;
    std::optional<ChannelKey> offlineChannel;
    std::vector<SplitGroup> splits;
    CanvasState canvas;
    std::optional<StationId> station;
    std::optional<ChannelKey> channel;
    std::optional<StationId> start;
    std::optional<StationId> first;
    std::optional<StationId> second;
    std::optional<std::size_t> group;
    AnalysisAction action = AnalysisAction::Traversal;
    TraversalMethod method = TraversalMethod::BreadthFirst;
    std::shared_ptr<Analysis> analysis;
    float speed = 4.0f;
    bool linkMode = false;
    bool pickStart = false;
    bool showInspector = false;
    std::string message;
    bool error = false;
    std::string newKey;
    int newPlanet = 2;
    bool newOrbit = false;
    GenerationOptions generation{Topology::Cycle, 8, 12, 42};
    std::optional<GenerationOptions> lastGeneration;

    std::function<void()> confirmation;
    std::string confirmationText;
    bool openConfirmation = false;
    bool openBulk = false;
    std::string bulkVertices;
    std::string bulkEdges;
    std::string bulkError;
    bool showStations = false;
    bool showChannels = false;
    std::string stationFilter;
    std::string edgeFilter;
    std::vector<ChannelKey> edgePage;
    const GraphDocument* edgePageSource = nullptr;
    std::string edgePageFilter;
    std::size_t edgePageNumber = 0;
    bool edgePageMore = false;

    void Say(std::string detail, bool failed = false);
    void Try(const std::function<void()>& action);
    void ConfirmReplacement(std::string text, std::function<void()> action);
    void ResetSelection(bool keep);
    void SetDocument(std::shared_ptr<GraphDocument> next, bool replaceView);
    void Change(GuiTask& job, const char* name, std::function<void(GraphDocument&)> operation,
                std::optional<StationId> select = std::nullopt);
    void AddConnection(GuiTask& job, const StationId& u, const StationId& v);
    void Generate(GuiTask& job, GenerationOptions options);
    void Transfer(GuiTask& job, GraphView& target);
    void StartAnalysis(GuiTask& job);
    void BeginTrial(GuiTask& job);
    void ApplyFailure(GuiTask& job, std::optional<StationId> key, std::optional<ChannelKey> edge);
    void FinishTrial(GuiTask& job, bool restore);

    void DrawSummary();
    void DrawEditor(GuiTask& job, GraphView* network);
    void DrawInspector(GuiTask& job);
    void DrawLists(const GraphDocument& current);
    void DrawAnalysis();
    void DrawTrial(GuiTask& job);
    void DrawDialogs(GuiTask& job);
    void HandleCanvas(GuiTask& job, CanvasEvent event);

public:
    explicit GraphView(bool network);
    void Draw(GuiTask& job, GraphView* network = nullptr);
    std::shared_ptr<GraphDocument> Current() const { return trial && showTrial ? trial : document; }
    std::shared_ptr<const GraphDocument> Original() const { return document; }
    bool IsExperiment() const { return trial != nullptr; }
};

#endif
