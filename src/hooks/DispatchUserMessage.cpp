/*
  Created by Jenny White on 29.04.18.
  Copyright (c) 2018 nullworks. All rights reserved.
*/

#include <settings/Bool.hpp>
#include "HookedMethods.hpp"
#include "MiscTemporary.hpp"
#include "votelogger.hpp"
#include "nospread.hpp"

static settings::Boolean dispatch_log{ "debug.log-dispatch-user-msg", "false" };
static settings::Boolean anti_autobalance{ "cat-bot.anti-autobalance", "false" };
static settings::Boolean anti_autobalance_safety{ "cat-bot.anti-autobalance.safety", "true" };

static bool retrun = false;
static Timer gitgud{};

std::string lastfilter{};
std::string lastname{};

namespace hooked_methods
{
static std::string previous_name = "";
static Timer reset_it{};
static Timer wait_timer{};
void Paint()
{
    if (!wait_timer.test_and_set(1000))
        return;
    INetChannel *server = (INetChannel *) g_IEngine->GetNetChannelInfo();
    if (server)
        reset_it.update();
    if (reset_it.test_and_set(20000))
    {
        anti_balance_attempts = 0;
        previous_name         = "";
    }
}


static InitRoutine Autobalance([]() {EC::Register(EC::Paint, Paint, "paint_autobalance", EC::average);});

DEFINE_HOOKED_METHOD(DispatchUserMessage, bool, void *this_, int type, bf_read &buf)
{
    if (!isHackActive())
        return original::DispatchUserMessage(this_, type, buf);

    int s, i;
    char c;
    const char *buf_data = reinterpret_cast<const char *>(buf.m_pData);
    // We should bail out
    if (!hacks::nospread::DispatchUserMessage(&buf, type))
        return true;
    std::string data;
    switch (type)
    {

    case 5:
    {
        if (*anti_autobalance && buf.GetNumBytesLeft() > 35)
        {
            INetChannel *server = (INetChannel *) g_IEngine->GetNetChannelInfo();
            data                = std::string(buf_data);
            logging::Info("%s", data.c_str());
            if (data.find("TeamChangeP") != data.npos && CE_GOOD(LOCAL_E))
            {
                std::string server_name(server->GetAddress());
                if (server_name != previous_name)
                {
                    previous_name         = server_name;
                    anti_balance_attempts = 0;
                }
                if (!anti_autobalance_safety || anti_balance_attempts < 2)
                {
                    logging::Info("Rejoin: anti autobalance");
                    ignoredc = true;
                    g_IEngine->ClientCmd_Unrestricted("killserver;wait 100;cat_mm_join");
                }
                else
                    re::CTFPartyClient::GTFPartyClient()->SendPartyChat("Will be autobalanced in 3 seconds");
                anti_balance_attempts++;
            }
            }
        }
        break;
    }
    votelogger::dispatchUserMessage(buf, type);
    return original::DispatchUserMessage(this_, type, buf);
}
} // namespace hooked_methods
