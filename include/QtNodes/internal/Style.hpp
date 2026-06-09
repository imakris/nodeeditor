#pragma once

#include <QtCore/QDebug>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <algorithm>

namespace QtNodes {

class Style
{
public:
    virtual ~Style() = default;

public:
    virtual void loadJson(QJsonObject const &json) = 0;

    virtual QJsonObject toJson() const = 0;

    /// Loads from utf-8 byte array.
    virtual void loadJsonFromByteArray(QByteArray const &byteArray)
    {
        auto json = QJsonDocument::fromJson(byteArray).object();

        loadJson(json);
    }

    virtual void loadJsonText(QString jsonText) { loadJsonFromByteArray(jsonText.toUtf8()); }

    virtual void loadJsonFile(QString fileName)
    {
        QFile file(fileName);

        if (!file.open(QIODevice::ReadOnly)) {
            qWarning() << "Couldn't open file " << fileName;

            return;
        }

        loadJsonFromByteArray(file.readAll());
    }
};

namespace detail {

inline bool readColor(QJsonObject const &obj, QString const &key, QColor &color)
{
    if (!obj.contains(key))
        return false;

    QJsonValue value = obj[key];
    if (value.isArray()) {
        auto colorArray = value.toArray();
        if (colorArray.size() < 3 || !colorArray[0].isDouble() || !colorArray[1].isDouble()
            || !colorArray[2].isDouble()) {
            qWarning() << "Style key" << key << "is not a valid [r, g, b] array; keeping default";
            return false;
        }
        auto clamp8 = [](int v) { return std::clamp(v, 0, 255); };
        color = QColor(clamp8(colorArray[0].toInt()),
                       clamp8(colorArray[1].toInt()),
                       clamp8(colorArray[2].toInt()));
    } else {
        QColor parsed(value.toString());
        if (!parsed.isValid()) {
            qWarning() << "Style key" << key << "is not a valid color string; keeping default";
            return false;
        }
        color = parsed;
    }
    return true;
}

inline void writeColor(QJsonObject &obj, QString const &key, QColor const &color)
{
    obj[key] = color.name();
}

inline bool readFloat(QJsonObject const &obj, QString const &key, double &val)
{
    if (!obj.contains(key))
        return false;
    val = obj[key].toDouble();
    return true;
}

inline bool readFloat(QJsonObject const &obj, QString const &key, float &val)
{
    double tmp{};

    if (!readFloat(obj, key, tmp))
        return false;

    val = static_cast<float>(tmp);
    return true;
}

inline void writeFloat(QJsonObject &obj, QString const &key, double val)
{
    obj[key] = val;
}

inline bool readBool(QJsonObject const &obj, QString const &key, bool &val)
{
    if (!obj.contains(key))
        return false;
    val = obj[key].toBool();
    return true;
}

inline void writeBool(QJsonObject &obj, QString const &key, bool val)
{
    obj[key] = val;
}

} // namespace detail

} // namespace QtNodes
