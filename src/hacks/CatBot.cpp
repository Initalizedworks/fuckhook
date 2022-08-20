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
#include "MiscTemporary.hpp"
#include "e8call.hpp"
#include "navparser.hpp"
#include "SettingCommands.hpp"
#include "glob.h"
#include <unordered_set>

namespace hacks::catbot
{

static settings::Boolean micspam{ "cat-bot.micspam.enable", "false" };
static settings::Int micspam_on{ "cat-bot.micspam.interval-on", "3" };
static settings::Int micspam_off{ "cat-bot.micspam.interval-off", "60" };

static settings::Boolean random_votekicks{ "cat-bot.votekicks", "false" };
static settings::Int requeue_if_humans_lte{ "cat-bot.requeue-if.humans-lte", "0" };
static settings::Boolean autovote_map{ "cat-bot.autovote-map", "true" };

/* votekicks start */

static settings::Boolean enabled{ "votekicks.enabled", "false" };
/* 0 - Smart, 1 - Random, 2 - Sequential */
static settings::Int mode{ "votekicks.mode", "0" };
/* static settings::Int reason{ "votekicks.reason", "0" }; */
/* Time between calling a vote in milliseconds */
static settings::Int timer{ "votekicks.timer", "1000" };
/* Minimum amount of team members to start a vote */
static settings::Int min_team_size{ "votekicks.min-team-size", "4" };
/* Only kick rage or pazer playerlist states */
static settings::Boolean rage_only{ "votekicks.rage-only", "false" };

/* Priority settings */
static settings::Boolean prioritize_rage{ "votekicks.prioritize.rage", "true" };
static settings::Boolean prioritize_previously_kicked{ "votekicks.prioritize.previous", "true" };
/* If highest score target >= this rvar (& gt 0), make them highest priority to kick */
static settings::Int prioritize_highest_score{ "votekicks.prioritize.highest-score", "0" };

std::unordered_set<uint32> previously_kicked;

/*const std::string votekickreason[] = { "", "cheating", "scamming", "idle" }; */

static int GetKickScore(int uid)
{
    player_info_s i{};
    int idx = g_IEngine->GetPlayerForUserID(uid);
    if (!g_IEngine->GetPlayerInfo(idx, &i))
        return 0;

    uid = 0;
    if (prioritize_previously_kicked && previously_kicked.find(i.friendsID) != previously_kicked.end())
        uid += 500;
    if (prioritize_rage)
    {
        auto &pl = playerlist::AccessData(i.friendsID);
        if (pl.state == playerlist::k_EState::RAGE || pl.state == playerlist::k_EState::PAZER)
            uid += 1000;
    }

    uid += g_pPlayerResource->GetScore(idx);
    return uid;
}

static void CreateMove()
{
    static Timer votekicks_timer;
    if (!votekicks_timer.test_and_set(*timer))
        return;

    player_info_s local_info{};
    std::vector<int> targets;
    std::vector<int> scores;
    int teamSize = 0;

    if (CE_BAD(LOCAL_E) || !g_IEngine->GetPlayerInfo(LOCAL_E->m_IDX, &local_info))
        return;
    for (int i = 1; i < g_GlobalVars->maxClients; ++i)
    {
        player_info_s info{};
        if (!g_IEngine->GetPlayerInfo(i, &info) || !info.friendsID)
            continue;
        if (g_pPlayerResource->GetTeam(i) != g_pLocalPlayer->team)
            continue;
        teamSize++;

        if (info.friendsID == local_info.friendsID)
            continue;
        if (!player_tools::shouldTargetSteamId(info.friendsID))
            continue;
        auto &pl = playerlist::AccessData(info.friendsID);
        if (rage_only && (pl.state != playerlist::k_EState::RAGE && pl.state != playerlist::k_EState::PAZER))
            continue;

        targets.push_back(info.userID);
        scores.push_back(g_pPlayerResource->GetScore(g_IEngine->GetPlayerForUserID(info.userID)));
    }
    if (targets.empty() || scores.empty() || teamSize <= *min_team_size)
        return;

    int target;
    auto score_iterator = std::max_element(scores.begin(), scores.end());

    switch (*mode)
    {
    case 0:
        // Smart mode - Sort by kick score
        std::sort(targets.begin(), targets.end(), [](int a, int b) { return GetKickScore(a) > GetKickScore(b); });
        target = (*prioritize_highest_score && *score_iterator >= *prioritize_highest_score) ? targets[std::distance(scores.begin(), score_iterator)] : targets[0];
        break;
    case 1:
        // Random
        target = targets[UniformRandomInt(0, targets.size() - 1)];
        break;
    case 2:
        // Sequential
        target = targets[0];
        break;
    }

    player_info_s info{};
    if (!g_IEngine->GetPlayerInfo(g_IEngine->GetPlayerForUserID(target), &info))
        return;
    hack::ExecuteCommand(/*format(*/"callvote kick \"" + std::to_string(target) + " cheating\""/*, votekickreason[int(reason)]).c_str()*/);
}

/* votekicks end */

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


CatCommand print_ammo("debug_print_ammo", "debug", []() {
    if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer() || CE_BAD(LOCAL_W))
        return;
    logging::Info("Current slot: %d", re::C_BaseCombatWeapon::GetSlot(RAW_ENT(LOCAL_W)));
    for (int i = 0; i < 10; i++)
    logging::Info("Ammo Table %d: %d", i, CE_INT(LOCAL_E, netvar.m_iAmmo + i * 4));
    });

static CatCommand debugKickScore("debug_kickscore", "Prints kick score for each player", []() {
    player_info_s info{};
    if (!g_IEngine->IsInGame())
        return;
    for (int i = 1; i < g_GlobalVars->maxClients; ++i)
    {
        if (!g_IEngine->GetPlayerInfo(i, &info) || !info.friendsID)
            continue;
        logging::Info("%d %u %s: %d", i, info.friendsID, info.name, GetKickScore(info.userID));
    }
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

            if (playerlist::AccessData(info.friendsID).state == playerlist::k_EState::IPC)
            {
                ipc_list.push_back(info.friendsID);
                ++count_ipc;
            }
            /* Check this so we don't spam logs */
            re::CTFGCClientSystem *gc = re::CTFGCClientSystem::GTFGCClientSystem();
            re::CTFPartyClient *pc    = re::CTFPartyClient::GTFPartyClient();
            if (requeue_if_humans_lte && gc && gc->BConnectedToMatchServer(true) && gc->BHaveLiveMatch())
            {
                if (pc && !(pc->BInQueueForMatchGroup(tfmm::getQueue()) || pc->BInQueueForStandby()))
                {
                    if (count_total - count_bot <= int(requeue_if_humans_lte))
                    {
                        tfmm::startQueue();
                        logging::Info("Requeuing because there are %d non-bots in "
                                        "game, and requeue_if_humans_lte is %d.",
                                        count_total - count_bot, int(requeue_if_humans_lte));
                        return;
                    }
                }
            }
        }
    }
}

static void register_votekicks(bool enable)
{
    if (enable)
        EC::Register(EC::CreateMove, CreateMove, "cm_votekicks");
    else
        EC::Unregister(EC::CreateMove, "cm_votekicks");
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

static InitRoutine init([]() {
    enabled.installChangeCallback([](settings::VariableBase<bool> &var, bool new_val) { register_votekicks(new_val); });
    if (*enabled)
        register_votekicks(true);
});

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
