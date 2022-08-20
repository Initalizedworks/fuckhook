/*
 * votelogger.cpp
 *
 *  Created on: Dec 31, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include "votelogger.hpp"
#include "PlayerTools.hpp"

static settings::Int vote_wait_min{ "votelogger.autovote.wait.min", "10" };
static settings::Int vote_wait_max{ "votelogger.autovote.wait.max", "40" };
static settings::Boolean vote_wait{ "votelogger.autovote.wait", "false" };
static settings::Boolean vote_kicky{ "votelogger.autovote.yes", "false" };
static settings::Boolean vote_kickn{ "votelogger.autovote.no", "false" };
static settings::Boolean vote_rage_vote{ "votelogger.autovote.no.rage", "false" };
static settings::Boolean chat{ "votelogger.chat", "true" };
static settings::Boolean chat_partysay{ "votelogger.chat.partysay", "false" };
static settings::Boolean chat_partysay_result{ "votelogger.chat.partysay.result", "false" };
static settings::Boolean chat_casts{ "votelogger.chat.casts", "false" };
static settings::Boolean chat_partysay_casts{ "votelogger.chat.partysay.casts", "false" };
static settings::Boolean chat_casts_f1_only{ "votelogger.chat.casts.f1-only", "false" };

namespace votelogger
{

static bool was_local_player{ false };
static bool was_local_player_caller{ false };
static int F1count = 0;
static int F2count = 0;

void Reset()
{
    was_local_player        = false;
    was_local_player_caller = false;
    F1count                 = 0;
    F2count                 = 0;
}

static void vote_rage_back()
{
    static Timer attempt_vote_time;
    char cmd[40];
    player_info_s info{};
    std::vector<int> targets;

    if (!g_IEngine->IsInGame() || !attempt_vote_time.test_and_set(1000))
        return;

    for (int i = 1; i <= g_IEngine->GetMaxClients(); i++)
    {
        auto ent = ENTITY(i);
        // TO DO: m_bEnemy check only when you can't vote off players from the opposite team
        if (CE_BAD(ent) || ent == LOCAL_E || ent->m_Type() != ENTITY_PLAYER || ent->m_bEnemy())
            continue;

        if (!g_IEngine->GetPlayerInfo(ent->m_IDX, &info))
            continue;

        auto &pl = playerlist::AccessData(info.friendsID);
        if (pl.state == playerlist::k_EState::RAGE && pl.state == playerlist::k_EState::PAZER)
            targets.emplace_back(info.userID);
    }
    if (targets.empty())
        return;

    std::snprintf(cmd, sizeof(cmd), "callvote kick \"%d scamming\"", targets[UniformRandomInt(0, targets.size() - 1)]);
    g_IEngine->ClientCmd_Unrestricted(cmd);
}

void dispatchUserMessage(bf_read &buffer, int type)
{
    static player_info_s kicked_info;
    player_info_s caller_info{};
    int caller, eid;
    int vote_id;
    char reason[64], formated_string[256];

    switch (type)
    {
    case 45:
        // Call Vote Failed
        break;
    case 46:
    {
        Reset();
        /* Team where vote occured */
        buffer.ReadByte();
        /* Caller player index */
        caller = buffer.ReadByte();
        /* Vote reason */
        buffer.ReadString(reason, 64, false, nullptr);
        /* Name of kicked player */
        buffer.ReadString(formated_string, 64, false, nullptr);
        /* Kicked player index */
        eid = (buffer.ReadByte() & 0xFF) >> 1;
        /* Restore buffer positions */
        buffer.Seek(0);
        /* voteid WTF */
        vote_id = buffer.ReadLong();

        if (!g_IEngine->GetPlayerInfo(eid, &kicked_info) || !g_IEngine->GetPlayerInfo(caller, &caller_info))
            break;

        logging::Info("Vote called to kick %s [U:1:%u] for %s by %s [U:1:%u]", kicked_info.name, kicked_info.friendsID, reason, caller_info.name, caller_info.friendsID);

        std::string reason_short;
        std::string reason_s = std::string(reason);
        if (reason_s.find("#TF_vote_kick_player_cheating") != std::string::npos)
            reason_short = "Cheating";
        else if (reason_s.find("#TF_vote_kick_player_scamming") != std::string::npos)
            reason_short = "Scamming";
        else if (reason_s.find("#TF_vote_kick_player_idle") != std::string::npos)
            reason_short = "Idle";
        else if (reason_s.find("#TF_vote_kick_player_other") != std::string::npos)
            reason_short = "Other";
        else
            reason_short = reason_s;

        if (CE_GOOD(LOCAL_E) && eid == LOCAL_E->m_IDX)
            was_local_player = true;
        if (CE_GOOD(LOCAL_E) && caller == LOCAL_E->m_IDX)
            was_local_player_caller = true;

        std::string vote_option_reason;
        if (*vote_kickn || *vote_kicky)
        {
            using namespace playerlist;

            auto &pl             = AccessData(kicked_info.friendsID);
            auto &pl_caller      = AccessData(caller_info.friendsID);
            bool friendly_kicked = !player_tools::shouldTargetSteamId(kicked_info.friendsID);
            bool friendly_caller = !player_tools::shouldTargetSteamId(caller_info.friendsID);

            int vote_option   = -1;
            std::string state = "DEFAULT";

            // Determine string
            switch (pl.state)
            {
            case k_EState::IPC:
                state = "IPC";
                break;
            case k_EState::PARTY:
                state = "PARTY";
                break;
            case k_EState::FRIEND:
                state = "FRIEND";
                break;
            case k_EState::PRIVATE:
                state = "PRIVATE";
                break;
            case k_EState::CAT:
                state = "CAT";
                break;
            case k_EState::PAZER:
            case k_EState::RAGE:
                state = "RAGE";
                break;
            }

            if (*vote_kickn && friendly_kicked)
            {
                vote_option = 2;
                if (*vote_rage_vote && !friendly_caller)
                {
                    pl_caller.state = k_EState::RAGE;
                }

            }
            else if (*vote_kicky && !friendly_kicked)
            {
                vote_option = 1;
            }
            if (vote_option != -1)
            {
                if (*vote_wait)
                    std::snprintf(formated_string, sizeof(formated_string), strfmt("wait %d;vote %d option%d", UniformRandomInt(*vote_wait_min, *vote_wait_max), vote_id).get(), vote_option);
                else
                    std::snprintf(formated_string, sizeof(formated_string), strfmt("vote %d option%d", vote_id).get(), vote_option);

                g_IEngine->ClientCmd_Unrestricted(formated_string);
            }
        }
        if (*chat_partysay)
        {
            std::snprintf(formated_string, sizeof(formated_string), "Kick called: %s [U:1:%u] -> %s [U:1:%u]", caller_info.name, caller_info.friendsID, kicked_info.name, kicked_info.friendsID);
            re::CTFPartyClient::GTFPartyClient()->SendPartyChat(formated_string);
        }
        break;
    }
    case 47:
        logging::Info("Vote passed on %s [U:1:%u] with %i F1s and %i F2s.", kicked_info.name, kicked_info.friendsID, F1count + 1, F2count + 1);
        if (*chat_partysay_result)
        {
            std::snprintf(formated_string, sizeof(formated_string), "Vote passed on %s [U:1:%u] with %i F1s and %i F2s.", kicked_info.name, kicked_info.friendsID, F1count + 1, F2count + 1);
            re::CTFPartyClient::GTFPartyClient()->SendPartyChat(formated_string);
        }
        Reset();
        break;
    case 48:
        logging::Info("Vote failed on %s [U:1:%u] with %i F1s and %i F2s.", kicked_info.name, kicked_info.friendsID, F1count + 1, F2count + 1);
        if (*chat_partysay_result)
        {
            std::snprintf(formated_string, sizeof(formated_string), "Vote failed on %s [U:1:%u] with %i F1s and %i F2s.", kicked_info.name, kicked_info.friendsID, F1count + 1, F2count + 1);
            re::CTFPartyClient::GTFPartyClient()->SendPartyChat(formated_string);
        }
        Reset();
        break;
    case 49:
        logging::Info("VoteSetup?");
        break;
    default:
        break;
    }
}

