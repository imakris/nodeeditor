#include "NodeRenderingUtils.hpp"

#include "DataFlowGraphModel.hpp"
#include "NodeDelegateModel.hpp"
#include "StyleCollection.hpp"

#include <QtCore/QJsonDocument>
#include <QtGui/QImage>
#include <QtGui/QPainter>

#include <algorithm>
#include <cmath>

namespace QtNodes::node_rendering {

NodeStyle const &resolved_node_style(
    AbstractGraphModel &model,
    NodeId node_id,
    std::optional<NodeStyle> &fallback_storage)
{
    if (auto *df_model = dynamic_cast<DataFlowGraphModel *>(&model)) {
        if (auto *delegate = df_model->delegateModel<NodeDelegateModel>(node_id)) {
            return delegate->nodeStyle();
        }
    }

    QVariant const style_data = model.nodeData(node_id, NodeRole::Style);
    if (!style_data.isValid() || style_data.isNull()) {
        return StyleCollection::nodeStyle();
    }

    QJsonObject const style_json = QJsonDocument::fromVariant(style_data).object();
    if (style_json.isEmpty()) {
        return StyleCollection::nodeStyle();
    }

    fallback_storage.emplace(style_json);
    return *fallback_storage;
}

QImage render_icon_image(QIcon const &icon, QSize const &logical_size, qreal dpr)
{
    QSize const physical_size(std::max(1, static_cast<int>(std::ceil(logical_size.width() * dpr))),
                              std::max(1, static_cast<int>(std::ceil(logical_size.height() * dpr))));

    QImage image(physical_size, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    icon.paint(&painter, QRect(QPoint(0, 0), logical_size));
    painter.end();

    return image;
}

} // namespace QtNodes::node_rendering
