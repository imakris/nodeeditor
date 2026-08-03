#include "ApplicationSetup.hpp"

#include "DynamicPortsActions.hpp"
#include "DynamicPortsModel.hpp"

#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/ConnectionIdUtils>
#include <QtNodes/GraphicsView>

#include <catch2/catch.hpp>

#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QTemporaryFile>
#include <QUndoCommand>
#include <QUndoStack>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace {

QJsonObject make_node(NodeId id,
                      QPointF const &position,
                      unsigned int in_port_count,
                      unsigned int out_port_count)
{
    return QJsonObject{
        {"id", static_cast<qint64>(id)},
        {"position", QJsonObject{{"x", position.x()}, {"y", position.y()}}},
        {"inPortCount", QString::number(in_port_count)},
        {"outPortCount", QString::number(out_port_count)},
    };
}

QJsonObject make_connection(NodeId out_node_id,
                            PortIndex out_port_index,
                            NodeId in_node_id,
                            PortIndex in_port_index)
{
    return QJsonObject{
        {"outNodeId", static_cast<qint64>(out_node_id)},
        {"outPortIndex", static_cast<qint64>(out_port_index)},
        {"inNodeId", static_cast<qint64>(in_node_id)},
        {"inPortIndex", static_cast<qint64>(in_port_index)},
    };
}

QJsonObject make_valid_graph()
{
    return QJsonObject{
        {"nodes",
         QJsonArray{
             make_node(4, QPointF(10.5, 20.25), 1, 2),
             make_node(9, QPointF(-3.0, 7.5), 2, 1),
             make_node(12, QPointF(100.0, -50.0), 1, 1),
         }},
        {"connections",
         QJsonArray{
             make_connection(4, 1, 9, 0),
             make_connection(9, 0, 12, 0),
         }},
    };
}

QByteArray serialized_graph_at_size(qsizetype size)
{
    QByteArray serialized = QJsonDocument(make_valid_graph()).toJson(QJsonDocument::Compact);
    REQUIRE(serialized.size() <= size);
    serialized.append(QByteArray(size - serialized.size(), ' '));
    return serialized;
}

void write_temporary_file(QTemporaryFile &file, QByteArray const &serialized)
{
    REQUIRE(file.open());
    REQUIRE(file.write(serialized) == serialized.size());
    REQUIRE(file.flush());
    file.close();
}

void replace_node_field(QJsonObject &graph,
                        qsizetype node_index,
                        QString const &field,
                        QJsonValue const &value)
{
    QJsonArray nodes = graph["nodes"].toArray();
    QJsonObject node = nodes[node_index].toObject();
    node[field] = value;
    nodes.replace(node_index, node);
    graph["nodes"] = nodes;
}

void replace_connection_field(QJsonObject &graph,
                              qsizetype connection_index,
                              QString const &field,
                              QJsonValue const &value)
{
    QJsonArray connections = graph["connections"].toArray();
    QJsonObject connection = connections[connection_index].toObject();
    connection[field] = value;
    connections.replace(connection_index, connection);
    graph["connections"] = connections;
}

struct model_signal_counts_t
{
    int connection_created = 0;
    int connection_deleted = 0;
    int node_created = 0;
    int node_deleted = 0;
    int node_updated = 0;
    int node_position_updated = 0;
    int model_reset = 0;
    std::vector<QString> sequence;
};

void count_model_signals(DynamicPortsModel &model, model_signal_counts_t &counts)
{
    QObject::connect(&model,
                     &QtNodes::AbstractGraphModel::connectionCreated,
                     [&counts](ConnectionId const &) {
                         ++counts.connection_created;
                         counts.sequence.push_back("connectionCreated");
                     });
    QObject::connect(&model,
                     &QtNodes::AbstractGraphModel::connectionDeleted,
                     [&counts](ConnectionId const &) {
                         ++counts.connection_deleted;
                         counts.sequence.push_back("connectionDeleted");
                     });
    QObject::connect(&model, &QtNodes::AbstractGraphModel::nodeCreated, [&counts](NodeId) {
        ++counts.node_created;
        counts.sequence.push_back("nodeCreated");
    });
    QObject::connect(&model, &QtNodes::AbstractGraphModel::nodeDeleted, [&counts](NodeId) {
        ++counts.node_deleted;
        counts.sequence.push_back("nodeDeleted");
    });
    QObject::connect(&model, &QtNodes::AbstractGraphModel::nodeUpdated, [&counts](NodeId) {
        ++counts.node_updated;
        counts.sequence.push_back("nodeUpdated");
    });
    QObject::connect(&model, &QtNodes::AbstractGraphModel::nodePositionUpdated, [&counts](NodeId) {
        ++counts.node_position_updated;
        counts.sequence.push_back("nodePositionUpdated");
    });
    QObject::connect(&model, &QtNodes::AbstractGraphModel::modelReset, [&counts] {
        ++counts.model_reset;
        counts.sequence.push_back("modelReset");
    });
}

