#include "DynamicPortsActions.hpp"

#include "DynamicPortsModel.hpp"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/GraphicsView>

#include <QAction>
#include <QCursor>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonDocument>
#include <QMenu>
#include <QMenuBar>
#include <QUndoStack>

bool load_graph_from_file(QString const &file_name,
                          DynamicPortsModel &graph_model,
                          QtNodes::BasicGraphicsScene &scene) noexcept
{
    try {
        QFile file(file_name);
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }

        qint64 const serialized_size = file.size();
        qint64 constexpr maximum_size = DynamicPortsModel::s_max_serialized_bytes;
        if (serialized_size < 0 || serialized_size > maximum_size) {
            return false;
        }

        QByteArray const serialized = file.read(maximum_size + 1);
        if (file.error() != QFileDevice::NoError || serialized.size() != serialized_size
            || !file.atEnd() || !graph_model.load_from_json(serialized)) {
            return false;
        }

        scene.undoStack().clear();
        scene.undoStack().setClean();
        return true;
    } catch (...) {
        return false;
    }
}

QMenuBar *create_save_restore_menu(DynamicPortsModel &graph_model,
                                   QtNodes::BasicGraphicsScene &scene,
                                   QtNodes::GraphicsView &view)
{
    auto menu_bar = new QMenuBar();
    QMenu *menu = menu_bar->addMenu("File");
    QAction *save_action = menu->addAction("Save Scene");
    QAction *load_action = menu->addAction("Load Scene");

    QObject::connect(save_action, &QAction::triggered, &scene, [&graph_model] {
        QString file_name = QFileDialog::getSaveFileName(nullptr,
                                                         "Save Flow Scene",
                                                         QDir::homePath(),
                                                         "Flow Scene Files (*.flow)");

        if (!file_name.isEmpty()) {
            if (!file_name.endsWith("flow", Qt::CaseInsensitive)) {
                file_name += ".flow";
            }

            QFile file(file_name);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(QJsonDocument(graph_model.save()).toJson());
            }
        }
    });

    QObject::connect(load_action, &QAction::triggered, &scene, [&graph_model, &scene, &view] {
        QString const file_name = QFileDialog::getOpenFileName(nullptr,
                                                               "Open Flow Scene",
                                                               QDir::homePath(),
                                                               "Flow Scene Files (*.flow)");
        if (file_name.isEmpty()) {
            return;
        }

        if (!load_graph_from_file(file_name, graph_model, scene)) {
            qWarning() << "Could not load dynamic-ports graph";
            return;
        }

        view.centerScene();
    });

    return menu_bar;
}

QAction *create_node_action(DynamicPortsModel &graph_model, QtNodes::GraphicsView &view)
{
    auto action = new QAction(QStringLiteral("Create Node"), &view);
    QObject::connect(action, &QAction::triggered, [&graph_model, &view] {
        QPointF const position = view.mapToScene(view.mapFromGlobal(QCursor::pos()));
        if (!graph_model.try_add_node(position)) {
            qWarning() << "Could not create dynamic-ports node: capacity exhausted";
        }
    });
    return action;
}
