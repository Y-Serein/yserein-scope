#include "HmiToolchain.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QHash>
#include <QtCore/QSaveFile>
#include <QtCore/QSet>
#include <QtCore/QTemporaryDir>

#include <algorithm>

namespace serein::scope::yserein_hmi {
namespace {

constexpr char Magic0 = 'Y';
constexpr char Magic1 = 'S';
constexpr quint8 ProtocolVersion = 1;
constexpr qsizetype HeaderSize = 10;
constexpr qsizetype TrailerSize = 2;
constexpr quint32 MaxPayloadSize = 1024U * 1024U;

void setError(QString *error, const QString &message)
{
    if (error) *error = message;
}

QByteArray readFile(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("无法读取 %1：%2").arg(path, file.errorString()));
        return {};
    }
    return file.readAll();
}

bool writeFileAtomic(const QString &path, const QByteArray &bytes, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, QStringLiteral("无法写入 %1：%2").arg(path, file.errorString()));
        return false;
    }
    if (file.write(bytes) != bytes.size()) {
        setError(error, QStringLiteral("写入 %1 失败：%2").arg(path, file.errorString()));
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("提交 %1 失败：%2").arg(path, file.errorString()));
        return false;
    }
    return true;
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool safeRelativePath(const QString &path)
{
    const QString clean = QDir::cleanPath(path);
    return !clean.isEmpty() && !QDir::isAbsolutePath(clean)
        && clean != QStringLiteral("..")
        && !clean.startsWith(QStringLiteral("../"))
        && !clean.startsWith(QStringLiteral("..\\"));
}

void appendU16(QByteArray *bytes, const quint16 value)
{
    bytes->append(char((value >> 8) & 0xff));
    bytes->append(char(value & 0xff));
}

void appendU32(QByteArray *bytes, const quint32 value)
{
    bytes->append(char((value >> 24) & 0xff));
    bytes->append(char((value >> 16) & 0xff));
    bytes->append(char((value >> 8) & 0xff));
    bytes->append(char(value & 0xff));
}

quint16 readU16(const QByteArray &bytes, const qsizetype offset)
{
    return (quint16(quint8(bytes.at(offset))) << 8)
        | quint16(quint8(bytes.at(offset + 1)));
}

quint32 readU32(const QByteArray &bytes, const qsizetype offset)
{
    return (quint32(quint8(bytes.at(offset))) << 24)
        | (quint32(quint8(bytes.at(offset + 1))) << 16)
        | (quint32(quint8(bytes.at(offset + 2))) << 8)
        | quint32(quint8(bytes.at(offset + 3)));
}

bool knownMessageType(const quint8 type)
{
    return type == quint8(HmiToolchain::MessageType::Hello)
        || type == quint8(HmiToolchain::MessageType::SetBinding)
        || type == quint8(HmiToolchain::MessageType::Navigate)
        || type == quint8(HmiToolchain::MessageType::SubmitInput)
        || type == quint8(HmiToolchain::MessageType::Ack)
        || type == quint8(HmiToolchain::MessageType::Error);
}

} // namespace