void populate_existing_graph(DynamicPortsModel &model)
{
    NodeId const first = model.addNode();
    NodeId const second = model.addNode();

    REQUIRE(model.setNodeData(first, NodeRole::Position, QPointF(1.0, 2.0)));
    REQUIRE(model.setNodeData(first, NodeRole::InPortCount, 1));
    REQUIRE(model.setNodeData(first, NodeRole::OutPortCount, 1));
    REQUIRE(model.setNodeData(second, NodeRole::Position, QPointF(3.0, 4.0)));
    REQUIRE(model.setNodeData(second, NodeRole::InPortCount, 1));
    REQUIRE(model.setNodeData(second, NodeRole::OutPortCount, 1));
    model.addConnection(ConnectionId{first, 0, second, 0});
}

struct malformed_case_t
{
    std::string name;
    QJsonObject graph;
};

std::vector<malformed_case_t> make_malformed_graphs()
{
    std::vector<malformed_case_t> cases;
    auto add = [&cases](std::string name, QJsonObject graph) {
        cases.push_back(malformed_case_t{std::move(name), std::move(graph)});
    };

    QJsonObject graph = make_valid_graph();
    graph.remove("nodes");
    add("missing nodes", graph);

    graph = make_valid_graph();
    graph["nodes"] = 1;
    add("nodes is not an array", graph);

    graph = make_valid_graph();
    graph["nodes"] = QJsonArray{"not-an-object"};
    add("node is not an object", graph);

    graph = make_valid_graph();
    replace_node_field(graph, 0, "id", "4");
    add("node id has wrong local type", graph);

    graph = make_valid_graph();
    replace_node_field(graph, 0, "id", -1);
    add("node id is negative", graph);

    graph = make_valid_graph();
    replace_node_field(graph, 0, "id", 4.5);
    add("node id is fractional", graph);

    graph = make_valid_graph();
    replace_node_field(graph, 0, "id", static_cast<double>(InvalidNodeId));
    add("node id is sentinel", graph);

    graph = make_valid_graph();
    replace_node_field(graph, 0, "position", "not-an-object");
    add("position is not an object", graph);

    graph = make_valid_graph();
    replace_node_field(graph, 0, "position", QJsonObject{{"x", "10.5"}, {"y", 20.25}});
    add("position coordinate has wrong type", graph);

    graph = make_valid_graph();
    replace_node_field(graph,
                       0,
                       "position",
                       QJsonObject{{"x", std::numeric_limits<double>::infinity()}, {"y", 1.0}});
    add("position coordinate is non-finite", graph);

    for (QString const &invalid_count : {QString("-1"),
                                         QString("1.5"),
                                         QString("abc"),
                                         QString("01"),
                                         QString("4294967296"),
                                         QString("33")}) {
        graph = make_valid_graph();
        replace_node_field(graph, 0, "inPortCount", invalid_count);
        add("invalid input port string " + invalid_count.toStdString(), graph);
    }

    graph = make_valid_graph();
    replace_node_field(graph, 0, "outPortCount", 1);
    add("port count has wrong local type", graph);

    graph = make_valid_graph();
    QJsonArray nodes = graph["nodes"].toArray();
    nodes.append(nodes[0]);
    graph["nodes"] = nodes;
    add("duplicate node id", graph);

    graph = QJsonObject{{"nodes", QJsonArray{}}, {"connections", QJsonArray{}}};
    nodes = QJsonArray{};
    for (std::size_t index = 0; index <= DynamicPortsModel::s_max_serialized_nodes; ++index) {
        nodes.append(make_node(static_cast<NodeId>(index), QPointF(), 0, 0));
    }
    graph["nodes"] = nodes;
    add("node resource limit", graph);

    graph = QJsonObject{{"nodes", QJsonArray{}}, {"connections", QJsonArray{}}};
    nodes = QJsonArray{};
    for (NodeId id = 0; id < 9; ++id) {
        nodes.append(make_node(id, QPointF(), 16, 16));
    }
    graph["nodes"] = nodes;
    add("total port resource limit", graph);

    graph = make_valid_graph();
    graph.remove("connections");
    add("missing connections", graph);

    graph = make_valid_graph();
    graph["connections"] = 1;
    add("connections is not an array", graph);

    graph = make_valid_graph();
    graph["connections"] = QJsonArray{"not-an-object"};
    add("connection is not an object", graph);

    graph = make_valid_graph();
    QJsonArray connections = graph["connections"].toArray();
    QJsonObject connection = connections[0].toObject();
    connection.remove("outNodeId");
    connections.replace(0, connection);
    graph["connections"] = connections;
    add("connection is missing a field", graph);

    graph = make_valid_graph();
    replace_connection_field(graph, 0, "outNodeId", "4");
    add("connection endpoint has wrong local type", graph);

    graph = make_valid_graph();
    replace_connection_field(graph, 0, "inNodeId", static_cast<double>(InvalidNodeId));
    add("connection endpoint is sentinel", graph);

    graph = make_valid_graph();
    replace_connection_field(graph, 0, "outNodeId", 8);
    add("unknown output endpoint", graph);

    graph = make_valid_graph();
    replace_connection_field(graph, 0, "inNodeId", 8);
    add("unknown input endpoint", graph);

    graph = make_valid_graph();
    replace_connection_field(graph, 0, "outPortIndex", 2);
    add("output port is out of range", graph);

    graph = make_valid_graph();
    replace_connection_field(graph, 0, "inPortIndex", 2);
    add("input port is out of range", graph);

    graph = make_valid_graph();
    connections = graph["connections"].toArray();
    connections.append(connections[0]);
    graph["connections"] = connections;
    add("duplicate connection", graph);

    graph = make_valid_graph();
    connections = graph["connections"].toArray();
    connections.append(make_connection(4, 1, 9, 1));
    graph["connections"] = connections;
    add("output port is occupied", graph);

    graph = make_valid_graph();
    connections = graph["connections"].toArray();
    connections.append(make_connection(12, 0, 9, 0));
    graph["connections"] = connections;
    add("input port is occupied", graph);

    graph = make_valid_graph();
    connections = graph["connections"].toArray();
    connections.append("late-malformed-entry");
    graph["connections"] = connections;
    add("late malformed connection", graph);

    graph = make_valid_graph();
    connections = QJsonArray{};
    for (std::size_t index = 0; index <= DynamicPortsModel::s_max_serialized_connections; ++index) {
        connections.append(make_connection(4, 1, 9, 0));
    }
    graph["connections"] = connections;
    add("connection resource limit", graph);

    return cases;
}

