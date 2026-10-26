#pragma once

#include "PresetStore.hpp"
#include "util/LatestReadLoader.hpp"

namespace Presets {
struct ManifestSnapshot {
    QString root;
    QString id;
    PresetManifest manifest;
    QString problem;
    bool ok = false;
};

inline ManifestSnapshot readManifestSnapshot(const QString &root, const QString &id) {
    ManifestSnapshot snapshot;
    snapshot.root = root;
    snapshot.id = id;
    snapshot.manifest = loadManifest(root, id, &snapshot.ok, &snapshot.problem);
    return snapshot;
}

struct ManifestReadRequest {
    QString root;
    QString id;
    bool operator==(const ManifestReadRequest &) const = default;
};

class ManifestLoader final : public LatestReadLoader<ManifestReadRequest, ManifestSnapshot> {
    using Base = LatestReadLoader<ManifestReadRequest, ManifestSnapshot>;
public:
    using Reader = std::function<ManifestSnapshot(const QString &, const QString &)>;
    explicit ManifestLoader(QObject *parent, Reader reader = {})
        : Base(parent, [reader = reader ? std::move(reader) : Reader(readManifestSnapshot)](const ManifestReadRequest &request) {
            auto snapshot = reader(request.root, request.id);
            snapshot.root = request.root;
            snapshot.id = request.id;
            return snapshot;
        }) {}
    void load(const QString &root, const QString &id, Completed completed) {
        Base::load({root, id}, std::move(completed));
    }
};
} // namespace Presets
