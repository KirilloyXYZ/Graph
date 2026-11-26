#ifndef ARRAYSTORAGE_HPP
#define ARRAYSTORAGE_HPP

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <utility>

#include "IDictionary.hpp"
#include "SortedSequence.hpp"

template<typename T>
class ArrayStorage : public IDictionary<std::size_t, T>
{
private:
    using Entry = std::pair<std::size_t, T>;
    std::unique_ptr<ISortedSequence<Entry>> items;

    static bool CompareEntries(const Entry& left, const Entry& right);

    class Enumerator : public IEnumerator<T, std::size_t>
    {
    private:
        std::unique_ptr<IEnumerator<Entry, std::size_t>> entries;

    public:
        explicit Enumerator(const ISortedSequence<Entry>& items) : entries(items.GetEnumerator()) { }
        bool MoveNext() override { return this->entries->MoveNext(); }
        const T& GetCurrent() const override { return this->entries->GetCurrent().second; }
        const std::size_t& GetCurrentKey() const override { return this->entries->GetCurrent().first; }
    };

public:
    ArrayStorage();
    ArrayStorage(const ArrayStorage<T>& other);

    std::size_t GetCount() const override;
    std::size_t GetCapacity() const override;
    const T& Get(const std::size_t& index) const override;
    const T* Find(const std::size_t& index) const override;
    bool ContainsKey(const std::size_t& index) const override;
    void Add(const std::size_t& index, const T& value) override;
    void Set(const std::size_t& index, const T& value) override;
    void Remove(const std::size_t& index) override;
    bool TryRemove(const std::size_t& index) override;
    void Clear() override;
    void Load(const Sequence<Entry>& entries) override;
    std::unique_ptr<IEnumerator<T, std::size_t>> GetEnumerator() const override;
    std::unique_ptr<IDictionary<std::size_t, T>> Clone() const override;
    std::unique_ptr<IDictionary<std::size_t, T>> CreateEmpty() const override;
};

template<typename T>
bool ArrayStorage<T>::CompareEntries(const Entry& left, const Entry& right) { return left.first < right.first; }

template<typename T>
ArrayStorage<T>::ArrayStorage() : items(std::make_unique<SortedSequence<Entry>>(CompareEntries)) { }

template<typename T>
ArrayStorage<T>::ArrayStorage(const ArrayStorage<T>& other) : items(other.items->Clone()) { }

template<typename T>
std::size_t ArrayStorage<T>::GetCount() const { return this->items->GetLength(); }

template<typename T>
std::size_t ArrayStorage<T>::GetCapacity() const { return this->items->GetCapacity(); }

template<typename T>
const T& ArrayStorage<T>::Get(const std::size_t& index) const
{
    const T* value = this->Find(index);
    if (value == nullptr) { throw std::out_of_range("ArrayStorage::Get: key not found"); }
    return *value;
}

template<typename T>
const T* ArrayStorage<T>::Find(const std::size_t& index) const
{
    std::ptrdiff_t position = this->items->IndexOf(Entry{index, T{}});
    return position < 0 ? nullptr : &this->items->Get(static_cast<std::size_t>(position)).second;
}

template<typename T>
bool ArrayStorage<T>::ContainsKey(const std::size_t& index) const { return this->Find(index) != nullptr; }

template<typename T>
void ArrayStorage<T>::Add(const std::size_t& index, const T& value)
{
    if (this->ContainsKey(index)) { throw std::invalid_argument("ArrayStorage::Add: duplicate key"); }
    this->items->Add(Entry{index, value});
}

template<typename T>
void ArrayStorage<T>::Set(const std::size_t& index, const T& value)
{
    Entry entry{index, value};
    std::ptrdiff_t position = this->items->IndexOf(entry);
    if (position < 0) { this->items->Add(entry); }
    else { this->items->Replace(static_cast<std::size_t>(position), entry); }
}

template<typename T>
void ArrayStorage<T>::Remove(const std::size_t& index)
{
    if (!this->TryRemove(index)) { throw std::out_of_range("ArrayStorage::Remove: key not found"); }
}

template<typename T>
bool ArrayStorage<T>::TryRemove(const std::size_t& index)
{
    std::ptrdiff_t position = this->items->IndexOf(Entry{index, T{}});
    if (position < 0) { return false; }
    this->items->RemoveAt(static_cast<std::size_t>(position));
    return true;
}

template<typename T>
void ArrayStorage<T>::Clear() { this->items->Clear(); }

template<typename T>
void ArrayStorage<T>::Load(const Sequence<Entry>& entries)
{
    for (std::size_t i = 1; i < entries.GetLength(); ++i)
    {
        if (entries.Get(i - 1).first >= entries.Get(i).first)
        {
            throw std::invalid_argument("ArrayStorage::Load: indices must be strictly increasing");
        }
    }
    this->items->Load(entries);
}

template<typename T>
std::unique_ptr<IEnumerator<T, std::size_t>> ArrayStorage<T>::GetEnumerator() const
{
    return std::make_unique<Enumerator>(*this->items);
}

template<typename T>
std::unique_ptr<IDictionary<std::size_t, T>> ArrayStorage<T>::Clone() const
{
    return std::make_unique<ArrayStorage<T>>(*this);
}

template<typename T>
std::unique_ptr<IDictionary<std::size_t, T>> ArrayStorage<T>::CreateEmpty() const
{
    return std::make_unique<ArrayStorage<T>>();
}

#endif
