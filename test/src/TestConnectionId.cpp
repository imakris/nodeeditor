#include <QtNodes/Definitions>
#include <QtNodes/ConnectionIdUtils>

#include <catch2/catch.hpp>

#include <QJsonObject>
#include <QStringList>

#include <limits>
#include <vector>

using QtNodes::ConnectionId;
using QtNodes::InvalidNodeId;
using QtNodes::InvalidPortIndex;
using QtNodes::invertConnection;
using QtNodes::NodeId;
using QtNodes::PortIndex;
using QtNodes::toJson;
using QtNodes::tryFromJson;

TEST_CASE("ConnectionId basic functionality", "[core]")
{
    NodeId node1 = 1;
    NodeId node2 = 2;
    PortIndex port1 = 0;
    PortIndex port2 = 1;

    SECTION("ConnectionId creation and equality")
    {
        ConnectionId conn1{node1, port1, node2, port2};
        ConnectionId conn2{node1, port1, node2, port2};
        ConnectionId conn3{node2, port1, node1, port2};

        CHECK(conn1 == conn2);
        CHECK(conn1 != conn3);
        CHECK(conn2 != conn3);

        // Test individual fields
        CHECK(conn1.outNodeId == node1);
        CHECK(conn1.outPortIndex == port1);
        CHECK(conn1.inNodeId == node2);
        CHECK(conn1.inPortIndex == port2);
    }

    SECTION("ConnectionId inversion")
    {
        ConnectionId original{node1, port1, node2, port2};
        ConnectionId copy = original;

        invertConnection(copy);

        CHECK(copy.outNodeId == original.inNodeId);
        CHECK(copy.outPortIndex == original.inPortIndex);
        CHECK(copy.inNodeId == original.outNodeId);
        CHECK(copy.inPortIndex == original.outPortIndex);

        // Inverting again should restore original
        invertConnection(copy);
        CHECK(copy == original);
    }
}

TEST_CASE("ConnectionId edge cases", "[core]")
{
    SECTION("Maximum values")
    {
        ConnectionId conn{std::numeric_limits<NodeId>::max(),
                          std::numeric_limits<PortIndex>::max(),
                          std::numeric_limits<NodeId>::max() - 1,
                          std::numeric_limits<PortIndex>::max() - 1};

        CHECK(conn.outNodeId == std::numeric_limits<NodeId>::max());
        CHECK(conn.outPortIndex == std::numeric_limits<PortIndex>::max());
        CHECK(conn.inNodeId == std::numeric_limits<NodeId>::max() - 1);
        CHECK(conn.inPortIndex == std::numeric_limits<PortIndex>::max() - 1);
    }
}

TEST_CASE("ConnectionId JSON parsing is fallible", "[core][serialization]")
{
    ConnectionId const expected{1, 2, 3, 4};
    QJsonObject const validJson = toJson(expected);

    SECTION("toJson preserves the wire schema and round-trips")
    {
        CHECK(validJson.size() == 4);
        CHECK(validJson["outNodeId"] == 1);
        CHECK(validJson["outPortIndex"] == 2);
        CHECK(validJson["inNodeId"] == 3);
        CHECK(validJson["inPortIndex"] == 4);

        ConnectionId parsed{InvalidNodeId, InvalidPortIndex, InvalidNodeId, InvalidPortIndex};
        REQUIRE(tryFromJson(validJson, parsed));
        CHECK(parsed == expected);
    }

    SECTION("The final non-sentinel values round-trip")
    {
        ConnectionId const boundary{InvalidNodeId - 1,
                                    InvalidPortIndex - 1,
                                    InvalidNodeId - 1,
                                    InvalidPortIndex - 1};
        ConnectionId parsed{};
        REQUIRE(tryFromJson(toJson(boundary), parsed));
        CHECK(parsed == boundary);
    }

    SECTION("Every endpoint field is required and integer-typed")
    {
        QStringList const keys{
            "outNodeId",
            "outPortIndex",
            "inNodeId",
            "inPortIndex",
        };

        for (QString const &key : keys) {
            QJsonObject malformed = validJson;
            malformed[key] = "1";

            ConnectionId parsed{9, 8, 7, 6};
            INFO(key.toStdString());
            CHECK_FALSE(tryFromJson(malformed, parsed));
            CHECK(parsed == ConnectionId{9, 8, 7, 6});

            malformed = validJson;
            malformed.remove(key);
            CHECK_FALSE(tryFromJson(malformed, parsed));
            CHECK(parsed == ConnectionId{9, 8, 7, 6});
        }
    }

    SECTION("Out-of-range and sentinel endpoint values are rejected")
    {
        std::vector<double> const invalidValues{
            -1.0,
            1.5,
            static_cast<double>(InvalidNodeId),
            static_cast<double>(InvalidNodeId) + 1.0,
        };

        for (double const value : invalidValues) {
            QJsonObject malformed = validJson;
            malformed["outNodeId"] = value;

            ConnectionId parsed{9, 8, 7, 6};
            INFO(value);
            CHECK_FALSE(tryFromJson(malformed, parsed));
            CHECK(parsed == ConnectionId{9, 8, 7, 6});
        }

        QJsonObject malformed = validJson;
        malformed["outPortIndex"] = static_cast<double>(InvalidPortIndex);

        ConnectionId parsed{9, 8, 7, 6};
        CHECK_FALSE(tryFromJson(malformed, parsed));
        CHECK(parsed == ConnectionId{9, 8, 7, 6});
    }
}
