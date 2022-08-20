#pragma once

#include <array>
#include <stdint.h>

namespace hacks::NavBot
{
bool init(bool first_cm);
namespace task
{

enum task : uint8_t
{
    none = 0,
    sniper_spot,
    stay_near,
    health,
    ammo,
    dispenser,
    followbot,
    outofbounds,
    capture
};

struct Task
{
    task id;
    int priority;
    Task(task id)
    {
        this->id = id;
        priority = id == none ? 0 : 5;
    }
    Task(task id, int priority)
    {
        this->id       = id;
        this->priority = priority;
    }
    operator task()
    {
        return id;
    }
};

constexpr std::array<task, 3> blocking_tasks{ followbot, outofbounds };
extern Task current_task;
} // namespace task
struct bot_class_config
{
    float min;
    float preferred;
    float max;
};
} // namespace hacks::NavBot

