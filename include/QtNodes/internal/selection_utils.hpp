#pragma once

#include <QtWidgets/QGraphicsItem>
#include <QtWidgets/QGraphicsScene>

#include <vector>

namespace QtNodes::detail {

/// Iterates over `scene->selectedItems()` and invokes `body(typed)` for every
/// item that successfully casts to `T*` via `qgraphicsitem_cast`.
template<typename T, typename Body>
void for_each_selected(QGraphicsScene const* scene, Body&& body)
{
    for (QGraphicsItem* item : scene->selectedItems()) {
        if (auto* typed = qgraphicsitem_cast<T*>(item)) {
            body(typed);
        }
    }
}

/// Returns the subset of `scene->selectedItems()` that successfully cast to `T*`.
template<typename T>
std::vector<T*> selected_items_of_type(QGraphicsScene const* scene)
{
    std::vector<T*> result;
    QList<QGraphicsItem*> const graphicsItems = scene->selectedItems();
    result.reserve(graphicsItems.size());

    for (QGraphicsItem* item : graphicsItems) {
        if (auto* typed = qgraphicsitem_cast<T*>(item)) {
            result.push_back(typed);
        }
    }

    return result;
}

} // namespace QtNodes::detail