static void setup_vote_rage()
{
    EC::Register(EC::CreateMove, vote_rage_back, "vote_rage_back");
}

static void reset_vote_rage()
{
    EC::Unregister(EC::CreateMove, "vote_rage_back");
}

class VoteEventListener : public IGameEventListener
{
public:
    void FireGameEvent(KeyValues *event) override
    {
        if (!chat_casts && !chat_partysay_casts && !chat_partysay_result)
            return;
        const char *name = event->GetName();
        if (!strcmp(name, "vote_cast"))
        {
            bool vote_option = event->GetInt("vote_option");
            if (vote_option)
                F2count++;
            if (!vote_option)
                F1count++;
            int eid = event->GetInt("entityid");
            player_info_s info{};
            if (!g_IEngine->GetPlayerInfo(eid, &info))
                return;
            if (chat_partysay && chat_partysay_casts && !(*chat_casts_f1_only && vote_option))
            {
                char formated_string[256];
                std::snprintf(formated_string, sizeof(formated_string), "%s [U:1:%u] voted %s", info.name, info.friendsID, vote_option ? "No" : "Yes");
                re::CTFPartyClient::GTFPartyClient()->SendPartyChat(formated_string);
            }
#if ENABLE_VISUALS
            if (chat && chat_casts && !(*chat_casts_f1_only && vote_option))
                PrintChat("\x07%06X%s\x01 [U:1:%u] voted %s", colors::chat::team(g_pPlayerResource->getTeam(eid)), info.name, info.friendsID, vote_option ? "No" : "Yes");
#endif
        }
    }
};

static VoteEventListener listener{};
static InitRoutine init(
    []()
    {
        if (*vote_rage_vote)
            setup_vote_rage();
        vote_rage_vote.installChangeCallback(
            [](settings::VariableBase<bool> &var, bool new_val)
            {
                if (new_val)
                    setup_vote_rage();
                else
                    reset_vote_rage();
            });
        g_IGameEventManager->AddListener(&listener, false);
        EC::Register(
            EC::Shutdown, []() { g_IGameEventManager->RemoveListener(&listener); }, "event_shutdown_vote");
    });
} // namespace votelogger
