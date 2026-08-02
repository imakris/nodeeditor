#include "StyleNotifier.hpp"

namespace QtNodes {

StyleNotifier &StyleNotifier::instance()
{
    static StyleNotifier notifier;

    return notifier;
}

void StyleNotifier::notifyDefaultsChanged()
{
    Q_EMIT instance().defaultsChanged();
}

} // namespace QtNodes
