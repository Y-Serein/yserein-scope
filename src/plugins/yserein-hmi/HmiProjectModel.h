#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QString>

#include <optional>
#include <utility>

namespace serein::scope::yserein_hmi {

// Offline, protocol-neutral representation of one HMI project.  Runtime
// pointers, serial sessions, sampled values and target-generated addresses do
// not belong to this type.  The controls array is intentionally opaque JSON:
// component definitions own their state schema and may add fields without
// requiring a project-model migration.
class HmiProjectModel final {
public:
    static constexpr int CurrentSchemaVersion = 1;

    struct Page {
        QString pageId;
        QString name;
        QJsonArray controls;

        // Unknown page fields are retained to make a load/save round trip
        // lossless when a newer component adds page metadata.
        QJsonObject extra;

        [[nodiscard]] QJsonObject toJsonObject() const;
        [[nodiscard]] bool validate(QString *error = nullptr) const;
        static std::optional<Page> fromJsonObject(const QJsonObject &object,
                                                  QString *error = nullptr);
    };

    HmiProjectModel() = default;

    // Creates a usable, empty project with one editable page.  The target
    // profile is a descriptive model value; no hardware/protocol defaults are
    // inferred here.
    static HmiProjectModel createDefault(
        const QString &targetProfile = QStringLiteral("generic"),
        const QString &projectId = {});

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

    // Extension fields are intentionally opaque.  They allow a workspace to
    // retain document-level UI state (for example an active page) without
    // making that state part of the cross-target project schema.
    [[nodiscard]] const QJsonObject &extensions() const noexcept { return m_extra; }
    void setExtensions(QJsonObject value) { m_extra = std::move(value); }

    [[nodiscard]] QJsonObject toJsonObject() const;
    [[nodiscard]] QJsonDocument toJsonDocument() const;

    // Replaces this model only when parsing and validation both succeed.
    bool setJsonObject(const QJsonObject &object, QString *error = nullptr);
    bool setJsonDocument(const QJsonDocument &document,
                         QString *error = nullptr);

    [[nodiscard]] bool validate(QString *error = nullptr) const;

private:
    int m_schemaVersion = CurrentSchemaVersion;
    QString m_projectId;
    QString m_targetProfile;
    QList<Page> m_pages;
    QJsonObject m_extra;

    static std::optional<HmiProjectModel> fromJsonObject(
        const QJsonObject &object, QString *error = nullptr);
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
    bool setJsonDocument(const QJsonDocument &document,
                         QString *error = nullptr);
    [[nodiscard]] bool validate(QString *error = nullptr) const
    {
        return m_model.validate(error);
    }

    // Loads and validates a JSON document.  The previous model and path are
    // retained if the operation fails.
    bool loadFromFile(const QString &filePath, QString *error = nullptr);

    // Writes through QSaveFile (temporary file + atomic commit).  The model is
    // validated before opening the destination.  The overload without a path
    // uses the path remembered by the last successful load/save.
    bool saveToFileAtomic(const QString &filePath, QString *error = nullptr);
    bool saveToFileAtomic(QString *error = nullptr);

    static std::optional<HmiProjectDocument> fromFile(
        const QString &filePath, QString *error = nullptr);

private:
    HmiProjectModel m_model = HmiProjectModel::createDefault();
    QString m_filePath;
};

} // namespace serein::scope::yserein_hmi