bool HmiToolchain::buildSimulatorTarget(const HmiProjectDocument &document,
                                        const QString &outputDirectory,
                                        BuildResult *result, QString *error)
{
    if (!document.validate(error)) return false;
    const QString output = QFileInfo(QDir::cleanPath(outputDirectory.trimmed())).absoluteFilePath();
    if (output.isEmpty()) {
        setError(error, QStringLiteral("目标输出目录不能为空"));
        return false;
    }
    if (QFileInfo::exists(output)) {
        setError(error, QStringLiteral("目标输出目录已存在，未覆盖已有内容"));
        return false;
    }
    const QString parent = QFileInfo(output).absolutePath();
    if (!QDir().mkpath(parent)) {
        setError(error, QStringLiteral("无法创建目标父目录：%1").arg(parent));
        return false;
    }
    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".yserein-hmi-build-XXXXXX")));
    if (!staging.isValid()) {
        setError(error, QStringLiteral("无法创建目标暂存目录"));
        return false;
    }
    QDir outputDir(staging.path());
    if (!outputDir.mkpath(QStringLiteral("resources"))) {
        setError(error, QStringLiteral("无法创建目标资源目录"));
        return false;
    }

    const QJsonObject project = document.toJsonObject();
    const QByteArray projectBytes = QJsonDocument(project).toJson(QJsonDocument::Compact);
    if (!writeFileAtomic(outputDir.filePath(QStringLiteral("project.json")), projectBytes, error)) {
        return false;
    }

    QList<QJsonObject> sourceAssets;
    for (const QJsonValue &value : project.value(QStringLiteral("assets")).toArray()) {
        if (!value.isObject()) {
            setError(error, QStringLiteral("资源记录必须是对象"));
            return false;
        }
        sourceAssets.append(value.toObject());
    }
    std::sort(sourceAssets.begin(), sourceAssets.end(), [](const QJsonObject &left,
                                                            const QJsonObject &right) {
        return left.value(QStringLiteral("assetId")).toString()
            < right.value(QStringLiteral("assetId")).toString();
    });

    QSet<QString> referencedImages;
    for (const QJsonValue &pageValue : project.value(QStringLiteral("pages")).toArray()) {
        for (const QJsonValue &controlValue : pageValue.toObject()
                 .value(QStringLiteral("controls")).toArray()) {
            const QJsonObject control = controlValue.toObject();
            if (control.value(QStringLiteral("definitionId")).toString()
                == QStringLiteral("yserein-hmi.image")) {
                referencedImages.insert(control.value(QStringLiteral("properties"))
                    .toObject().value(QStringLiteral("assetId")).toString().trimmed());
            }
        }
    }

    QSet<QString> assetIds;
    QHash<QString, QString> assetKinds;
    QJsonArray resources;
    QByteArray contentSeed = projectBytes;
    const QString baseDirectory = document.filePath().isEmpty()
        ? QDir::currentPath() : QFileInfo(document.filePath()).absolutePath();
    for (const QJsonObject &asset : sourceAssets) {
        const QString assetId = asset.value(QStringLiteral("assetId")).toString().trimmed();
        const QString kind = asset.value(QStringLiteral("kind")).toString().trimmed();
        const QString source = asset.value(QStringLiteral("source")).toString().trimmed();
        if (assetId.isEmpty() || assetIds.contains(assetId) || kind.isEmpty() || source.isEmpty()) {
            setError(error, QStringLiteral("资源 ID、类型或源路径无效：%1").arg(assetId));
            return false;
        }
        const QString sourcePath = QDir::isAbsolutePath(source)
            ? QDir::cleanPath(source) : QDir(baseDirectory).absoluteFilePath(source);
        QString readError;
        const QByteArray bytes = readFile(sourcePath, &readError);
        if (bytes.isEmpty() && QFileInfo(sourcePath).size() != 0) {
            setError(error, readError);
            return false;
        }
        if (!QFileInfo::exists(sourcePath)) {
            setError(error, QStringLiteral("资源不存在：%1").arg(sourcePath));
            return false;
        }
        const QString digest = sha256(bytes);
        const QString suffix = QFileInfo(sourcePath).suffix().toLower();
        const QString relativePath = QStringLiteral("resources/%1-%2%3")
            .arg(digest, assetId,
                 suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix);
        if (!writeFileAtomic(outputDir.filePath(relativePath), bytes, error)) return false;
        resources.append(QJsonObject{
            {QStringLiteral("assetId"), assetId},
            {QStringLiteral("kind"), kind},
            {QStringLiteral("path"), relativePath},
            {QStringLiteral("sha256"), digest},
            {QStringLiteral("size"), bytes.size()},
        });
        assetIds.insert(assetId);
        assetKinds.insert(assetId, kind);
        contentSeed.append(QByteArray::fromHex(digest.toLatin1()));
    }
    for (const QString &assetId : std::as_const(referencedImages)) {
        if (assetId.isEmpty() || assetKinds.value(assetId) != QStringLiteral("image")) {
            setError(error, QStringLiteral("图片控件引用了未登记的图片资源：%1").arg(assetId));
            return false;
        }
    }

    const QString contentHash = sha256(contentSeed);
    const QJsonObject manifest{
        {QStringLiteral("format"), QStringLiteral("yserein-hmi-simulator-target")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("projectId"), document.model().projectId()},
        {QStringLiteral("targetProfile"), document.model().targetProfile()},
        {QStringLiteral("projectPath"), QStringLiteral("project.json")},
        {QStringLiteral("projectSha256"), sha256(projectBytes)},
        {QStringLiteral("contentHash"), contentHash},
        {QStringLiteral("resources"), resources},
        {QStringLiteral("hardwareImage"), false},
    };
    const QString manifestPath = outputDir.filePath(QStringLiteral("manifest.json"));
    if (!writeFileAtomic(manifestPath,
                         QJsonDocument(manifest).toJson(QJsonDocument::Indented), error)) {
        return false;
    }
    const QString stagingPath = staging.path();
    staging.setAutoRemove(false);
    if (!QDir().rename(stagingPath, output)) {
        staging.setAutoRemove(true);
        setError(error, QStringLiteral("无法原子发布目标目录：%1").arg(output));
        return false;
    }
    if (result) {
        result->outputDirectory = output;
        result->manifestPath = QDir(output).filePath(QStringLiteral("manifest.json"));
        result->contentHash = contentHash;
        result->resourceCount = resources.size();
    }
    return true;
}

