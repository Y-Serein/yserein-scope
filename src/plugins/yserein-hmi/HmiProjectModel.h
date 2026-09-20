#pragma once

#include "ControlModel.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>

#include <optional>
#include <utility>

namespace serein::scope::yserein_hmi {

class Change;
struct ControlAdds;
struct ControlRemoves;
struct ControlUpdates;
struct ControlMoves;
struct PageAdds;
struct PageRemoves;
struct PageRenames;

// Offline, protocol-neutral representation of one HMI project.  Runtime
// pointers, serial sessions, sampled values and target-generated addresses do
// not belong to this type.  Controls are typed (ControlModel) and their
// authorable attributes are described by the ControlSchema registry, so field
// rules, defaults and Inspector metadata all derive from a single table.
class HmiProjectModel final {
public:
    // Version 2 introduced typed controls (ControlModel) plus a migration chain.
    // Version 1 stored controls as opaque JSON objects.
    static constexpr int CurrentSchemaVersion = 2;
    static constexpr int MinimumSupportedSchemaVersion = 1;

    struct Page {
        QString pageId;
        QString name;
        QList<ControlModel> controls;

        // Unknown page fields are retained to make a load/save round trip
        // lossless when a newer component adds page metadata.
        QJsonObject extra;

        [[nodiscard]] QJsonObject toJsonObject() const;
        [[nodiscard]] bool validate(QString *error = nullptr) const;
        static std::optional<Page> fromJsonObject(const QJsonObject &object,
                                                  QString *error = nullptr);
    };

    // One registered project resource.  The model deliberately stores only the
    // stable identity, its semantic kind and the authoring-time source; it does
    // not hold target Flash offsets, color depth or generated symbols.
    struct Asset {
        QString assetId;
        QString kind;
        QString source;
        QJsonObject extra;

        [[nodiscard]] QJsonObject toJsonObject() const;
        [[nodiscard]] bool validate(QString *error = nullptr) const;
        static std::optional<Asset> fromJsonObject(const QJsonObject &object,
                                                   QString *error = nullptr);
    };

    HmiProjectModel() = default;

    // Creates a usable, empty project with one editable page.  The target
    // profile is a descriptive model value; no hardware/protocol defaults are
    // inferred here.
    static HmiProjectModel createDefault(
        const QString &targetProfile = QStringLiteral("generic"),
        const QString &projectId = {});

    // Replaces this model only when parsing and validation both succeed.
    static std::optional<HmiProjectModel> fromJsonObject(
        const QJsonObject &object, QString *error = nullptr);

    // Migration pipeline: takes a document JSON object at any supported schema
    // version and returns current-version JSON. Pure and side-effect free so it
    // can be exercised directly from smoke tests without touching the disk.
    // Returns nullopt when the document is malformed or an unhandled version is
    // encountered.
    static std::optional<QJsonObject> migrateToCurrentVersion(
        const QJsonObject &document, QString *error = nullptr);

    [[nodiscard]] int schemaVersion() const noexcept { return m_schemaVersion; }
    void setSchemaVersion(int value) noexcept { m_schemaVersion = value; }

    [[nodiscard]] const QString &projectId() const noexcept { return m_projectId; }
    void setProjectId(QString value) { m_projectId = std::move(value); }

    [[nodiscard]] const QString &targetProfile() const noexcept
    {
        return m_targetProfile;
    }
    void setTargetProfile(QString value) { m_targetProfile = std::move(value); }

    [[nodiscard]] QList<Page> &pages() noexcept { return m_pages; }
    [[nodiscard]] const QList<Page> &pages() const noexcept { return m_pages; }
    void setPages(QList<Page> value) { m_pages = std::move(value); }
    void clearPages() { m_pages.clear(); }
    void addPage(Page page) { m_pages.append(std::move(page)); }