QtNodes::AbstractGraphModel::ConnectionIdSet saved_connection_set(DynamicPortsModel const &model)
{
    QtNodes::AbstractGraphModel::ConnectionIdSet result;
    for (QJsonValue const &value : model.save()["connections"].toArray()) {
        ConnectionId connection_id{};
        REQUIRE(QtNodes::tryFromJson(value.toObject(), connection_id));
        result.insert(connection_id);
    }
    return result;
}

QJsonObject make_port_boundary_graph()
{
    QJsonArray nodes;
    for (NodeId id = 0; id < 8; ++id) {
        nodes.append(make_node(id, QPointF(id, -static_cast<qreal>(id)), 16, 16));
    }
    return QJsonObject{{"nodes", nodes}, {"connections", QJsonArray{}}};
}

} // namespace

TEST_CASE("Dynamic ports graph rejection is atomic", "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();
    DynamicPortsModel model;
    populate_existing_graph(model);

    QJsonObject const before = model.save();
    model_signal_counts_t counts;
    count_model_signals(model, counts);

    for (malformed_case_t const &test_case : make_malformed_graphs()) {
        CAPTURE(test_case.name);
        CHECK_FALSE(model.load(test_case.graph));
        CHECK(model.save() == before);
        CHECK(counts.sequence.empty());
    }

    CHECK(counts.connection_created == 0);
    CHECK(counts.connection_deleted == 0);
    CHECK(counts.node_created == 0);
    CHECK(counts.node_deleted == 0);
    CHECK(counts.node_updated == 0);
    CHECK(counts.node_position_updated == 0);
    CHECK(counts.model_reset == 0);
}

