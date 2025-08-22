#include "main-window.h"
#include "libupse/upse.h"
#include <fmt/format.h>
#include <QApplication>

int main(int argc, char* argv[])
{
    upse_module_init();

    // {
    //     using namespace std::literals;
    //     UpseModule mod{
    //         "/home/aesteve/Emulation/PSX music/Final Fantasy X (EMU).zophar/"
    //         "102 In Zanarkand.minipsf2"};
    // }

    QApplication a{argc, argv};
    MainWindow w{};
    w.show();
    a.exec();

    return 0;
}
