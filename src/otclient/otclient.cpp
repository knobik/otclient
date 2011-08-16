#include "otclient.h"

#include <framework/core/modulemanager.h>
#include <framework/core/configs.h>
#include <framework/core/resourcemanager.h>
#include <framework/core/eventdispatcher.h>
#include <framework/luascript/luainterface.h>
#include <framework/platform/platform.h>
#include <framework/graphics/graphics.h>
#include <framework/graphics/fontmanager.h>
#include <framework/ui/uimanager.h>
#include <framework/ui/uiwidget.h>
#include <framework/net/connection.h>

#include <otclient/net/protocolgame.h>
#include <otclient/core/game.h>
#include <otclient/core/map.h>

#define POLL_CYCLE_DELAY 10

OTClient g_client;

void OTClient::init(std::vector<std::string> args)
{
    m_running = false;
    m_stopping = false;

    // print client information
    logInfo("OTClient 0.2.0");

    // initialize platform related stuff
    g_platform.init(this, "OTClient");

    // initialize script environment
    g_lua.init();

    // register otclient lua functions
    registerLuaFunctions();

    // initialize resources
    g_resources.init(args[0].c_str());

    // load configurations
    loadConfigurations();

    // create the client window
    int minWidth = 550;
    int minHeight = 450;
    int windowX = fw::fromstring(g_configs.get("window x"), 0);
    int windowY = fw::fromstring(g_configs.get("window y"), 0);
    int windowWidth = fw::fromstring(g_configs.get("window width"), minWidth);
    int windowHeight = fw::fromstring(g_configs.get("window height"), minHeight);
    bool maximized = fw::fromstring(g_configs.get("window maximized"), false);
    g_platform.createWindow(windowX, windowY, windowWidth, windowHeight, minWidth, minHeight, maximized);
    g_platform.setWindowTitle("OTClient");

    // initialize graphics
    g_graphics.init();

    // initialize event dispatcher
    g_dispatcher.init();

    // initialize network
    //g_network.init();

    // initialize the ui
    g_ui.init();

    g_game.init();

    // discover and load modules
    g_modules.discoverAndLoadModules();

    // now that everything is initialized, setup configurations
    setupConfigurations();

    // now that everything is loaded, show the window
    g_platform.showWindow();
}

void OTClient::run()
{
    std::string fpsText;
    Size fpsTextSize;
    FontPtr defaultFont = g_fonts.getDefaultFont();
    int frameTicks = g_platform.getTicks();
    int lastFpsTicks = frameTicks;
    int lastPollTicks = frameTicks;
    int frameCount = 0;

    m_stopping = false;
    m_running = true;

    if(g_ui.getRootWidget()->getChildCount() == 0) {
        logError("ERROR: there is no root widgets to display, the app will close");
        m_stopping = true;
    }

    // run the first poll
    poll();

    while(!m_stopping) {
        g_platform.updateTicks();
        frameTicks = g_platform.getTicks();

        // calculate fps
        frameCount++;
        if(frameTicks - lastFpsTicks >= 1000) {
            fpsText = fw::mkstr("FPS: ", frameCount);
            fpsTextSize = defaultFont->calculateTextRectSize(fpsText);
            frameCount = 0;
            lastFpsTicks = frameTicks;
        }

        // poll events every POLL_CYCLE_DELAY
        // this delay exists to avoid massive polling thus increasing framerate
        if(frameTicks - lastPollTicks >= POLL_CYCLE_DELAY) {
            poll();
            lastPollTicks = frameTicks;
        }

        // only render when the windows is visible
        if(g_platform.isWindowVisible()) {
            // render begin
            g_graphics.beginRender();

            // render everything
            render();

            // render fps
            defaultFont->renderText(fpsText, Point(g_graphics.getScreenSize().width() - fpsTextSize.width() - 10, 10));

            // render end
            g_graphics.endRender();

            // swap the old buffer with the new rendered
            g_platform.swapBuffers();
        } else {
            // sleeps until next poll to avoid massive cpu usage
            g_platform.sleep(POLL_CYCLE_DELAY+1);
        }
    }

    m_stopping = false;
    m_running = false;
}

void OTClient::terminate()
{
    // hide the window because there is no render anymore
    g_platform.hideWindow();

    // run modules unload event
    g_modules.unloadModules();

    g_game.terminate();

    // terminate ui
    g_ui.terminate();

    // release remaining lua object references
    g_lua.collectGarbage();

    // poll remaining events
    poll();

    // terminate network
    Connection::terminate();

    // terminate dispatcher
    g_dispatcher.terminate();

    // terminate graphics
    g_graphics.terminate();

    // save configurations
    saveConfigurations();

    // release resources
    g_resources.terminate();

    // terminate script environment
    g_lua.terminate();

    // end platform related stuff
    g_platform.terminate();

}

void OTClient::exit()
{
    // stops the main loop
    m_stopping = true;
}

void OTClient::poll()
{
    // poll platform events
    g_platform.poll();

    // poll network events
    Connection::poll();

    // poll dispatcher events
    g_dispatcher.poll();
}

void OTClient::render()
{
    //TODO: UIMap for map drawing
    if(g_game.isOnline())
        g_map.draw(0, 0);

    // everything is rendered by UI components
    g_ui.render();
}

void OTClient::loadConfigurations()
{
    // default window size
    int defWidth = 550;
    int defHeight = 450;

    // sets default window configuration
    g_configs.set("window x", fw::tostring((g_platform.getDisplayWidth() - defWidth)/2));
    g_configs.set("window y", fw::tostring((g_platform.getDisplayHeight() - defHeight)/2));
    g_configs.set("window width", fw::tostring(defWidth));
    g_configs.set("window height", fw::tostring(defHeight));
    g_configs.set("window maximized", fw::tostring(false));
    g_configs.set("vsync", fw::tostring(true));

    // loads user configuration
    if(!g_configs.load("config.otml"))
        logInfo("Using default configurations.");
}

void OTClient::setupConfigurations()
{
    // activate vertical synchronization?
    bool vsync = fw::fromstring(g_configs.get("vsync"), true);
    g_platform.setVerticalSync(vsync);
}

void OTClient::saveConfigurations()
{
    g_configs.set("window x", fw::tostring(g_platform.getWindowX()));
    g_configs.set("window y", fw::tostring(g_platform.getWindowY()));
    g_configs.set("window width", fw::tostring(g_platform.getWindowWidth()));
    g_configs.set("window height", fw::tostring(g_platform.getWindowHeight()));
    g_configs.set("window maximized", fw::tostring(g_platform.isWindowMaximized()));

    // saves user configuration
    if(!g_configs.save())
        logError("ERROR: configurations are lost because it couldn't be saved");
}

void OTClient::onClose()
{
    if(m_onCloseCallback)
       m_onCloseCallback();
    else
        exit();
}

void OTClient::onResize(const Size& size)
{
    g_graphics.resize(size);
    g_ui.resize(size);
}

void OTClient::onPlatformEvent(const PlatformEvent& event)
{
    g_ui.inputEvent(event);

    ProtocolGamePtr protocol = g_game.getProtocolGame();
    if(protocol) {
        if(event.type == EventKeyDown) {
            if(event.keycode == KC_UP)
                protocol->sendWalkNorth();
            if(event.keycode == KC_RIGHT)
                protocol->sendWalkEast();
            if(event.keycode == KC_DOWN)
                protocol->sendWalkSouth();
            if(event.keycode == KC_LEFT)
                protocol->sendWalkWest();
        }
    }
}
