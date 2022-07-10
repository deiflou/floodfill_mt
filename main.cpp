#include <QApplication>
#include "window.h"

int main(int argc, char ** argv)
{
    QApplication app(argc, argv);

    window wnd;
    wnd.show();

    return app.exec();
}