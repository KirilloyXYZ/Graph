#ifndef GUITASK_HPP
#define GUITASK_HPP

#include <atomic>
#include <cstddef>
#include <functional>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

class GuiTask
{
public:
    enum class Stage { Preparing, Measuring, Saving };

private:
    std::thread worker;
    std::atomic<bool> stopping{false};
    std::atomic<bool> ready{false};
    std::function<void()> publish;
    std::string error;
    const void* owner = nullptr;
    std::string title;
    std::size_t total = 0;
    std::size_t cases = 0;

public:
    std::atomic<Stage> stage{Stage::Preparing};
    std::atomic<std::size_t> completed{0};
    std::atomic<std::size_t> completedCases{0};

    ~GuiTask()
    {
        RequestStop();
        if (worker.joinable()) { worker.join(); }
    }

    bool Busy() const { return worker.joinable(); }
    bool Locks(const void* view) const { return Busy() && owner == view; }
    bool Stopping() const { return stopping.load(); }
    const std::string& Title() const { return title; }
    std::size_t Total() const { return total; }
    std::size_t Cases() const { return cases; }
    void RequestStop() { stopping = true; }

    template<typename Work>
    void Start(const void* view, std::string description, std::size_t operations, std::size_t scenarios, Work work)
    {
        if (Busy()) { throw std::logic_error("Дождитесь завершения текущей операции"); }
        owner = view;
        title = std::move(description);
        total = operations;
        cases = scenarios;
        completed = 0;
        completedCases = 0;
        stage = Stage::Preparing;
        stopping = false;
        ready = false;
        error.clear();
        publish = {};
        worker = std::thread([this, work = std::move(work)]() mutable
        {
            try { publish = work(*this); }
            catch (const std::bad_alloc&) { error = "Недостаточно памяти для выполнения операции."; }
            catch (const std::exception& exception) { error = exception.what(); }
            catch (...) { error = "Неизвестная ошибка фоновой операции"; }
            ready.store(true, std::memory_order_release);
        });
    }

    void Poll()
    {
        if (!Busy() || !ready.load(std::memory_order_acquire)) { return; }
        worker.join();
        owner = nullptr;
        auto finish = std::move(publish);
        if (!error.empty()) { throw std::runtime_error(error); }
        if (finish) { finish(); }
    }
};

#endif
