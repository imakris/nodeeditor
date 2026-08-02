#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/ConnectionStyle>
#include <QtNodes/GraphicsView>
#include <QtNodes/StyleCollection>

#include <QScreen>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QVBoxLayout>

#include "DynamicPortsActions.hpp"
#include "DynamicPortsModel.hpp"

using QtNodes::BasicGraphicsScene;
using QtNodes::ConnectionStyle;
using QtNodes::GraphicsView;
using QtNodes::NodeRole;
using QtNodes::StyleCollection;

void initializeModel(DynamicPortsModel &graphModel)
{
    NodeId id1 = graphModel.addNode();
    graphModel.setNodeData(id1, NodeRole::Position, QPointF(0, 0));
    graphModel.setNodeData(id1, NodeRole::InPortCount, 1);
    graphModel.setNodeData(id1, NodeRole::OutPortCount, 1);

    NodeId id2 = graphModel.addNode();
    graphModel.setNodeData(id2, NodeRole::Position, QPointF(300, 300));

    graphModel.setNodeData(id2, NodeRole::InPortCount, 1);
    graphModel.setNodeData(id2, NodeRole::OutPortCount, 1);

    graphModel.addConnection(ConnectionId{id1, 0, id2, 0});
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    DynamicPortsModel graphModel;

    // Initialize and connect two nodes.
    initializeModel(graphModel);

    // Main app window holding menu and a scene view.
    QWidget window;
    window.setWindowTitle("Dynamic Nodes Example");
    window.resize(800, 600);

    auto scene = new BasicGraphicsScene(graphModel);

    qWarning() << "MODEF FROM SCENE " << &(scene->graphModel());

    GraphicsView view(scene);
    // Setup context menu for creating new nodes.
    view.setContextMenuPolicy(Qt::ActionsContextMenu);
    view.insertAction(view.actions().front(), create_node_action(graphModel, view));

    // Pack all elements into layout.
    QVBoxLayout *l = new QVBoxLayout(&window);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(0);
    l->addWidget(create_save_restore_menu(graphModel, *scene, view));
    l->addWidget(&view);

    // Center window
    window.move(QApplication::primaryScreen()->availableGeometry().center() - view.rect().center());
    window.showNormal();

    return app.exec();
}