bool HmiToolchain::validateSimulatorTarget(const QString &directory,
                                           QJsonObject *manifest, QString *error)
{
    const QDir root(QDir::cleanPath(directory.trimmed()));
    QString readError;
    const QByteArray manifestBytes = readFile(root.filePath(QStringLiteral("manifest.json")), &readError);
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(manifestBytes, &parseError);
    if (!parsed.isObject()) {
        setError(error, readError.isEmpty()
            ? QStringLiteral("目标清单无效：%1").arg(parseError.errorString()) : readError);
        return false;
    }
    const QJsonObject object = parsed.object();
    if (object.value(QStringLiteral("format")).toString()
            != QStringLiteral("yserein-hmi-simulator-target")
        || object.value(QStringLiteral("formatVersion")).toInt() != 1
        || object.value(QStringLiteral("hardwareImage")).toBool(true)) {
        setError(error, QStringLiteral("不是受支持的 Y-Serein HMI 本机目标包"));
        return false;
    }
    const QString projectPath = object.value(QStringLiteral("projectPath")).toString();
    if (!safeRelativePath(projectPath)) {
        setError(error, QStringLiteral("目标清单包含不安全的工程路径"));
        return false;
    }
    const QByteArray projectBytes = readFile(root.filePath(projectPath), error);
    if (sha256(projectBytes) != object.value(QStringLiteral("projectSha256")).toString()) {
        setError(error, QStringLiteral("目标工程哈希不匹配"));
        return false;
    }
    QByteArray contentSeed = projectBytes;
    QSet<QString> resourcePaths;
    for (const QJsonValue &value : object.value(QStringLiteral("resources")).toArray()) {
        const QJsonObject resource = value.toObject();
        const QString path = resource.value(QStringLiteral("path")).toString();
        if (!safeRelativePath(path) || resourcePaths.contains(path)) {
            setError(error, QStringLiteral("目标清单包含不安全或重复的资源路径"));
            return false;
        }
        const QByteArray bytes = readFile(root.filePath(path), error);
        const QString digest = sha256(bytes);
        if (digest != resource.value(QStringLiteral("sha256")).toString()
            || bytes.size() != resource.value(QStringLiteral("size")).toInteger()) {
            setError(error, QStringLiteral("资源校验失败：%1").arg(path));
            return false;
        }
        contentSeed.append(QByteArray::fromHex(digest.toLatin1()));
        resourcePaths.insert(path);
    }
    if (sha256(contentSeed) != object.value(QStringLiteral("contentHash")).toString()) {
        setError(error, QStringLiteral("目标包内容哈希不匹配"));
        return false;
    }
    if (manifest) *manifest = object;
    return true;
}

