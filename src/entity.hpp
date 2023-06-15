#pragma once

// local
#include "slotmap.hpp"

// builtin
#include <functional>
#include <optional>

// extern
#include <SFML/Graphics/RenderWindow.hpp>
#include <glm/vec2.hpp>


class Entity;
class StateManager;
using Id = SlotMap<std::unique_ptr<Entity>>::Key;

class Entity {

private:

    Id m_id;

public:

    virtual void render(sf::RenderWindow&) const = 0;

    // events
    virtual void update(StateManager&) = 0;
    virtual void explosion(StateManager&, glm::u32vec2, double) = 0;

    [[nodiscard]]
    Id get_id() const {

        return this->m_id;
    }

public:

    explicit Entity(Id _id): m_id(_id) {}
    virtual ~Entity() = default;
};
