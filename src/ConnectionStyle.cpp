#include "ConnectionStyle.hpp"

#include "StyleCollection.hpp"

#include <QtCore/QJsonObject>

#include <QDebug>

#include <random>

using QtNodes::ConnectionStyle;
using namespace QtNodes::detail;

inline void initResources()
{
    Q_INIT_RESOURCE(resources);
}

ConnectionStyle::ConnectionStyle()
{
    // Explicit resources inialization for preventing the static initialization
    // order fiasco: https://isocpp.org/wiki/faq/ctors#static-init-order
    initResources();

    // This configuration is stored inside the compiled unit and is loaded statically
    loadJsonFile(":DefaultStyle.json");
}

ConnectionStyle::ConnectionStyle(QString jsonText)
{
    loadJsonFile(":DefaultStyle.json");
    loadJsonText(jsonText);
}

void ConnectionStyle::setConnectionStyle(QString jsonText)
{
    ConnectionStyle style(jsonText);

    StyleCollection::setConnectionStyle(style);
}

void ConnectionStyle::loadJson(QJsonObject const &json)
{
    QJsonObject obj = json["ConnectionStyle"].toObject();

    readColor(obj, "ConstructionColor", ConstructionColor);
    readColor(obj, "NormalColor", NormalColor);
    readColor(obj, "SelectedColor", SelectedColor);
    readColor(obj, "SelectedHaloColor", SelectedHaloColor);
    readColor(obj, "HoveredColor", HoveredColor);

    readFloat(obj, "LineWidth", LineWidth);
    readFloat(obj, "ConstructionLineWidth", ConstructionLineWidth);
    readFloat(obj, "PointDiameter", PointDiameter);

    readBool(obj, "UseDataDefinedColors", UseDataDefinedColors);
}

QJsonObject ConnectionStyle::toJson() const
{
    QJsonObject obj;

    writeColor(obj, "ConstructionColor", ConstructionColor);
    writeColor(obj, "NormalColor", NormalColor);
    writeColor(obj, "SelectedColor", SelectedColor);
    writeColor(obj, "SelectedHaloColor", SelectedHaloColor);
    writeColor(obj, "HoveredColor", HoveredColor);

    writeFloat(obj, "LineWidth", LineWidth);
    writeFloat(obj, "ConstructionLineWidth", ConstructionLineWidth);
    writeFloat(obj, "PointDiameter", PointDiameter);

    writeBool(obj, "UseDataDefinedColors", UseDataDefinedColors);

    QJsonObject root;
    root["ConnectionStyle"] = obj;

    return root;
}

QColor ConnectionStyle::constructionColor() const
{
    return ConstructionColor;
}

QColor ConnectionStyle::normalColor() const
{
    return NormalColor;
}

QColor ConnectionStyle::normalColor(QString typeId) const
{
    std::size_t hash = qHash(typeId);

    std::size_t const hue_range = 0xFF;

    std::mt19937 gen(static_cast<unsigned int>(hash));
    std::uniform_int_distribution<int> distrib(0, hue_range);

    int hue = distrib(gen);
    int sat = 120 + hash % 129;

    return QColor::fromHsl(hue, sat, 160);
}

QColor ConnectionStyle::selectedColor() const
{
    return SelectedColor;
}

QColor ConnectionStyle::selectedHaloColor() const
{
    return SelectedHaloColor;
}

QColor ConnectionStyle::hoveredColor() const
{
    return HoveredColor;
}

float ConnectionStyle::lineWidth() const
{
    return LineWidth;
}

float ConnectionStyle::constructionLineWidth() const
{
    return ConstructionLineWidth;
}

float ConnectionStyle::pointDiameter() const
{
    return PointDiameter;
}

bool ConnectionStyle::useDataDefinedColors() const
{
    return UseDataDefinedColors;
}