bool HmiToolchain::exerciseRuntime(const QString &directory, QJsonObject *report,
                                   QString *error)
{
    QJsonObject manifest;
    if (!validateSimulatorTarget(directory, &manifest, error)) return false;
    const QDir root(directory);
    QJsonParseError parseError;
    const QJsonDocument projectDocument = QJsonDocument::fromJson(
        readFile(root.filePath(manifest.value(QStringLiteral("projectPath")).toString()), error),
        &parseError);
    if (!projectDocument.isObject()) {
        setError(error, QStringLiteral("Runtime 无法读取工程：%1").arg(parseError.errorString()));
        return false;
    }
    const QJsonArray pages = projectDocument.object().value(QStringLiteral("pages")).toArray();
    if (pages.isEmpty()) {
        setError(error, QStringLiteral("Runtime 工程没有页面"));
        return false;
    }
    int controls = 0;
    QSet<QString> bindings;
    QSet<QString> pageIds;
    QSet<QString> inputIds;
    QString inputId;
    for (const QJsonValue &pageValue : pages) {
        pageIds.insert(pageValue.toObject().value(QStringLiteral("pageId")).toString());
        for (const QJsonValue &controlValue : pageValue.toObject()
                 .value(QStringLiteral("controls")).toArray()) {
            ++controls;
            const QJsonObject control = controlValue.toObject();
            const QString binding = control.value(QStringLiteral("properties")).toObject()
                                        .value(QStringLiteral("binding")).toString();
            if (!binding.isEmpty()) bindings.insert(binding);
            if (control.value(QStringLiteral("definitionId")).toString()
                == QStringLiteral("yserein-hmi.text-input")) {
                inputId = control.value(QStringLiteral("instanceId")).toString();
                inputIds.insert(inputId);
            }
        }
    }

    QList<Frame> frames;
    frames.append(Frame{1, MessageType::Hello, QByteArrayLiteral("simulator")});
    frames.append(Frame{2, MessageType::Navigate,
        pages.first().toObject().value(QStringLiteral("pageId")).toString().toUtf8()});
    if (!bindings.isEmpty()) {
        const QJsonObject update{{QStringLiteral("binding"), *bindings.cbegin()},
                                 {QStringLiteral("value"), 42.5}};
        frames.append(Frame{3, MessageType::SetBinding,
            QJsonDocument(update).toJson(QJsonDocument::Compact)});
    }
    if (!inputId.isEmpty()) {
        const QJsonObject submit{{QStringLiteral("instanceId"), inputId},
                                 {QStringLiteral("value"), QStringLiteral("runtime-smoke")}};
        frames.append(Frame{4, MessageType::SubmitInput,
            QJsonDocument(submit).toJson(QJsonDocument::Compact)});
    }
    QString currentPage;
    QJsonObject runtimeValues;
    QJsonObject submittedInputs;
    int appliedMessages = 0;
    for (const Frame &frame : frames) {
        QString frameError;
        const auto decoded = decodeFrame(encodeFrame(frame), &frameError);
        if (!decoded || decoded->sequence != frame.sequence || decoded->type != frame.type
            || decoded->payload != frame.payload) {
            setError(error, QStringLiteral("Runtime 协议回环失败：%1").arg(frameError));
            return false;
        }
        if (decoded->type == MessageType::Hello) {
            ++appliedMessages;
        } else if (decoded->type == MessageType::Navigate) {
            const QString pageId = QString::fromUtf8(decoded->payload);
            if (!pageIds.contains(pageId)) {
                setError(error, QStringLiteral("Runtime 收到不存在的页面：%1").arg(pageId));
                return false;
            }
            currentPage = pageId;
            ++appliedMessages;
        } else if (decoded->type == MessageType::SetBinding) {
            const QJsonObject update = QJsonDocument::fromJson(decoded->payload).object();
            const QString binding = update.value(QStringLiteral("binding")).toString();
            if (!bindings.contains(binding) || !update.contains(QStringLiteral("value"))) {
                setError(error, QStringLiteral("Runtime 收到无效的数据绑定写入"));
                return false;
            }
            runtimeValues.insert(binding, update.value(QStringLiteral("value")));
            ++appliedMessages;
        } else if (decoded->type == MessageType::SubmitInput) {
            const QJsonObject submit = QJsonDocument::fromJson(decoded->payload).object();
            const QString id = submit.value(QStringLiteral("instanceId")).toString();
            if (!inputIds.contains(id) || !submit.value(QStringLiteral("value")).isString()) {
                setError(error, QStringLiteral("Runtime 收到无效的文本提交"));
                return false;
            }
            submittedInputs.insert(id, submit.value(QStringLiteral("value")));
            ++appliedMessages;
        }
    }
    if (report) {
        *report = QJsonObject{
            {QStringLiteral("result"), QStringLiteral("pass")},
            {QStringLiteral("runtime"), QStringLiteral("local-simulator")},
            {QStringLiteral("protocolVersion"), ProtocolVersion},
            {QStringLiteral("frameRoundTrips"), frames.size()},
            {QStringLiteral("pages"), pages.size()},
            {QStringLiteral("controls"), controls},
            {QStringLiteral("bindings"), bindings.size()},
            {QStringLiteral("currentPage"), currentPage},
            {QStringLiteral("appliedMessages"), appliedMessages},
            {QStringLiteral("runtimeValues"), runtimeValues},
            {QStringLiteral("submittedInputs"), submittedInputs},
            {QStringLiteral("contentHash"), manifest.value(QStringLiteral("contentHash"))},
        };
    }
    return true;
}

