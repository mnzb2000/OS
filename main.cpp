#include "mainwindow.h"
#include <QApplication>
#include <iostream>

int main(int argc, char *argv[])
{
    std::cout << "=== Input Device Monitor (Qt GUI) ===\n";
    std::cout << "Run with sudo if you get permission errors.\n";

    QApplication a(argc, argv);
    MainWindow w;
    w.show();

    return a.exec();
}
