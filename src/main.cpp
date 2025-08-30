#include "main-window.h"
#include "libupse/upse.h"
#include <portaudio.h>
#include <QApplication>
#include <iostream>

int main(int argc, char* argv[])
{
    upse_module_init();

    auto err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    QApplication a{argc, argv};
    MainWindow w{};
    w.show();
    a.exec();

    Pa_Terminate();

    return 0;
}
