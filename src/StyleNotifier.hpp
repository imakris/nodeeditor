#pragma once

#include <QtCore/QObject>

namespace QtNodes {

/**
 * Announces that a process-wide default style was replaced.
 *
 * StyleCollection stores plain values, so installing a default cannot by itself
 * reach the objects that already resolved one: a node copies Opacity out of the
 * style into item state and, under the Crisp rasterization policy, renders from
 * a device-coordinate cache; a view copies the background colour into its brush
 * and caches the drawn background; and a group frame stores a rect computed from
 * its member nodes' style-dependent extents. Every StyleCollection setter emits
 * defaultsChanged(), and those objects re-resolve and invalidate what they
 * cached.
 *
 * GUI-thread only, like the StyleCollection state it reports on.
 *
 * Library-private: consumers install defaults through StyleCollection or the
 * Style classes, so nothing outside the library needs to emit or observe this.
 */
class StyleNotifier : public QObject
{
    Q_OBJECT
public:
    static StyleNotifier &instance();

    /// Emits defaultsChanged() on the single notifier.
    static void notifyDefaultsChanged();

Q_SIGNALS:
    void defaultsChanged();

private:
    StyleNotifier() = default;
};

} // namespace QtNodes