TEST_CASE("Dynamic ports byte ingress rejects malformed documents atomically",
          "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();
    DynamicPortsModel model;
    populate_existing_graph(model);
    QJsonObject const before = model.save();
    model_signal_counts_t counts;
    count_model_signals(model, counts);

    for (QByteArray const &serialized :
         {QByteArray("{"), QByteArray("[]"), QByteArray("1"), QByteArray("null")}) {
        CAPTURE(serialized);
        CHECK_FALSE(model.load_from_json(serialized));
        CHECK(model.save() == before);
        CHECK(counts.sequence.empty());
    }
}

TEST_CASE("Dynamic ports byte ingress enforces its serialized resource bound",
          "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();

    SECTION("valid JSON below and at the limit")
    {
        for (qsizetype const size : {DynamicPortsModel::s_max_serialized_bytes - 1,
                                     DynamicPortsModel::s_max_serialized_bytes}) {
            CAPTURE(size);
            DynamicPortsModel model;
            populate_existing_graph(model);
            REQUIRE(model.load_from_json(serialized_graph_at_size(size)));
            CHECK(model.allNodeIds() == DynamicPortsModel::NodeIdSet{4, 9, 12});
        }
    }

    SECTION("oversized whitespace and ignored fields")
    {
        QByteArray const whitespace_over_limit = serialized_graph_at_size(
            DynamicPortsModel::s_max_serialized_bytes + 1);

        QJsonObject ignored_field_graph = make_valid_graph();
        ignored_field_graph["ignored"] = QString(DynamicPortsModel::s_max_serialized_bytes,
                                                 QChar('x'));
        QByteArray const ignored_field_over_limit = QJsonDocument(ignored_field_graph)
                                                        .toJson(QJsonDocument::Compact);
        REQUIRE(ignored_field_over_limit.size() > DynamicPortsModel::s_max_serialized_bytes);

        for (QByteArray const &serialized : {whitespace_over_limit, ignored_field_over_limit}) {
            DynamicPortsModel model;
            populate_existing_graph(model);
            QJsonObject const before = model.save();
            model_signal_counts_t counts;
            count_model_signals(model, counts);

            CHECK_FALSE(model.load_from_json(serialized));
            CHECK(model.save() == before);
            CHECK(counts.sequence.empty());
        }
    }
}

TEST_CASE("Dynamic ports file replacement commits graph and undo history together",
          "[serialization][dynamic-ports][undo]")
{
    auto app = applicationSetup();

    SECTION("successful boundary-size replacement clears dirty history")
    {
        DynamicPortsModel model;
        populate_existing_graph(model);
        QtNodes::BasicGraphicsScene scene(model);
        scene.undoStack().push(new QUndoCommand(QStringLiteral("dirty history")));
        REQUIRE_FALSE(scene.undoStack().isClean());

        QTemporaryFile file;
        write_temporary_file(file,
                             serialized_graph_at_size(DynamicPortsModel::s_max_serialized_bytes));
        REQUIRE(load_graph_from_file(file.fileName(), model, scene));

        CHECK(model.allNodeIds() == DynamicPortsModel::NodeIdSet{4, 9, 12});
        CHECK(scene.undoStack().count() == 0);
        CHECK(scene.undoStack().index() == 0);
        CHECK(scene.undoStack().isClean());
    }

    SECTION("oversized rejection preserves graph and dirty history")
    {
        DynamicPortsModel model;
        populate_existing_graph(model);
        QtNodes::BasicGraphicsScene scene(model);
        scene.undoStack().push(new QUndoCommand(QStringLiteral("preserved history")));
        QJsonObject const before = model.save();
        int const history_count = scene.undoStack().count();
        int const history_index = scene.undoStack().index();
        model_signal_counts_t counts;
        count_model_signals(model, counts);

        QTemporaryFile file;
        write_temporary_file(file,
                             serialized_graph_at_size(DynamicPortsModel::s_max_serialized_bytes
                                                      + 1));
        CHECK_FALSE(load_graph_from_file(file.fileName(), model, scene));

        CHECK(model.save() == before);
        CHECK(counts.sequence.empty());
        CHECK(scene.undoStack().count() == history_count);
        CHECK(scene.undoStack().index() == history_index);
        CHECK_FALSE(scene.undoStack().isClean());
    }

    SECTION("missing file preserves graph and dirty history")
    {
        DynamicPortsModel model;
        populate_existing_graph(model);
        QtNodes::BasicGraphicsScene scene(model);
        scene.undoStack().push(new QUndoCommand(QStringLiteral("preserved history")));
        QJsonObject const before = model.save();

        CHECK_FALSE(
            load_graph_from_file(QStringLiteral("missing-dynamic-ports.flow"), model, scene));
        CHECK(model.save() == before);
        CHECK(scene.undoStack().count() == 1);
        CHECK(scene.undoStack().index() == 1);
        CHECK_FALSE(scene.undoStack().isClean());
    }

    SECTION("invalid JSON preserves graph and dirty history")
    {
        DynamicPortsModel model;
        populate_existing_graph(model);
        QtNodes::BasicGraphicsScene scene(model);
        scene.undoStack().push(new QUndoCommand(QStringLiteral("preserved history")));
        QJsonObject const before = model.save();
        model_signal_counts_t counts;
        count_model_signals(model, counts);

        QTemporaryFile file;
        write_temporary_file(file, QByteArray("{"));
        CHECK_FALSE(load_graph_from_file(file.fileName(), model, scene));

        CHECK(model.save() == before);
        CHECK(counts.sequence.empty());
        CHECK(scene.undoStack().count() == 1);
        CHECK(scene.undoStack().index() == 1);
        CHECK_FALSE(scene.undoStack().isClean());
    }
}

