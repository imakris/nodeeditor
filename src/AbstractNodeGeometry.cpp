#include "AbstractNodeGeometry.hpp"

#include "AbstractGraphModel.hpp"
#include "ConnectionIdUtils.hpp"
#include "NodeRenderingUtils.hpp"

#include <cmath>
#include <optional>

namespace QtNodes {

AbstractNodeGeometry::AbstractNodeGeometry(AbstractGraphModel &graphModel)
    : _graphModel(graphModel)
{
    //
}

QPointF AbstractNodeGeometry::portScenePosition(NodeId const nodeId,
                                                PortType const portType,
                                                PortIndex const index,
                                                QTransform const &t) const
{
    QPointF result = portPosition(nodeId, portType, index);

    return t.map(result);
}

PortIndex AbstractNodeGeometry::checkPortHit(NodeId const nodeId,
                                             PortType const portType,
                                             QPointF const nodePoint) const
{
    PortIndex result = InvalidPortIndex;

    if (portType == PortType::None)
        return result;

    // The clickable region must follow the same style the painter sizes the
    // port circles from, otherwise a node carrying its own style draws its
    // ports in one place and accepts clicks in another.
    std::optional<NodeStyle> fallback_style;
    NodeStyle const &nodeStyle = node_rendering::resolved_node_style(_graphModel, nodeId, fallback_style);

    double const tolerance = 2.0 * nodeStyle.ConnectionPointDiameter;

    size_t const n = _graphModel.nodeData<unsigned int>(nodeId, portCountRole(portType));

    for (unsigned int portIndex = 0; portIndex < n; ++portIndex) {
        auto pp = portPosition(nodeId, portType, portIndex);

        QPointF p = pp - nodePoint;
        auto distance = std::sqrt(QPointF::dotProduct(p, p));

        if (distance < tolerance) {
            result = portIndex;
            break;
        }
    }

    return result;
}

} // namespace QtNodes
