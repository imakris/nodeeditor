#include "StyleCollection.hpp"

#include <QtCore/QCoreApplication>
#include <QtCore/QThread>

using QtNodes::ConnectionStyle;
using QtNodes::GraphicsViewStyle;
using QtNodes::NodeStyle;
using QtNodes::StyleCollection;

namespace {

/**
 * Every paint and every hit test reads the defaults, and the setters overwrite
 * them in place, so a write from anywhere but the GUI thread tears a style out
 * from under a reader. There is no application object to compare against before
 * QApplication is constructed, and installing the defaults at that point is a
 * legitimate thing for a consumer to do.
 */
void assert_gui_thread()
{
    QCoreApplication const *const app = QCoreApplication::instance();

    Q_ASSERT(app == nullptr || app->thread() == QThread::currentThread());
    Q_UNUSED(app);
}

} // namespace

NodeStyle const &StyleCollection::nodeStyle()
{
    return instance()._nodeStyle;
}

ConnectionStyle const &StyleCollection::connectionStyle()
{
    return instance()._connectionStyle;
}

GraphicsViewStyle const &StyleCollection::flowViewStyle()
{
    return instance()._flowViewStyle;
}

void StyleCollection::setNodeStyle(NodeStyle nodeStyle)
{
    assert_gui_thread();

    instance()._nodeStyle = nodeStyle;
}

void StyleCollection::setConnectionStyle(ConnectionStyle connectionStyle)
{
    assert_gui_thread();

    instance()._connectionStyle = connectionStyle;
}

void StyleCollection::setGraphicsViewStyle(GraphicsViewStyle flowViewStyle)
{
    assert_gui_thread();

    instance()._flowViewStyle = flowViewStyle;
}

StyleCollection &StyleCollection::instance()
{
    static StyleCollection collection;

    return collection;
}
