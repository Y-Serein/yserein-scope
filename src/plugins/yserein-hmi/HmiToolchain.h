#pragma once

#include "HmiProjectModel.h"

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>

#include <optional>

namespace serein::scope::yserein_hmi {

// Target-neutral, local-only toolchain used to close the authoring flow before
// a board SDK is selected.  Its artifacts are explicitly tagged simulator and
// must never be treated as D133/D211/ESP32 firmware images.
class HmiToolchain final {
public:
    enum class MessageType : quint8 {
        Hello = 1,
        SetBinding = 2,
        Navigate = 3,
        SubmitInput = 4,
        Ack = 0x80,
        Error = 0x7f,
    };

    struct Frame {
        quint16 sequence = 0;
        MessageType type = MessageType::Hello;
        QByteArray payload;
    };

    struct BuildResult {
        QString outputDirectory;
        QString manifestPath;
        QString contentHash;
        int resourceCount = 0;
    };

    static bool buildSimulatorTarget(const HmiProjectDocument &document,
                                     const QString &outputDirectory,
                                     BuildResult *result = nullptr,
                                     QString *error = nullptr);
    static bool validateSimulatorTarget(const QString &directory,
                                        QJsonObject *manifest = nullptr,
                                        QString *error = nullptr);
    static bool exerciseRuntime(const QString &directory,
                                QJsonObject *report = nullptr,
                                QString *error = nullptr);
    static bool installToSimulatorDevice(const QString &sourceDirectory,
                                         const QString &deviceRoot,
                                         QString *installedDirectory = nullptr,
                                         QString *error = nullptr);

    static QByteArray encodeFrame(const Frame &frame);
    static std::optional<Frame> decodeFrame(const QByteArray &bytes,
                                            QString *error = nullptr);

private:
    static quint16 crc16(const QByteArray &bytes);
};

} // namespace serein::scope::yserein_hmi
