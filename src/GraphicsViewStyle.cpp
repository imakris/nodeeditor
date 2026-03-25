#include "GraphicsViewStyle.hpp"

#include "StyleCollection.hpp"

#include <QtCore/QJsonObject>

using QtNodes::GraphicsViewStyle;
using namespace QtNodes::detail;

inline void initResources()
{
    Q_INIT_RESOURCE(resources);
}

GraphicsViewStyle::GraphicsViewStyle()
{
    // Explicit resources inialization for preventing the static initialization
    // order fiasco: https://isocpp.org/wiki/faq/ctors#static-init-order
    initResources();

    // This configuration is stored inside the compiled unit and is loaded statically
    loadJsonFile(":DefaultStyle.json");
}

GraphicsViewStyle::GraphicsViewStyle(QString jsonText)
{
    loadJsonText(jsonText);
}

void GraphicsViewStyle::setStyle(QString jsonText)
{
    GraphicsViewStyle style(jsonText);

    StyleCollection::setGraphicsViewStyle(style);
}

void GraphicsViewStyle::loadJson(QJsonObject const &json)
{
    QJsonObject obj = json["GraphicsViewStyle"].toObject();

    readColor(obj, "BackgroundColor", BackgroundColor);
    readColor(obj, "FineGridColor", FineGridColor);
    readColor(obj, "CoarseGridColor", CoarseGridColor);
}

QJsonObject GraphicsViewStyle::toJson() const
{
    QJsonObject obj;

    writeColor(obj, "BackgroundColor", BackgroundColor);
    writeColor(obj, "FineGridColor", FineGridColor);
    writeColor(obj, "CoarseGridColor", CoarseGridColor);

    QJsonObject root;
    root["GraphicsViewStyle"] = obj;

    return root;
}
