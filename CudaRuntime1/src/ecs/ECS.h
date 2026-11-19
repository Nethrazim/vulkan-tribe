#pragma once

#include <cassert>
#include <cstdint>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vt {

using Entity = std::uint32_t;
constexpr Entity kInvalidEntity = 0;

// Type-erased handle so Registry can clear every component of a dead entity.
class IStorage {
public:
    virtual ~IStorage() = default;
    virtual void remove(Entity entity) = 0;
    virtual bool contains(Entity entity) const = 0;
};

// Dense component array plus an entity -> slot index. Removal swaps the last
// element into the freed slot, so iteration stays cache-friendly.
template <class T>
class Storage final : public IStorage {
public:
    T &add(Entity entity, T value) {
        const auto it = index_.find(entity);
        if (it != index_.end()) {
            items_[it->second] = std::move(value);
            return items_[it->second];
        }
        index_.emplace(entity, items_.size());
        owners_.push_back(entity);
        items_.push_back(std::move(value));
        return items_.back();
    }

    bool contains(Entity entity) const override {
        return index_.find(entity) != index_.end();
    }

    T *tryGet(Entity entity) {
        const auto it = index_.find(entity);
        return it == index_.end() ? nullptr : &items_[it->second];
    }

    T &get(Entity entity) {
        T *found = tryGet(entity);
        assert(found && "component not present on entity");
        return *found;
    }

    void remove(Entity entity) override {
        const auto it = index_.find(entity);
        if (it == index_.end()) {
            return;
        }
        const std::size_t slot = it->second;
        const std::size_t last = items_.size() - 1;
        if (slot != last) {
            items_[slot] = std::move(items_[last]);
            owners_[slot] = owners_[last];
            index_[owners_[slot]] = slot;
        }
        items_.pop_back();
        owners_.pop_back();
        index_.erase(entity);
    }

    std::size_t size() const { return items_.size(); }
    const std::vector<Entity> &entities() const { return owners_; }

private:
    std::vector<T> items_;
    std::vector<Entity> owners_;
    std::unordered_map<Entity, std::size_t> index_;
};

// Minimal registry: entity ids, component storages, and a filtered iterator.
class Registry {
public:
    Entity create() { return ++lastEntity_; }

    void destroy(Entity entity) {
        for (auto &entry : storages_) {
            entry.second->remove(entity);
        }
    }

    template <class T>
    T &add(Entity entity, T value = T{}) {
        return storage<T>().add(entity, std::move(value));
    }

    template <class T>
    T *tryGet(Entity entity) {
        return hasStorage<T>() ? storage<T>().tryGet(entity) : nullptr;
    }

    template <class T>
    T &get(Entity entity) {
        return storage<T>().get(entity);
    }

    template <class T>
    bool has(Entity entity) {
        return hasStorage<T>() && storage<T>().contains(entity);
    }

    template <class T>
    void remove(Entity entity) {
        if (hasStorage<T>()) {
            storage<T>().remove(entity);
        }
    }

    // Calls fn(entity) for every entity that carries First and all of Rest.
    // Iterates a snapshot so fn may add or remove components safely.
    template <class First, class... Rest, class Fn>
    void each(Fn &&fn) {
        if (!hasStorage<First>()) {
            return;
        }
        const std::vector<Entity> snapshot = storage<First>().entities();
        for (const Entity entity : snapshot) {
            if ((has<Rest>(entity) && ...)) {
                fn(entity);
            }
        }
    }

    template <class T>
    Storage<T> &storage() {
        const std::type_index key{typeid(T)};
        auto it = storages_.find(key);
        if (it == storages_.end()) {
            it = storages_.emplace(key, std::make_unique<Storage<T>>()).first;
        }
        return *static_cast<Storage<T> *>(it->second.get());
    }

private:
    template <class T>
    bool hasStorage() const {
        return storages_.find(std::type_index{typeid(T)}) != storages_.end();
    }

    Entity lastEntity_ = kInvalidEntity;
    std::unordered_map<std::type_index, std::unique_ptr<IStorage>> storages_;
};

} // namespace vt