    [[nodiscard]] QList<Asset> &assets() noexcept { return m_assets; }
    [[nodiscard]] const QList<Asset> &assets() const noexcept { return m_assets; }
    void setAssets(QList<Asset> value) { m_assets = std::move(value); }
    void clearAssets() { m_assets.clear(); }
    void addAsset(Asset asset) { m_assets.append(std::move(asset)); }
    bool removeAsset(const QString &assetId);
    [[nodiscard]] bool hasAsset(const QString &assetId) const;
    [[nodiscard]] const Asset *findAsset(const QString &assetId) const;

    // Extension fields are intentionally opaque.  They allow a workspace to
    // retain document-level UI state (for example an active page) without
    // making that state part of the cross-target project schema.
    [[nodiscard]] const QJsonObject &extensions() const noexcept { return m_extra; }
    void setExtensions(QJsonObject value) { m_extra = std::move(value); }

    [[nodiscard]] QJsonObject toJsonObject() const;
    [[nodiscard]] QJsonDocument toJsonDocument() const;

    bool setJsonObject(const QJsonObject &object, QString *error = nullptr);
    bool setJsonDocument(const QJsonDocument &document, QString *error = nullptr);

    [[nodiscard]] bool validate(QString *error = nullptr) const;

    // Read-only access by stable identity. Authoring edits must go through
    // applyChange so every mutation is recorded and reversible.
    [[nodiscard]] const Page *findPage(const QString &pageId) const;
    [[nodiscard]] const ControlModel *findControl(const QString &pageId,
                                                  const QString &instanceId) const;

    // Atomic, validated write entry for authoring edits. Every operation in
    // `change` is applied to a staged copy and committed only when the result
    // validates; on failure the model is untouched and *error names the reason.
    bool applyChange(const Change &change, QString *error = nullptr);

private:
    Page *findPageMut(const QString &pageId);
    ControlModel *findControlMut(const QString &pageId, const QString &instanceId);
    bool applyOne(const ControlAdds &op, QString *error);
    bool applyOne(const ControlRemoves &op, QString *error);
    bool applyOne(const ControlUpdates &op, QString *error);
    bool applyOne(const ControlMoves &op, QString *error);
    bool applyOne(const PageAdds &op, QString *error);
    bool applyOne(const PageRemoves &op, QString *error);
    bool applyOne(const PageRenames &op, QString *error);

    int m_schemaVersion = CurrentSchemaVersion;
    QString m_projectId;
    QString m_targetProfile;
    QList<Page> m_pages;
    QList<Asset> m_assets;
    QJsonObject m_extra;
};

// File/document facade kept separate from the model so DesignerCanvas and
// other views can exchange QJsonObject state without owning a file path.
class HmiProjectDocument final {
public:
    HmiProjectDocument() = default;
    explicit HmiProjectDocument(HmiProjectModel model)
        : m_model(std::move(model))
    {
    }

    static HmiProjectDocument createDefault(
        const QString &targetProfile = QStringLiteral("generic"),
        const QString &projectId = {});

    [[nodiscard]] HmiProjectModel &model() noexcept { return m_model; }
    [[nodiscard]] const HmiProjectModel &model() const noexcept { return m_model; }
    void setModel(HmiProjectModel model) { m_model = std::move(model); }

    [[nodiscard]] const QString &filePath() const noexcept { return m_filePath; }

    [[nodiscard]] QJsonObject toJsonObject() const { return m_model.toJsonObject(); }
    [[nodiscard]] QJsonDocument toJsonDocument() const
    {
        return m_model.toJsonDocument();
    }

    bool setJsonObject(const QJsonObject &object, QString *error = nullptr);
    bool setJsonDocument(const QJsonDocument &document, QString *error = nullptr);
    [[nodiscard]] bool validate(QString *error = nullptr) const
    {
        return m_model.validate(error);
    }

    bool loadFromFile(const QString &filePath, QString *error = nullptr);
    bool saveToFileAtomic(const QString &filePath, QString *error = nullptr);
    bool saveToFileAtomic(QString *error = nullptr);

    static std::optional<HmiProjectDocument> fromFile(
        const QString &filePath, QString *error = nullptr);

private:
    HmiProjectModel m_model = HmiProjectModel::createDefault();
    QString m_filePath;
};

} // namespace serein::scope::yserein_hmi
