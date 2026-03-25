#include "NodeStyle.hpp"

#include "StyleCollection.hpp"

#include <QtCore/QJsonObject>

#include <QtCore/QDebug>

using QtNodes::NodeStyle;
using namespace QtNodes::detail;

inline void initResources()
{
    Q_INIT_RESOURCE(resources);
}

NodeStyle::NodeStyle()
{
    // Explicit resources inialization for preventing the static initialization
    // order fiasco: https://isocpp.org/wiki/faq/ctors#static-init-order
    initResources();

    // Initialize status icons after resources are loaded
    statusUpdated = QIcon(":/status_icons/updated.svg");
    statusProcessing = QIcon(":/status_icons/processing.svg");
    statusPending = QIcon(":/status_icons/pending.svg");
    statusInvalid = QIcon(":/status_icons/failed.svg");
    statusEmpty = QIcon(":/status_icons/empty.svg");
    statusPartial = QIcon(":/status_icons/partial.svg");

    // This configuration is stored inside the compiled unit and is loaded statically
    loadJsonFile(":DefaultStyle.json");
}

NodeStyle::NodeStyle(QString jsonText)
{
    loadJsonText(jsonText);
}

NodeStyle::NodeStyle(QJsonObject const &json)
{
    loadJson(json);
}

void NodeStyle::setNodeStyle(QString jsonText)
{
    NodeStyle style(jsonText);

    StyleCollection::setNodeStyle(style);
}

void NodeStyle::loadJson(QJsonObject const &json)
{
    QJsonObject obj = json["NodeStyle"].toObject();

    readColor(obj, "NormalBoundaryColor", NormalBoundaryColor);
    readColor(obj, "SelectedBoundaryColor", SelectedBoundaryColor);
    readColor(obj, "GradientColor0", GradientColor0);
    readColor(obj, "GradientColor1", GradientColor1);
    readColor(obj, "GradientColor2", GradientColor2);
    readColor(obj, "GradientColor3", GradientColor3);
    readColor(obj, "ShadowColor", ShadowColor);
    readBool(obj, "ShadowEnabled", ShadowEnabled);
    readColor(obj, "FontColor", FontColor);
    readColor(obj, "FontColorFaded", FontColorFaded);
    readColor(obj, "ConnectionPointColor", ConnectionPointColor);
    readColor(obj, "FilledConnectionPointColor", FilledConnectionPointColor);
    readColor(obj, "WarningColor", WarningColor);
    readColor(obj, "ErrorColor", ErrorColor);

    readFloat(obj, "PenWidth", PenWidth);
    readFloat(obj, "HoveredPenWidth", HoveredPenWidth);
    readFloat(obj, "ConnectionPointDiameter", ConnectionPointDiameter);

    readFloat(obj, "Opacity", Opacity);
}

QJsonObject NodeStyle::toJson() const
{
    QJsonObject obj;

    writeColor(obj, "NormalBoundaryColor", NormalBoundaryColor);
    writeColor(obj, "SelectedBoundaryColor", SelectedBoundaryColor);
    writeColor(obj, "GradientColor0", GradientColor0);
    writeColor(obj, "GradientColor1", GradientColor1);
    writeColor(obj, "GradientColor2", GradientColor2);
    writeColor(obj, "GradientColor3", GradientColor3);
    writeColor(obj, "ShadowColor", ShadowColor);
    writeBool(obj, "ShadowEnabled", ShadowEnabled);
    writeColor(obj, "FontColor", FontColor);
    writeColor(obj, "FontColorFaded", FontColorFaded);
    writeColor(obj, "ConnectionPointColor", ConnectionPointColor);
    writeColor(obj, "FilledConnectionPointColor", FilledConnectionPointColor);
    writeColor(obj, "WarningColor", WarningColor);
    writeColor(obj, "ErrorColor", ErrorColor);

    writeFloat(obj, "PenWidth", PenWidth);
    writeFloat(obj, "HoveredPenWidth", HoveredPenWidth);
    writeFloat(obj, "ConnectionPointDiameter", ConnectionPointDiameter);

    writeFloat(obj, "Opacity", Opacity);

    QJsonObject root;
    root["NodeStyle"] = obj;

    return root;
}

void NodeStyle::setBackgroundColor(QColor const &color)
{
    GradientColor0 = color;
    GradientColor1 = color;
    GradientColor2 = color;
    GradientColor3 = color;
}

QColor NodeStyle::backgroundColor() const
{
    return GradientColor0;
}
