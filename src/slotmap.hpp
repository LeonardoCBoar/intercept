#pragma once

// builtin
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <optional>
#include <variant>
#include <vector>

// local
#include "assert.hpp"



template <typename T>
class SlotMap {
private:

    // it is theoretically and practically impossible for an item to exist with the maximum
    // position, since the smallest possible slot has a m_size greater than 1 byte and no modern
    // computer has a memory space as large as size_t allows
    static inline size_t NONE = SIZE_MAX;
    using Version = uint32_t;

    struct EmptySlot {
        size_t next_free;
    };

    struct OccupiedSlot {
        T value;
        size_t next_occupied;
        size_t last_occupied;
    };

    struct Slot {
        Version version;
        std::variant<OccupiedSlot, EmptySlot> content;

        static Slot new_empty(size_t next_free) {
            return Slot{0, EmptySlot{next_free}};
        }

        void to_occupied(T value, size_t next, size_t last) {
            rb_assert(this->is_empty());
            this->content = OccupiedSlot{.value = std::move(value), .next_occupied = next, .last_occupied = last};
        }

        [[nodiscard]]
        T to_empty(size_t next_free) {
            rb_assert(this->is_occupied());

            auto ret = std::move(this->occupied().value);
            this->content = EmptySlot{.next_free = next_free};
            ++this->version;

            return ret;
        }

        Slot(Version _version, decltype(content) _content): version(_version), content(std::move(_content)) {}
        Slot(Slot&& other): version(other.version), content(std::move(other.content)) {}
        Slot(Slot const&) = delete;
        Slot& operator=(Slot const&) = delete;

        bool is_empty() const {
            return std::holds_alternative<EmptySlot>(this->content);
        }

        bool is_occupied() const {
            return std::holds_alternative<OccupiedSlot>(this->content);
        }

        EmptySlot& empty() {
            rb_assert(this->is_empty());
            return std::get<EmptySlot>(this->content);
        }

        OccupiedSlot& occupied() {
            rb_assert(this->is_occupied());
            return std::get<OccupiedSlot>(this->content);
        }
    };

    class iterator {
    public:

        friend class SlotMap<T>;

        using iterator_category = std::forward_iterator_tag;
        using difference_type = size_t;
        using value_type = T;
        using pointer = T*;
        using reference = T&;

    private:

        SlotMap<T>& slotmap;
        size_t idx;

    private:

        iterator(SlotMap<T>& _slotmap, size_t _idx): slotmap(_slotmap), idx(_idx) {}

    public:

        reference operator*() {
            rb_assert(this->slotmap.valid_index(this->idx));
            return this->slotmap.m_data[this->idx].occupied().value;
        }

        pointer operator->() {
            return &this->operator*();
        }

        iterator operator++() // prefix
        {
            rb_assert(this->slotmap.valid_index(this->idx));
            this->idx = this->slotmap.m_data[this->idx].occupied().next_occupied;
            rb_assert(this->slotmap.valid_index(this->idx) || this->idx == NONE);
            return *this;
        }

        iterator operator++(int) // postfix
        {
            auto it = *this;
            ++(*this);

            return it;
        }

        friend bool operator==(iterator const& a, iterator const& b) {
            return (&a.slotmap == &b.slotmap) && (a.idx == b.idx);
        }

        friend bool operator!=(iterator const& a, iterator const& b) {
            return !(a == b);
        }
    };

    friend class iterator;

public:

    class Key {
    private:

        friend SlotMap<T>;

        size_t m_idx;
        Version m_version;

    public:

        Key(size_t _idx, Version _version): m_idx(_idx), m_version(_version) {}

        static Key null() {

            return Key{NONE, 0};
        }

        friend bool operator==(Key const& a, Key const& b) {

            return (a.m_idx == b.m_idx) && (a.m_version == b.m_version);
        }
    };

private:

    std::vector<Slot> m_data;
    size_t empty_list_head;
    size_t first_occupied;

    size_t _size;

public:

    SlotMap(SlotMap const&) = delete;
    SlotMap& operator=(SlotMap const&) = delete;

    SlotMap(): m_data{}, empty_list_head(NONE), first_occupied(NONE), _size(0) {}