TEST_CASE("Dynamic ports create action contains expected capacity refusal", "[dynamic-ports][ui]")
{
    auto app = applicationSetup();

    SECTION("ordinary action creates one positioned node")
    {
        DynamicPortsModel model;
        QtNodes::BasicGraphicsScene scene(model);
        QtNodes::GraphicsView view(&scene);
        QAction *action = create_node_action(model, view);
        model_signal_counts_t counts;
        count_model_signals(model, counts);

        CHECK_NOTHROW(action->trigger());
        REQUIRE(model.allNodeIds().size() == 1);
        NodeId const node_id = *model.allNodeIds().begin();
        QPointF const position = model.nodeData(node_id, NodeRole::Position).toPointF();
        CHECK(std::isfinite(position.x()));
        CHECK(std::isfinite(position.y()));
        CHECK(counts.node_created == 1);
        CHECK(counts.node_position_updated == 1);
    }

    SECTION("node-count capacity refuses without mutation")
    {
        QJsonArray nodes;
        for (std::size_t index = 0; index < DynamicPortsModel::s_max_serialized_nodes; ++index) {
            nodes.append(make_node(static_cast<NodeId>(index), QPointF(), 0, 0));
        }
        DynamicPortsModel model;
        REQUIRE(model.load(QJsonObject{{"nodes", nodes}, {"connections", QJsonArray{}}}));
        QtNodes::BasicGraphicsScene scene(model);
        QtNodes::GraphicsView view(&scene);
        QAction *action = create_node_action(model, view);
        QJsonObject const before = model.save();
        model_signal_counts_t counts;
        count_model_signals(model, counts);

        CHECK_NOTHROW(action->trigger());
        CHECK(model.save() == before);
        CHECK(counts.sequence.empty());
    }

    SECTION("id exhaustion refuses without mutation")
    {
        DynamicPortsModel model;
        QJsonObject const graph{
            {"nodes", QJsonArray{make_node(InvalidNodeId - 1, QPointF(), 0, 0)}},
            {"connections", QJsonArray{}},
        };
        REQUIRE(model.load(graph));
        QtNodes::BasicGraphicsScene scene(model);
        QtNodes::GraphicsView view(&scene);
        QAction *action = create_node_action(model, view);
        QJsonObject const before = model.save();
        model_signal_counts_t counts;
        count_model_signals(model, counts);

        CHECK_NOTHROW(action->trigger());
        CHECK(model.save() == before);
        CHECK(counts.sequence.empty());
    }
}

