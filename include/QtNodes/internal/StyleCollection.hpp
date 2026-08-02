#pragma once

#include "Export.hpp"

#include "ConnectionStyle.hpp"
#include "GraphicsViewStyle.hpp"
#include "NodeStyle.hpp"

namespace QtNodes {

/**
 * Holds the process-wide default styles that every scene, view, geometry and
 * painter falls back to.
 *
 * Thread affinity: these are GUI-thread state. The getters hand out references
 * into the singleton and the setters overwrite it in place, so a concurrent
 * reader would observe a torn style. Install the defaults from the GUI thread,
 * normally during application start-up, before or between paints. The setters
 * assert that affinity once an application object exists.
 */
class NODE_EDITOR_PUBLIC StyleCollection
{
public:
    static NodeStyle const &nodeStyle();

    static ConnectionStyle const &connectionStyle();

    static GraphicsViewStyle const &flowViewStyle();

public:
    static void setNodeStyle(NodeStyle);

    static void setConnectionStyle(ConnectionStyle);

    static void setGraphicsViewStyle(GraphicsViewStyle);

private:
    StyleCollection() = default;

    StyleCollection(StyleCollection const &) = delete;

    StyleCollection &operator=(StyleCollection const &) = delete;

    static StyleCollection &instance();

private:
    NodeStyle _nodeStyle;

    ConnectionStyle _connectionStyle;

    GraphicsViewStyle _flowViewStyle;
};
} // namespace QtNodes
