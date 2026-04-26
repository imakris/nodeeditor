#include "SerializationValidation.hpp"

#include <cmath>
#include <stdexcept>

namespace QtNodes::detail {

bool read_unsigned_number(QJsonValue const &value, quint64 maxValue, quint64 &result)
{
    if (!value.isDouble()) {
        return false;
    }

    double const parsed = value.toDouble();
    if (!std::isfinite(parsed) || parsed < 0.0 || parsed > static_cast<double>(maxValue)) {
        return false;
    }

    quint64 const integral = static_cast<quint64>(parsed);
    if (parsed != static_cast<double>(integral)) {
        return false;
    }

    result = integral;
    return true;
}

bool read_node_id(QJsonValue const &value, NodeId &nodeId)
{
    quint64 parsed = 0;

    if (!read_unsigned_number(value, InvalidNodeId - 1ull, parsed)) {
        return false;
    }

    nodeId = static_cast<NodeId>(parsed);
    return true;
}

bool read_group_id(QJsonValue const &value, GroupId &groupId)
{
    quint64 parsed = 0;

    if (!read_unsigned_number(value, InvalidGroupId - 1ull, parsed)) {
        return false;
    }

    groupId = static_cast<GroupId>(parsed);
    return true;
}

bool read_port_index(QJsonValue const &value, PortIndex &portIndex)
{
    quint64 parsed = 0;

    if (!read_unsigned_number(value, InvalidPortIndex - 1ull, parsed)) {
        return false;
    }

    portIndex = static_cast<PortIndex>(parsed);
    return true;
}

bool read_finite_number(QJsonValue const &value, double &result)
{
    if (!value.isDouble()) {
        return false;
    }

    double const parsed = value.toDouble();
    if (!std::isfinite(parsed)) {
        return false;
    }

    result = parsed;
    return true;
}

bool read_required_object(QJsonObject const &obj, QString const &key, QJsonObject &result)
{
    auto const it = obj.find(key);

    if (it == obj.end() || !it->isObject()) {
        return false;
    }

    result = it->toObject();
    return true;
}

bool read_required_array(QJsonObject const &obj, QString const &key, QJsonArray &result)
{
    auto const it = obj.find(key);

    if (it == obj.end() || !it->isArray()) {
        return false;
    }

    result = it->toArray();
    return true;
}

bool read_required_string(QJsonObject const &obj, QString const &key, QString &result)
{
    auto const it = obj.find(key);

    if (it == obj.end() || !it->isString()) {
        return false;
    }

    result = it->toString();
    return true;
}

bool read_optional_bool(QJsonObject const &obj, QString const &key, bool &result)
{
    auto const it = obj.find(key);

    if (it == obj.end()) {
        return true;
    }

    if (!it->isBool()) {
        return false;
    }

    result = it->toBool();
    return true;
}

bool read_required_point(QJsonObject const &obj, QString const &key, QPointF &result)
{
    QJsonObject pointObject;

    if (!read_required_object(obj, key, pointObject)) {
        return false;
    }

    double x = 0.0;
    double y = 0.0;

    if (!read_finite_number(pointObject["x"], x) || !read_finite_number(pointObject["y"], y)) {
        return false;
    }

    result = QPointF(x, y);
    return true;
}

QPointF read_required_point_or_throw(QJsonObject const &obj, QString const &key, char const *errorMessage)
{
    QPointF point;
    if (!read_required_point(obj, key, point)) {
        throw std::logic_error(errorMessage);
    }
    return point;
}

NodeId read_node_id_or_throw(QJsonValue const &value, char const *errorMessage)
{
    NodeId nodeId = InvalidNodeId;
    if (!read_node_id(value, nodeId)) {
        throw std::logic_error(errorMessage);
    }
    return nodeId;
}

} // namespace QtNodes::detail