TEST_CASE("Dynamic ports graph load preserves all serialized state",
          "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();
    DynamicPortsModel model;
    populate_existing_graph(model);
    model_signal_counts_t counts;
    count_model_signals(model, counts);

    QJsonObject const graph = make_valid_graph();
    REQUIRE(model.load_from_json(QJsonDocument(graph).toJson(QJsonDocument::Compact)));

    CHECK(model.allNodeIds() == DynamicPortsModel::NodeIdSet{4, 9, 12});
    CHECK(model.nodeData(4, NodeRole::Position).toPointF() == QPointF(10.5, 20.25));
    CHECK(model.nodeData(9, NodeRole::Position).toPointF() == QPointF(-3.0, 7.5));
    CHECK(model.nodeData(12, NodeRole::Position).toPointF() == QPointF(100.0, -50.0));
    CHECK(model.nodeData(4, NodeRole::InPortCount).toUInt() == 1);
    CHECK(model.nodeData(4, NodeRole::OutPortCount).toUInt() == 2);
    CHECK(model.nodeData(9, NodeRole::InPortCount).toUInt() == 2);
    CHECK(model.nodeData(9, NodeRole::OutPortCount).toUInt() == 1);
    CHECK(model.nodeData(12, NodeRole::InPortCount).toUInt() == 1);
    CHECK(model.nodeData(12, NodeRole::OutPortCount).toUInt() == 1);

    QtNodes::AbstractGraphModel::ConnectionIdSet const expected_connections{
        ConnectionId{4, 1, 9, 0},
        ConnectionId{9, 0, 12, 0},
    };
    CHECK(saved_connection_set(model) == expected_connections);

    DynamicPortsModel round_trip;
    REQUIRE(round_trip.load(model.save()));
    CHECK(round_trip.allNodeIds() == model.allNodeIds());
    CHECK(saved_connection_set(round_trip) == expected_connections);
    for (NodeId const node_id : model.allNodeIds()) {
        CHECK(round_trip.nodeData(node_id, NodeRole::Position)
              == model.nodeData(node_id, NodeRole::Position));
        CHECK(round_trip.nodeData(node_id, NodeRole::InPortCount)
              == model.nodeData(node_id, NodeRole::InPortCount));
        CHECK(round_trip.nodeData(node_id, NodeRole::OutPortCount)
              == model.nodeData(node_id, NodeRole::OutPortCount));
    }

    CHECK(counts.connection_deleted == 1);
    CHECK(counts.node_deleted == 2);
    CHECK(counts.node_created == 3);
    CHECK(counts.node_position_updated == 3);
    CHECK(counts.connection_created == 2);
    CHECK(counts.node_updated == 0);
    CHECK(counts.model_reset == 0);
    std::vector<QString> const expected_sequence{
        "connectionDeleted",
        "nodeDeleted",
        "nodeDeleted",
        "nodePositionUpdated",
        "nodeCreated",
        "nodePositionUpdated",
        "nodeCreated",
        "nodePositionUpdated",
        "nodeCreated",
        "connectionCreated",
        "connectionCreated",
    };
    CHECK(counts.sequence == expected_sequence);
}

TEST_CASE("Dynamic ports graph load accepts an empty replacement", "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();
    DynamicPortsModel model;
    populate_existing_graph(model);
    model_signal_counts_t counts;
    count_model_signals(model, counts);

    QJsonObject const empty_graph{
        {"nodes", QJsonArray{}},
        {"connections", QJsonArray{}},
    };
    REQUIRE(model.load(empty_graph));

    CHECK(model.allNodeIds().empty());
    CHECK(counts.connection_deleted == 1);
    CHECK(counts.node_deleted == 2);
    CHECK(counts.node_created == 0);
    CHECK(counts.connection_created == 0);
    std::vector<QString> const expected_sequence{
        "connectionDeleted",
        "nodeDeleted",
        "nodeDeleted",
    };
    CHECK(counts.sequence == expected_sequence);
}

TEST_CASE("Dynamic ports load replaces scene-owned widgets safely", "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();
    DynamicPortsModel model;
    populate_existing_graph(model);
    QtNodes::BasicGraphicsScene scene(model);

    REQUIRE(model.load(make_valid_graph()));
    CHECK(scene.nodeGraphicsObject(0) == nullptr);
    CHECK(scene.nodeGraphicsObject(1) == nullptr);
    CHECK(scene.nodeGraphicsObject(4) != nullptr);
    CHECK(scene.nodeGraphicsObject(9) != nullptr);
    CHECK(scene.nodeGraphicsObject(12) != nullptr);
}

