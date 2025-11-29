#include "main-window.h"
#include "libupse/upse.h"

#include <QApplication>
#include <QtSystemDetection>

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_hints.h>

int main(int argc, char* argv[])
{
#ifdef Q_OS_LINUX
    // Prefer pulseaudio in Linux over pipewire, the pipewire driver seems to get stuck when
    // changing audio devices sometimes.
    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "pulseaudio");
#endif
    SDL_SetAppMetadata("PSXPlayer", "0.0.1", "psxplayer");
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    upse_module_init();

    {
        QApplication a{argc, argv};
        MainWindow w{};
        w.show();
        a.exec();
    }

    SDL_QuitSubSystem(SDL_INIT_AUDIO);

    return 0;
}
