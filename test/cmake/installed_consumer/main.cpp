#include <QtNodes/BasicGraphicsScene>
#include <QtNodes/ConnectionIdUtils>

#include <QJsonObject>

#include <type_traits>

int main()
{
    static_assert(std::is_destructible_v<QtNodes::BasicGraphicsScene>);

    QJsonObject const json{
        {"outNodeId", 3},
        {"outPortIndex", 4},
        {"inNodeId", 5},
        {"inPortIndex", 6},
    };
    QtNodes::ConnectionId connection_id{};
    if (!QtNodes::tryFromJson(json, connection_id)) {
        return 1;
    }
    return connection_id == QtNodes::ConnectionId{3, 4, 5, 6} ? 0 : 2;
}