TEST_CASE("Dynamic ports resource boundaries remain reloadable", "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();

    SECTION("total port boundary")
    {
        DynamicPortsModel model;
        REQUIRE(model.load(make_port_boundary_graph()));
        DynamicPortsModel round_trip;
        REQUIRE(round_trip.load(model.save()));
        CHECK(round_trip.allNodeIds() == model.allNodeIds());
    }

    SECTION("node boundary")
    {
        QJsonArray nodes;
        for (std::size_t index = 0; index < DynamicPortsModel::s_max_serialized_nodes; ++index) {
            nodes.append(make_node(static_cast<NodeId>(index), QPointF(), 0, 0));
        }
        DynamicPortsModel model;
        REQUIRE(model.load(QJsonObject{{"nodes", nodes}, {"connections", QJsonArray{}}}));
        CHECK(model.allNodeIds().size() == DynamicPortsModel::s_max_serialized_nodes);
        model_signal_counts_t counts;
        count_model_signals(model, counts);
        CHECK(model.addNode() == InvalidNodeId);
        CHECK(model.allNodeIds().size() == DynamicPortsModel::s_max_serialized_nodes);
        CHECK(counts.sequence.empty());
    }

    SECTION("per-node port boundary")
    {
        DynamicPortsModel model;
        NodeId const node_id = model.addNode();
        REQUIRE(model.setNodeData(node_id,
                                  NodeRole::InPortCount,
                                  DynamicPortsModel::s_max_serialized_ports_per_node));
        QJsonObject const before = model.save();
        CHECK_FALSE(model.addPort(node_id,
                                  PortType::In,
                                  DynamicPortsModel::s_max_serialized_ports_per_node));
        CHECK_FALSE(model.setNodeData(node_id,
                                      NodeRole::InPortCount,
                                      DynamicPortsModel::s_max_serialized_ports_per_node + 1));
        CHECK(model.save() == before);
        DynamicPortsModel round_trip;
        REQUIRE(round_trip.load(before));
    }
}

TEST_CASE("Dynamic ports connection admission refuses without mutation",
          "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();
    DynamicPortsModel model;
    populate_existing_graph(model);
    QJsonObject const before = model.save();
    model_signal_counts_t counts;
    count_model_signals(model, counts);

    // The fixture gives both nodes exactly one port per direction and already
    // connects the only admissible pair, so port index 1 names a port that does
    // not exist and the third call repeats an existing connection.
    model.addConnection(ConnectionId{0, 1, 1, 0});
    model.addConnection(ConnectionId{0, 0, 1, 1});
    model.addConnection(ConnectionId{0, 0, 1, 0});

    CHECK(model.save() == before);
    CHECK(counts.sequence.empty());
}

TEST_CASE("Dynamic ports allocator never exposes reserved ids", "[serialization][dynamic-ports]")
{
    auto app = applicationSetup();

    SECTION("last admissible id is allocated once")
    {
        DynamicPortsModel model;
        QJsonObject const graph{
            {"nodes", QJsonArray{make_node(InvalidNodeId - 2, QPointF(), 0, 0)}},
            {"connections", QJsonArray{}},
        };
        REQUIRE(model.load(graph));
        model_signal_counts_t counts;
        count_model_signals(model, counts);
        CHECK(model.addNode() == InvalidNodeId - 1);
        CHECK(model.nodeExists(InvalidNodeId - 1));
        CHECK(counts.node_created == 1);
        CHECK_THROWS_AS(model.newNodeId(), std::overflow_error);
        CHECK_THROWS_AS(model.newNodeId(), std::overflow_error);
    }

    SECTION("maximum external admissible id")
    {
        DynamicPortsModel model;
        QJsonObject const graph{
            {"nodes", QJsonArray{make_node(InvalidNodeId - 1, QPointF(), 0, 0)}},
            {"connections", QJsonArray{}},
        };
        REQUIRE(model.load(graph));
        CHECK(model.allNodeIds() == DynamicPortsModel::NodeIdSet{InvalidNodeId - 1});
        QJsonObject const before = model.save();
        model_signal_counts_t counts;
        count_model_signals(model, counts);
        CHECK_THROWS_AS(model.newNodeId(), std::overflow_error);
        CHECK_THROWS_AS(model.newNodeId(), std::overflow_error);
        CHECK_THROWS_AS(model.addNode(), std::overflow_error);
        CHECK_THROWS_AS(model.addNode(), std::overflow_error);
        CHECK(model.save() == before);
        CHECK(counts.sequence.empty());
    }
}
