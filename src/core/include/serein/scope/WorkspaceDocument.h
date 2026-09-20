#pragma once

#include "CoreExport.h"
#include "WorkspaceComponents.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QList>
#include <QtCore/QRectF>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUuid>

#include <optional>
#include <utility>

namespace serein::scope {

// A page is the serializable unit inside a workspace document.  Runtime
// widgets, serial sessions and sampled data never cross this boundary.
struct SEREIN_SCOPE_CORE_EXPORT WorkspaceDocumentPage {
    QString pageId;
    QString title;
    QList<WorkspaceComponentInstance> components;
    QJsonObject extensions;

    [[nodiscard]] QJsonObject toJsonObject() const;
    [[nodiscard]] bool validate(QString *error = nullptr) const;
    static std::optional<WorkspaceDocumentPage> fromJsonObject(
        const QJsonObject &object, QString *error = nullptr);
};

// Versioned, protocol-neutral workspace file model.  It is intentionally
// independent of QSettings: QSettings owns shell/dock preferences, while this
// model owns user-authored pages and component instances.
class SEREIN_SCOPE_CORE_EXPORT WorkspaceDocument final {
public:
    static constexpr int CurrentSchemaVersion = 1;

    WorkspaceDocument() = default;

    static WorkspaceDocument createDefault(
        const QString &title = QStringLiteral("Serein Workspace"),
        const QString &documentId = {});

    [[nodiscard]] int schemaVersion() const noexcept { return m_schemaVersion; }
    void setSchemaVersion(int value) noexcept { m_schemaVersion = value; }

    [[nodiscard]] const QString &documentId() const noexcept { return m_documentId; }
    void setDocumentId(QString value) { m_documentId = std::move(value); }

    [[nodiscard]] const QString &title() const noexcept { return m_title; }
    void setTitle(QString value) { m_title = std::move(value); }

    [[nodiscard]] const QString &targetProfile() const noexcept
    {
        return m_targetProfile;
    }
    void setTargetProfile(QString value) { m_targetProfile = std::move(value); }

    [[nodiscard]] QList<WorkspaceDocumentPage> &pages() noexcept { return m_pages; }
    [[nodiscard]] const QList<WorkspaceDocumentPage> &pages() const noexcept
    {
        return m_pages;
    }
    void setPages(QList<WorkspaceDocumentPage> value) { m_pages = std::move(value); }
    void addPage(WorkspaceDocumentPage page) { m_pages.append(std::move(page)); }
    void clearPages() { m_pages.clear(); }

    [[nodiscard]] const QJsonObject &extensions() const noexcept { return m_extensions; }
    void setExtensions(QJsonObject value) { m_extensions = std::move(value); }

    [[nodiscard]] QJsonObject toJsonObject() const;
    [[nodiscard]] QJsonDocument toJsonDocument() const;

    // Parsing replaces no state until validation succeeds.
    bool setJsonObject(const QJsonObject &object, QString *error = nullptr);
    bool setJsonDocument(const QJsonDocument &document, QString *error = nullptr);
    [[nodiscard]] bool validate(QString *error = nullptr) const;

    // Returns definition IDs that are not present in the supplied registry.
    // The document remains unchanged so callers can render an unavailable
    // placeholder and later recover it after installing the provider.
    [[nodiscard]] QStringList missingDefinitionIds(
        const QStringList &availableDefinitionIds) const;

    bool loadFromFile(const QString &filePath, QString *error = nullptr);
    bool saveToFileAtomic(const QString &filePath, QString *error = nullptr) const;

    static std::optional<WorkspaceDocument> fromFile(
        const QString &filePath, QString *error = nullptr);

private:
    static std::optional<WorkspaceDocument> fromJsonObject(
        const QJsonObject &object, QString *error = nullptr);

    int m_schemaVersion = CurrentSchemaVersion;
    QString m_documentId;
    QString m_title;
    QString m_targetProfile = QStringLiteral("generic");
    QList<WorkspaceDocumentPage> m_pages;
    QJsonObject m_extensions;
};

} // namespace serein::scope
