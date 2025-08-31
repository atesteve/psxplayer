#include "main-window.h"
#include "libupse/upse.h"
#include <QApplication>

int main(int argc, char* argv[])
{
    upse_module_init();

    QApplication a{argc, argv};
    MainWindow w{};
    w.show();
    a.exec();

    return 0;
}