    std::optional<std::reference_wrapper<T>> get(Key key) {
        if (this->contains(key) == false)
            return std::nullopt;

        return std::optional{std::reference_wrapper{this->m_data[key.m_idx].occupied().value}};
    }

    T& unchecked_get(Key key) {
        rb_assert(this->contains(key));
        return this->get(key).value().get();
    }

    [[nodiscard]]
    Key push(T&& value) {

        // caso não haja nenhum slot vazio, aloca um novo slot
        if (this->empty_list_head == NONE) {
            this->push_empty_slot();
        }

        // é garantido que haja um slot disponível, já que um novo é alocado caso não haja nenhum
        rb_assert(this->empty_list_head != NONE);
        // o slot vazio apontado pela lista deve ter um index válido
        rb_assert(this->empty_list_head < this->m_data.size());

        size_t slot_idx = this->empty_list_head;
        Slot* slot = &this->m_data[slot_idx];

        // o slot apontado pela lista de m_slots vazios deveria ser vazio
        rb_assert(slot->is_empty());
        this->empty_list_head = slot->empty().next_free;


        size_t last_occupied = NONE;
        if (slot_idx != 0) {


            last_occupied = slot_idx - 1;
            rb_assert(this->m_data[last_occupied].is_occupied());
        }

        size_t next_occupied = NONE;
        if (last_occupied != NONE)
            next_occupied = this->m_data[last_occupied].occupied().next_occupied;
        else
            next_occupied = this->first_occupied;

        if (slot_idx == 0)
            this->first_occupied = slot_idx;

        if (next_occupied != NONE)
            rb_assert(this->m_data[next_occupied].is_occupied());


        if (last_occupied != NONE)
            this->m_data[last_occupied].occupied().next_occupied = slot_idx;

        if (next_occupied != NONE)
            this->m_data[next_occupied].occupied().last_occupied = slot_idx;

        slot->to_occupied(std::move(value), next_occupied, last_occupied);
        this->_size += 1;
        return Key{slot_idx, slot->version};
    }

    std::optional<T> remove(Key key) {
        if (this->contains(key) == false)
            return std::nullopt;

        size_t slot_idx = key.m_idx;
        Slot* slot = &this->m_data[slot_idx];


        if (auto last_occupied = slot->occupied().last_occupied; last_occupied != NONE) {
            rb_assert(this->m_data[last_occupied].is_occupied());
            rb_assert(this->m_data[last_occupied].occupied().next_occupied == slot_idx);
            this->m_data[last_occupied].occupied().next_occupied = slot->occupied().next_occupied;
        }

        if (auto next_occupied = slot->occupied().next_occupied; next_occupied != NONE) {
            rb_assert(this->m_data[next_occupied].is_occupied());
            rb_assert(this->m_data[next_occupied].occupied().last_occupied == slot_idx);
            this->m_data[next_occupied].occupied().last_occupied = slot->occupied().last_occupied;
        }

        if (slot_idx == this->first_occupied) {
            rb_assert(slot->occupied().last_occupied == NONE);
            this->first_occupied = slot->occupied().next_occupied;
        }


        auto value = slot->to_empty(this->empty_list_head);
        rb_assert(slot->is_empty());
        this->empty_list_head = slot_idx;

        this->_size -= 1;

        return std::optional{std::move(value)};
    }

    T unchecked_remove(Key key) {
        rb_assert(this->contains(key));
        return this->remove(key).value();
    }

    bool contains(Key key) const {
        if (this->valid_index(key.m_idx) == false)
            return false;

        if (this->m_data[key.m_idx].m_version != key.m_version)
            return false;

        return true;
    }

    bool valid_index(size_t idx) const {
        if (idx >= this->m_data.size())
            return false;

        if (this->m_data[idx].is_occupied() == false)
            return false;

        return true;
    }

    size_t size() const {
        return this->_size;
    }

    size_t capacity() const {
        return this->m_data.size();
    }

    iterator begin() {
        return iterator{*this, this->first_occupied};
    }

    iterator end() {
        return iterator{*this, NONE};
    }

    // T pop_first();
    // void clear();

private:

    size_t push_empty_slot() {
        rb_assert(this->empty_list_head == NONE);

        this->m_data.push_back(Slot::new_empty(NONE));
        this->empty_list_head = this->m_data.size() - 1;

        return empty_list_head;
    }
};
