#include <QApplication>
#include <QCoreApplication>
#include <QStyleFactory>

#include "mainwindow.h"

int main(int argc, char* argv[]) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    app.setApplicationName("BLP 查看器");
    app.setOrganizationName("blp-lib");

    MainWindow window;
    window.show();

    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) {
        window.openPaths(args.mid(1));
    }
    return app.exec();
}
