#include <QtWidgets/QApplication>
#include <QDirIterator>
#include <QResource>
#include <QDialog>
#include <QMainWindow>
#include <thread>
#include <chrono>
#include <iostream>
#include <random>
#include "spread_sheet.h"
using namespace std::chrono_literals;

tool::SpreadSheet* ss = NULL;
bool need_stop = false;
const int COLUMN = 102400;

void Producer()
{
    while (!need_stop)
    {
        std::random_device rd;  //Will be used to obtain a seed for the random number engine
        std::mt19937 gen(rd()); //Standard mersenne_twister_engine seeded with rd()
        std::uniform_int_distribution<> distrib(1, 102400);

        tool::DatasPtr datas(new tool::Datas(COLUMN));
        for (int i = 0; i < datas->size(); i++)
        {
            tool::DataStruct& data = datas->at(i);
            data.idx = i;
            data.v1 = distrib(gen);
            data.v2 = distrib(gen);
            data.v3 = distrib(gen);
        }
        if (ss)
            ss->Update(datas);
        std::this_thread::sleep_for(200ms);
    }
}

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

    QMainWindow* w = new QMainWindow(NULL);
    ss = new tool::SpreadSheet(COLUMN, 4, w);
    w->setCentralWidget(ss);
    w->show();

    std::shared_ptr<std::thread> th(new std::thread(Producer));
    return a.exec();
}
