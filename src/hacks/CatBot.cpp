/*
 * CatBot.cpp
 *
 *  Created on: Dec 30, 2017
 *      Author: nullifiedcat
 */

#include <settings/Bool.hpp>
#include "CatBot.hpp"
#include "common.hpp"
#include "hack.hpp"
#include "PlayerTools.hpp"
#include "e8call.hpp"
#include "navparser.hpp"
#include "SettingCommands.hpp"
#include "glob.h"

namespace hacks::catbot
{

static settings::Boolean micspam{ "cat-bot.micspam.enable", "false" };
static settings::Int micspam_on{ "cat-bot.micspam.interval-on", "3" };
static settings::Int micspam_off{ "cat-bot.micspam.interval-off", "60" };

static settings::Boolean random_votekicks{ "cat-bot.votekicks", "false" };
static settings::Boolean autovote_map{ "cat-bot.autovote-map", "true" };

settings::Boolean catbotmode{ "cat-bot.enable", "true" };
settings::Boolean anti_motd{ "cat-bot.anti-motd", "false" };

struct catbot_user_state
{
    int treacherous_kills{ 0 };
};

static std::unordered_map<unsigned, catbot_user_state> human_detecting_map{};

int globerr(const char *path, int eerrno)
{
    logging::Info("%s: %s\n", path, strerror(eerrno));
    // let glob() keep going
    return 0;
}

static std::string blacklist;

void do_random_votekick()
{
    std::vector<int> targets;
    player_info_s local_info;

    if (CE_BAD(LOCAL_E) || !GetPlayerInfo(LOCAL_E->m_IDX, &local_info))
        return;
    for (int i = 1; i < g_GlobalVars->maxClients; ++i)
    {
        player_info_s info;
        if (!GetPlayerInfo(i, &info) || !info.friendsID)
            continue;
        if (g_pPlayerResource->GetTeam(i) != g_pLocalPlayer->team)
            continue;
        if (info.friendsID == local_info.friendsID)
            continue;
        if (!player_tools::shouldTargetSteamId(info.friendsID))
            continue;

        targets.push_back(info.userID);
    }

    if (targets.empty())
        return;

    int target = targets[rand() % targets.size()];
    player_info_s info;
    if (!GetPlayerInfo(GetPlayerForUserID(target), &info))
        return;
    hack::ExecuteCommand("callvote kick \"" + std::to_string(target) + " cheating\"");
}

// Store information
struct Posinfo
{
    float x;
    float y;
    float z;
    std::string lvlname;
    Posinfo(float _x, float _y, float _z, std::string _lvlname)
    {
        x       = _x;
        y       = _y;
        z       = _z;
        lvlname = _lvlname;
    }
    Posinfo(){};
};

void SendNetMsg(INetMessage &msg)
{

}

class CatBotEventListener2 : public IGameEventListener2
{
    void FireGameEvent(IGameEvent *) override
    {
        // vote for current map if catbot mode and autovote is on
        if (catbotmode && autovote_map)
            g_IEngine->ServerCmd("next_map_vote 0");
    }
};

CatBotEventListener2 &listener2()
{
    static CatBotEventListener2 object{};
    return object;
}

Timer timer_votekicks{};
static Timer timer_abandon{};
static Timer timer_catbot_list{};

static int count_ipc = 0;
static std::vector<unsigned> ipc_list{ 0 };

static bool waiting_for_quit_bool{ false };
static Timer waiting_for_quit_timer{};

static std::vector<unsigned> ipc_blacklist{};
#if ENABLE_IPC
void update_ipc_data(ipc::user_data_s &data)
{
    data.ingame.bot_count = count_ipc;
}
#endif

Timer level_init_timer{};
Timer micspam_on_timer{};
Timer micspam_off_timer{};


CatCommand print_ammo("debug_print_ammo", "debug",
                      []()
                      {
                          if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer() || CE_BAD(LOCAL_W))
                              return;
                          logging::Info("Current slot: %d", re::C_BaseCombatWeapon::GetSlot(RAW_ENT(LOCAL_W)));
                          for (int i = 0; i < 10; i++)
                              logging::Info("Ammo Table %d: %d", i, CE_INT(LOCAL_E, netvar.m_iAmmo + i * 4));
                      });
