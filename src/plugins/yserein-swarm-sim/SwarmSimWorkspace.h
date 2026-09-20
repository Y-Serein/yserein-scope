#pragma once

#include <QtWidgets/QWidget>

namespace serein::scope {
class IHostServices;
}

class QQuickView;
class ZSMApplication;

class SwarmSimWorkspace : public QWidget {
	Q_OBJECT
public:
	explicit SwarmSimWorkspace(serein::scope::IHostServices* host, QWidget* parent = nullptr);
	~SwarmSimWorkspace() override;

private:
	void onQmlError(const QString &description);

	serein::scope::IHostServices* m_host = nullptr;
	ZSMApplication* m_app = nullptr;
	QQuickView* m_view = nullptr;
	QWidget* m_container = nullptr;
};
