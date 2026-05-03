#pragma once

#include "Definitions.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QPointF>
#include <QString>

namespace QtNodes::detail {

bool read_unsigned_number(QJsonValue const &value, quint64 maxValue, quint64 &result);

bool read_node_id(QJsonValue const &value, NodeId &nodeId);

bool read_group_id(QJsonValue const &value, GroupId &groupId);

bool read_port_index(QJsonValue const &value, PortIndex &portIndex);

bool read_finite_number(QJsonValue const &value, double &result);

bool read_required_object(QJsonObject const &obj, QString const &key, QJsonObject &result);

bool read_required_array(QJsonObject const &obj, QString const &key, QJsonArray &result);

bool read_required_string(QJsonObject const &obj, QString const &key, QString &result);

bool read_optional_bool(QJsonObject const &obj, QString const &key, bool &result);

bool read_required_point(QJsonObject const &obj, QString const &key, QPointF &result);

/// Reads a required point from `obj[key]` and throws std::logic_error with `errorMessage` on failure.
QPointF read_required_point_or_throw(QJsonObject const& obj, QString const& key, char const* errorMessage);

/// Reads a NodeId from a QJsonValue and throws std::logic_error with `errorMessage` on failure.
NodeId read_node_id_or_throw(QJsonValue const& value, char const* errorMessage);

} // namespace QtNodes::detail
