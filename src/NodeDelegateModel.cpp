#include "NodeDelegateModel.hpp"

#include "StyleCollection.hpp"

namespace QtNodes {

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
    _nodeValidationState = validationState;
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
    _nodeStyle = style;
    _processingStatusIconDirty = true;
}

QPixmap NodeDelegateModel::processingStatusIcon() const
{
    int const resolution = _nodeStyle.processingIconStyle._resolution;

    if (_processingStatus == NodeProcessingStatus::NoStatus) {
        return {};
    }

    if (!_processingStatusIconDirty && _cachedProcessingStatus == _processingStatus
        && _cachedProcessingStatusResolution == resolution) {
        return _cachedProcessingStatusIcon;
    }

    switch (_processingStatus) {
    case NodeProcessingStatus::NoStatus:
        _cachedProcessingStatusIcon = {};
        break;
    case NodeProcessingStatus::Updated:
        _cachedProcessingStatusIcon = _nodeStyle.statusUpdated.pixmap(resolution);
        break;
    case NodeProcessingStatus::Processing:
        _cachedProcessingStatusIcon = _nodeStyle.statusProcessing.pixmap(resolution);
        break;
    case NodeProcessingStatus::Pending:
        _cachedProcessingStatusIcon = _nodeStyle.statusPending.pixmap(resolution);
        break;
    case NodeProcessingStatus::Empty:
        _cachedProcessingStatusIcon = _nodeStyle.statusEmpty.pixmap(resolution);
        break;
    case NodeProcessingStatus::Failed:
        _cachedProcessingStatusIcon = _nodeStyle.statusInvalid.pixmap(resolution);
        break;
    case NodeProcessingStatus::Partial:
        _cachedProcessingStatusIcon = _nodeStyle.statusPartial.pixmap(resolution);
        break;
    }

    _cachedProcessingStatus = _processingStatus;
    _cachedProcessingStatusResolution = resolution;
    _processingStatusIconDirty = false;

    return _cachedProcessingStatusIcon;
}

void NodeDelegateModel::setStatusIcon(NodeProcessingStatus status, const QPixmap &pixmap)
{
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
    _nodeStyle.processingIconStyle = style;
    _processingStatusIconDirty = true;
}

void NodeDelegateModel::setNodeProcessingStatus(NodeProcessingStatus status)
{
    _processingStatus = status;
    _processingStatusIconDirty = true;
}

void NodeDelegateModel::setBackgroundColor(QColor const &color)
{
    _nodeStyle.setBackgroundColor(color);
}

} // namespace QtNodes