static Timer disguise{};
static Timer report_timer{};
static std::string health = "Health: 0/0";
static std::string ammo   = "Ammo: 0/0";
static int max_ammo;
static CachedEntity *local_w;
// TODO: add more stuffs
static void cm()
{
    if (!*catbotmode)
        return;

    if (CE_GOOD(LOCAL_E))
    {
        if (LOCAL_W != local_w)
        {
            local_w  = LOCAL_W;
            max_ammo = 0;
        }
        float max_hp  = g_pPlayerResource->GetMaxHealth(LOCAL_E);
        float curr_hp = CE_INT(LOCAL_E, netvar.iHealth);
        int ammo0     = CE_INT(LOCAL_E, netvar.m_iClip2);
        int ammo2     = CE_INT(LOCAL_E, netvar.m_iClip1);
        if (ammo0 + ammo2 > max_ammo)
            max_ammo = ammo0 + ammo2;
        health = format("Health: ", curr_hp, "/", max_hp);
        ammo   = format("Ammo: ", ammo0 + ammo2, "/", max_ammo);
    }
    if (g_Settings.bInvalid)
        return;

    if (CE_BAD(LOCAL_E) || CE_BAD(LOCAL_W))
        return;

}

static Timer unstuck{};
static int unstucks;
void update()
{
    if (!catbotmode)
        return;

    if (CE_BAD(LOCAL_E))
        return;

    if (LOCAL_E->m_bAlivePlayer())
    {
        unstuck.update();
        unstucks = 0;
    }
    if (micspam)
    {
        if (micspam_on && micspam_on_timer.test_and_set(*micspam_on * 1000))
            g_IEngine->ClientCmd_Unrestricted("+voicerecord");
        if (micspam_off && micspam_off_timer.test_and_set(*micspam_off * 1000))
            g_IEngine->ClientCmd_Unrestricted("-voicerecord");
    }

    if (random_votekicks && timer_votekicks.test_and_set(5000))
        do_random_votekick();
    if (timer_abandon.test_and_set(2000) && level_init_timer.check(13000))
    {
        count_ipc = 0;
        ipc_list.clear();
        int count_total = 0;

        for (int i = 1; i <= g_IEngine->GetMaxClients(); ++i)
        {
            if (g_IEntityList->GetClientEntity(i))
                ++count_total;
            else
                continue;

            player_info_s info{};
            if (!GetPlayerInfo(i, &info))
                continue;
            if (playerlist::AccessData(info.friendsID).state == playerlist::k_EState::CAT)
                --count_total;

            if (playerlist::AccessData(info.friendsID).state == playerlist::k_EState::IPC || playerlist::AccessData(info.friendsID).state == playerlist::k_EState::TEXTMODE)
            {
                ipc_list.push_back(info.friendsID);
                ++count_ipc;
            }
        }
    }
}

void init()
{
    // g_IEventManager2->AddListener(&listener(), "player_death", false);
    g_IEventManager2->AddListener(&listener2(), "vote_maps_changed", false);
}

void level_init()
{
    level_init_timer.update();
}

void shutdown()
{
    // g_IEventManager2->RemoveListener(&listener());
    g_IEventManager2->RemoveListener(&listener2());
}

#if ENABLE_VISUALS
static void draw()
{
    if (!catbotmode || !anti_motd)
        return;
    if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer())
        return;
    AddCenterString(health, colors::green);
    AddCenterString(ammo, colors::yellow);
}
#endif

static InitRoutine runinit(
    []()
    {
        EC::Register(EC::CreateMove, cm, "cm_catbot", EC::average);
        EC::Register(EC::CreateMove, update, "cm2_catbot", EC::average);
        EC::Register(EC::LevelInit, level_init, "levelinit_catbot", EC::average);
        EC::Register(EC::Shutdown, shutdown, "shutdown_catbot", EC::average);
#if ENABLE_VISUALS
        EC::Register(EC::Draw, draw, "draw_catbot", EC::average);
#endif
        init();
    });
} // namespace hacks::catbot
