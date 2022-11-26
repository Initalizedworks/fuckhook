
/*
 * drawmgr.cpp
 *
 *  Created on: May 22, 2017
 *      Author: nullifiedcat
 */

#include <MiscTemporary.hpp>
#include <hacks/Aimbot.hpp>
#include <hacks/hacklist.hpp>
#if ENABLE_IMGUI_DRAWING
#include "imgui/imrenderer.hpp"
#endif
#include <settings/Bool.hpp>
#include <settings/Float.hpp>
#include <menu/GuiInterface.hpp>
#include "common.hpp"
#include "visual/drawing.hpp"
#include "hack.hpp"
#include "menu/menu/Menu.hpp"
#include "drawmgr.hpp"

static settings::Boolean info_text{"hack-info.enable", "true"};
static settings::Int info_x{"hack-info.x", "10"};
static settings::Int info_y{"hack-info.y", "10"};

static settings::Float info_alpha{"hack-info.alpha", "0.7"};

void render_cheat_visuals()
{
    {
        PROF_SECTION(BeginCheatVisuals);
        BeginCheatVisuals();
    }
    {
        PROF_SECTION(DrawCheatVisuals);
        DrawCheatVisuals();
    }
    {
        PROF_SECTION(EndCheatVisuals);
        EndCheatVisuals();
    }
}

int currentBuffer = 0;

void BeginCheatVisuals()
{
#if ENABLE_IMGUI_DRAWING
    im_renderer::bufferBegin();
#endif
    ResetStrings();
}

std::mutex drawing_mutex;

double getRandom(double lower_bound, double upper_bound)
{
    std::uniform_real_distribution<double> unif(lower_bound, upper_bound);
    static std::mt19937 rand_engine(std::time(nullptr));

    double x = unif(rand_engine);
    return x;
}

void DrawCheatVisuals()
{
    {
        PROF_SECTION(DRAW_info);
        std::string name_s, reason_s;
        if (info_text && draw::inited)
        {
            // Setup time
            char timeString[10];
            time_t current_time;
            struct tm *time_info;

            time(&current_time);
            time_info = localtime(&current_time);
            strftime(timeString, sizeof(timeString), "%H:%M:%S", time_info);

            // Server info (if applicable)
            std::string server_info;
            auto netchannel = g_IEngine->GetNetChannelInfo();
            if (netchannel)
            {
                // Get ping same way as net_graph
                float avgLatency = netchannel->GetAvgLatency(FLOW_OUTGOING);
                float adjust = 0.0f;

                static const ConVar *pUpdateRate = g_pCVar->FindVar("cl_updaterate");
                if (!pUpdateRate)
                    pUpdateRate = g_pCVar->FindVar("cl_updaterate");
                else
                {
                    if (pUpdateRate->GetFloat() > 0.001f)
                    {
                        adjust = -0.5f / pUpdateRate->GetFloat();
                        avgLatency += adjust;
                    }
                }
                // Can't be below zero
                avgLatency = MAX( 0.0, avgLatency );

                server_info = " | " + std::to_string((int)(avgLatency*1000.0f)) + " ms";
            }

            std::string result = std::string(format_cstr("Fuckhook | %s%s", timeString, server_info.c_str()).get());

            // Sizes for rectangle and line
            float w, h;
            fonts::center_screen->stringSize(result, &w, &h);

            // Draw!
            draw::Rectangle(*info_x - 5, *info_y - 5, w + 10, h + 10, colors::Transparent(colors::black, *info_alpha));
            draw::Line(*info_x - 5, *info_y - 5, w + 10, 0, colors::gui, 2.0f);
            draw::String(*info_x, *info_y, colors::gui, result.c_str(), *fonts::center_screen);
        }
    }
    if (spectator_target)
    {
        AddCenterString("Press SPACE to stop spectating");
    }
    {
        PROF_SECTION(DRAW_WRAPPER);
        EC::run(EC::Draw);
    }
    if (CE_GOOD(g_pLocalPlayer->entity) && !g_Settings.bInvalid)
    {
        PROF_SECTION(DRAW_skinchanger);
        hacks::skinchanger::DrawText();
        Prediction_PaintTraverse();
    }
    {
        PROF_SECTION(DRAW_strings);
        DrawStrings();
    }
#if ENABLE_GUI
    {
        PROF_SECTION(DRAW_GUI);
        gui::draw();
    }
#endif
}

void EndCheatVisuals()
{
#if !ENABLE_ENGINE_DRAWING && !ENABLE_IMGUI_DRAWING
    buffers[currentBuffer]->end();
#endif
#if !ENABLE_ENGINE_DRAWING || ENABLE_IMGUI_DRAWING
    currentBuffer = !currentBuffer;
#endif
}

void DrawCache()
{
#if !ENABLE_ENGINE_DRAWING && !ENABLE_IMGUI_DRAWING
    buffers[!currentBuffer]->replay();
#endif
}
