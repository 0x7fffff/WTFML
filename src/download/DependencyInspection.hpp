#pragma once

#include "ModDependencies.hpp"

namespace ModDependencies {
// Read-only report over recognized metadata, never a full dependency solver.
QString inspectInstalledDependencies(const QList<InstalledItem> &selected,
                                     const QList<InstalledItem> &inventory);
}
