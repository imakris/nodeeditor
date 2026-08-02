#pragma once

#include <QtCore/QString>

class QAction;
class QMenuBar;

namespace QtNodes {
class BasicGraphicsScene;
class GraphicsView;
} // namespace QtNodes

class DynamicPortsModel;

bool load_graph_from_file(QString const &file_name,
                          DynamicPortsModel &graph_model,
                          QtNodes::BasicGraphicsScene &scene) noexcept;

QMenuBar *create_save_restore_menu(DynamicPortsModel &graph_model,
                                   QtNodes::BasicGraphicsScene &scene,
                                   QtNodes::GraphicsView &view);

QAction *create_node_action(DynamicPortsModel &graph_model, QtNodes::GraphicsView &view);
