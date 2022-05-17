/*
  Created on 23.06.18.
*/

#include "common.hpp"
#include <unordered_map>
#include <playerlist.hpp>
#include "PlayerTools.hpp"
#include "entitycache.hpp"
#include "settings/Bool.hpp"
#include "MiscTemporary.hpp"

namespace player_tools
{

static settings::Int betrayal_limit{ "player-tools.betrayal-limit", "2" };
static settings::Boolean betrayal_sync{ "player-tools.betrayal-ipc-sync", "true" };

static settings::Boolean taunting{ "player-tools.ignore.taunting", "false" };
static settings::Boolean ignoreCathook{ "player-tools.ignore.cathook", "true" };

static std::unordered_map<unsigned, unsigned> betrayal_list{};

bool shouldTargetSteamId(unsigned id)
{
    if (betrayal_limit)
    {
        if (betrayal_list[id] > (unsigned) *betrayal_limit)
            return true;
    }

    auto &pl = playerlist::AccessData(id);
    if (playerlist::IsFriendly(pl.state) || (pl.state == playerlist::EState::CAT && *ignoreCathook))
        return false;
    return true;
}

bool shouldTarget(CachedEntity *entity)
{
    if (entity->m_Type() == ENTITY_PLAYER)
    {
        if (taunting && HasCondition<TFCond_Taunting>(entity) && CE_INT(entity, netvar.m_iTauntIndex) == 3)
            return false;
        if (HasCondition<TFCond_HalloweenGhostMode>(entity))
            return false;
        // Don't shoot players in truce
        if (isTruce())
            return false;
        return shouldTargetSteamId(entity->player_info.friendsID);
    }
    else if (entity->m_Type() == ENTITY_BUILDING)
        // Don't shoot buildings in truce
        if (isTruce())
            return false;

    return true;
}
bool shouldAlwaysRenderEspSteamId(unsigned id)
{
    if (id == 0)
        return false;

    auto &pl = playerlist::AccessData(id);
    if (pl.state != playerlist::EState::DEFAULT)
        return true;
    return false;
}
bool shouldAlwaysRenderEsp(CachedEntity *entity)
{
    if (entity->m_Type() == ENTITY_PLAYER)
    {
        return shouldAlwaysRenderEspSteamId(entity->player_info.friendsID);
    }

    return false;
}

#if ENABLE_VISUALS
std::optional<colors::rgba_t> forceEspColorSteamId(unsigned id)
{
    if (id == 0)
        return std::nullopt;

    auto pl = playerlist::Color(id);
    if (pl != colors::empty)
        return std::optional<colors::rgba_t>{ pl };

    return std::nullopt;
}
std::optional<colors::rgba_t> forceEspColor(CachedEntity *entity)
{
    if (entity->m_Type() == ENTITY_PLAYER)
    {
        return forceEspColorSteamId(entity->player_info.friendsID);
    }

    return std::nullopt;
}
#endif

void onKilledBy(unsigned id)
{
    auto &pl = playerlist::AccessData(id);
    if (!shouldTargetSteamId(id) && !playerlist::IsFriendly(pl.state))
    {
        // We ignored the gamer, but they still shot us
        if (betrayal_list.find(id) == betrayal_list.end())
            betrayal_list[id] = 0;
        betrayal_list[id]++;
        // Notify other bots
        if (betrayal_list[id] == *betrayal_limit) {
            g_IEngine->ClientCmd_Unrestricted("cat_pl_add_id %s RAGE cat_pl_save"),id;
        }
        if (id && betrayal_list[id] == *betrayal_limit && betrayal_sync)
        {
            if (ipc::peer && ipc::peer->connected)
            {
                std::string command = "cat_pl_load";
                if (command.length() >= 63)
                    ipc::peer->SendMessage(0, -1, ipc::commands::execute_client_cmd_long, command.c_str(), command.length() + 1);
                else
                    ipc::peer->SendMessage(command.c_str(), -1, ipc::commands::execute_client_cmd, 0, 0);
            }
        }

    }
}

void onKilledBy(CachedEntity *entity)
{
    onKilledBy(entity->player_info.friendsID);
}

class PlayerToolsEventListener : public IGameEventListener2
{
    void FireGameEvent(IGameEvent *event) override
    {

        int killer_id = GetPlayerForUserID(event->GetInt("attacker"));
        int victim_id = GetPlayerForUserID(event->GetInt("userid"));

        if (victim_id == g_IEngine->GetLocalPlayer())
        {
            onKilledBy(ENTITY(killer_id));
            return;
        }
    }
};

PlayerToolsEventListener &listener()
{
    static PlayerToolsEventListener object{};
    return object;
}

static InitRoutine register_event(
    []()
    {
        g_IEventManager2->AddListener(&listener(), "player_death", false);
        EC::Register(
            EC::Shutdown, []() { g_IEventManager2->RemoveListener(&listener()); }, "playerlist_shutdown");
    });
} // namespace player_tools