bool HmiToolchain::installToSimulatorDevice(const QString &sourceDirectory,
                                            const QString &deviceRoot,
                                            QString *installedDirectory,
                                            QString *error)
{
    QJsonObject manifest;
    if (!validateSimulatorTarget(sourceDirectory, &manifest, error)) return false;
    const QString projectId = manifest.value(QStringLiteral("projectId")).toString();
    const QString hash = manifest.value(QStringLiteral("contentHash")).toString();
    if (projectId.isEmpty() || hash.isEmpty()) {
        setError(error, QStringLiteral("目标清单缺少工程 ID 或内容哈希"));
        return false;
    }
    const QString cleanDeviceRoot = QDir::cleanPath(deviceRoot.trimmed());
    if (cleanDeviceRoot.isEmpty() || cleanDeviceRoot == QStringLiteral(".")) {
        setError(error, QStringLiteral("模拟设备目录不能为空"));
        return false;
    }
    const QDir device(cleanDeviceRoot);
    const QString relativeInstall = QStringLiteral("yserein-hmi-device/%1/%2").arg(projectId, hash);
    const QString destination = device.filePath(relativeInstall);
    QDir destinationDir(destination);
    if (destinationDir.exists()) {
        if (validateSimulatorTarget(destination, nullptr, error)) {
            if (installedDirectory) *installedDirectory = destination;
            return true;
        }
        setError(error, QStringLiteral("模拟设备中已存在同名但无效的目标，未覆盖：%1").arg(destination));
        return false;
    }
    if (!QDir().mkpath(destinationDir.filePath(QStringLiteral("resources")))) {
        setError(error, QStringLiteral("无法创建模拟设备安装目录"));
        return false;
    }
    const QDir source(sourceDirectory);
    QStringList paths{QStringLiteral("project.json"), QStringLiteral("manifest.json")};
    for (const QJsonValue &value : manifest.value(QStringLiteral("resources")).toArray()) {
        paths.append(value.toObject().value(QStringLiteral("path")).toString());
    }
    for (const QString &path : paths) {
        if (!safeRelativePath(path)) {
            setError(error, QStringLiteral("拒绝安装不安全路径：%1").arg(path));
            return false;
        }
        const QByteArray bytes = readFile(source.filePath(path), error);
        const QString target = destinationDir.filePath(path);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())
            || !writeFileAtomic(target, bytes, error)) return false;
    }
    if (!validateSimulatorTarget(destination, nullptr, error)) return false;
    if (installedDirectory) *installedDirectory = destination;
    return true;
}

QByteArray HmiToolchain::encodeFrame(const Frame &frame)
{
    if (frame.payload.size() > MaxPayloadSize) return {};
    QByteArray bytes;
    bytes.reserve(HeaderSize + frame.payload.size() + TrailerSize);
    bytes.append(Magic0);
    bytes.append(Magic1);
    bytes.append(char(ProtocolVersion));
    bytes.append(char(quint8(frame.type)));
    appendU16(&bytes, frame.sequence);
    appendU32(&bytes, quint32(frame.payload.size()));
    bytes.append(frame.payload);
    appendU16(&bytes, crc16(bytes));
    return bytes;
}

std::optional<HmiToolchain::Frame> HmiToolchain::decodeFrame(const QByteArray &bytes,
                                                             QString *error)
{
    if (bytes.size() < HeaderSize + TrailerSize) {
        setError(error, QStringLiteral("协议帧过短"));
        return std::nullopt;
    }
    if (bytes.at(0) != Magic0 || bytes.at(1) != Magic1) {
        setError(error, QStringLiteral("协议帧魔数无效"));
        return std::nullopt;
    }
    if (quint8(bytes.at(2)) != ProtocolVersion) {
        setError(error, QStringLiteral("不支持的协议版本"));
        return std::nullopt;
    }
    const quint8 type = quint8(bytes.at(3));
    if (!knownMessageType(type)) {
        setError(error, QStringLiteral("不支持的消息类型"));
        return std::nullopt;
    }
    const quint32 payloadSize = readU32(bytes, 6);
    if (payloadSize > MaxPayloadSize
        || bytes.size() != HeaderSize + qsizetype(payloadSize) + TrailerSize) {
        setError(error, QStringLiteral("协议帧长度不匹配"));
        return std::nullopt;
    }
    const quint16 expected = readU16(bytes, bytes.size() - TrailerSize);
    if (crc16(bytes.first(bytes.size() - TrailerSize)) != expected) {
        setError(error, QStringLiteral("协议帧 CRC16 校验失败"));
        return std::nullopt;
    }
    return Frame{readU16(bytes, 4), MessageType(type),
                 bytes.mid(HeaderSize, payloadSize)};
}

quint16 HmiToolchain::crc16(const QByteArray &bytes)
{
    quint16 crc = 0xffff;
    for (const char byte : bytes) {
        crc ^= quint16(quint8(byte)) << 8;
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) != 0 ? quint16((crc << 1) ^ 0x1021)
                                      : quint16(crc << 1);
        }
    }
    return crc;
}

} // namespace serein::scope::yserein_hmi
