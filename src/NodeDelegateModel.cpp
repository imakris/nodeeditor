#include "NodeDelegateModel.hpp"

#include "NodeRenderingUtils.hpp"
#include "StyleCollection.hpp"

#include <QtCore/QThread>

namespace QtNodes {

namespace {

QIcon const &status_icon(NodeStyle const &style, NodeProcessingStatus status)
{
    switch (status) {
    case NodeProcessingStatus::Updated:
        return style.statusUpdated;
    case NodeProcessingStatus::Processing:
        return style.statusProcessing;
    case NodeProcessingStatus::Pending:
        return style.statusPending;
    case NodeProcessingStatus::Empty:
        return style.statusEmpty;
    case NodeProcessingStatus::Failed:
        return style.statusInvalid;
    case NodeProcessingStatus::Partial:
        return style.statusPartial;
    case NodeProcessingStatus::NoStatus:
        break;
    }

    return style.statusEmpty;
}

} // namespace

NodeDelegateModel::NodeDelegateModel()
    : _nodeStyle(StyleCollection::nodeStyle())
{
    // Derived classes can initialize specific style here
}

QJsonObject NodeDelegateModel::save() const
{
    QJsonObject modelJson;

    modelJson["model-name"] = name();

    return modelJson;
}

void NodeDelegateModel::load(QJsonObject const &)
{
    //
}

void NodeDelegateModel::setValidationState(const NodeValidationState &validationState)
{
    Q_ASSERT(thread() == QThread::currentThread());

    _nodeValidationState = validationState;
}

void NodeDelegateModel::setFrozenState(bool state)
{
    Q_ASSERT(thread() == QThread::currentThread());

    _frozen = state;
}

ConnectionPolicy NodeDelegateModel::portConnectionPolicy(PortType portType, PortIndex) const
{
    auto result = ConnectionPolicy::One;
    switch (portType) {
    case PortType::In:
        result = ConnectionPolicy::One;
        break;
    case PortType::Out:
        result = ConnectionPolicy::Many;
        break;
    case PortType::None:
        break;
    }

    return result;
}

NodeStyle const &NodeDelegateModel::nodeStyle() const
{
    return _nodeStyle;
}

void NodeDelegateModel::setNodeStyle(NodeStyle const &style)
{
    Q_ASSERT(thread() == QThread::currentThread());

    _nodeStyle = style;
    _processingStatusIconDirty = true;
}

QImage NodeDelegateModel::processingStatusImage(qreal dpr) const
{
    // render_icon_image() drives QPainter and QIcon::paint(), which are GUI-thread only.
    Q_ASSERT(thread() == QThread::currentThread());

    int const resolution = _nodeStyle.processingIconStyle._resolution;

    if (_processingStatus == NodeProcessingStatus::NoStatus) {
        return {};
    }

    if (!_processingStatusIconDirty && _cachedProcessingStatus == _processingStatus
        && _cachedProcessingStatusResolution == resolution
        && qFuzzyCompare(_cachedProcessingStatusDpr, dpr)) {
        return _cachedProcessingStatusImage;
    }

    _cachedProcessingStatusImage = node_rendering::render_icon_image(
        status_icon(_nodeStyle, _processingStatus),
        QSize(resolution, resolution),
        dpr);

    _cachedProcessingStatus = _processingStatus;
    _cachedProcessingStatusResolution = resolution;
    _cachedProcessingStatusDpr = dpr;
    _processingStatusIconDirty = false;

    return _cachedProcessingStatusImage;
}

ProcessingIconStyle NodeDelegateModel::processingIconStyle() const
{
    return _nodeStyle.processingIconStyle;
}

void NodeDelegateModel::setStatusIcon(NodeProcessingStatus status, const QPixmap &pixmap)
{
    // QPixmap may not be used outside the GUI thread.
    Q_ASSERT(thread() == QThread::currentThread());

    switch (status) {
    case NodeProcessingStatus::NoStatus:
        break;
    case NodeProcessingStatus::Updated:
        _nodeStyle.statusUpdated = QIcon(pixmap);
        break;
    case NodeProcessingStatus::Processing:
        _nodeStyle.statusProcessing = QIcon(pixmap);
        break;
    case NodeProcessingStatus::Pending:
        _nodeStyle.statusPending = QIcon(pixmap);
        break;
    case NodeProcessingStatus::Empty:
        _nodeStyle.statusEmpty = QIcon(pixmap);
        break;
    case NodeProcessingStatus::Failed:
        _nodeStyle.statusInvalid = QIcon(pixmap);
        break;
    case NodeProcessingStatus::Partial:
        _nodeStyle.statusPartial = QIcon(pixmap);
        break;
    }

    _processingStatusIconDirty = true;
}

void NodeDelegateModel::setStatusIconStyle(const ProcessingIconStyle &style)
{
    Q_ASSERT(thread() == QThread::currentThread());

    _nodeStyle.processingIconStyle = style;
    _processingStatusIconDirty = true;
}

void NodeDelegateModel::setNodeProcessingStatus(NodeProcessingStatus status)
{
    Q_ASSERT(thread() == QThread::currentThread());

    _processingStatus = status;
    _processingStatusIconDirty = true;
}

void NodeDelegateModel::setBackgroundColor(QColor const &color)
{
    Q_ASSERT(thread() == QThread::currentThread());

    _nodeStyle.setBackgroundColor(color);
}

} // namespace QtNodes
