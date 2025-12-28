#include "ctexteditwithimagesupport.h"

#include <QApplication>
#include <QDebug>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QProgressDialog>
#include <QTimer>

struct CTextEditWithImageSupport::Downloader final : QObject {
	using QObject::QObject;

	QByteArray get(const QUrl& url) {
		QNetworkRequest request(url);
		request.setRawHeader("User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64; rv:135.0) Gecko/20100101 Firefox/135.0");
		QPointer<QNetworkReply> reply = nam.get(request);
		qInfo() << "Fetching remote URL" << url.toString();

		QEventLoop loop;
		connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		connect(reply, &QNetworkReply::errorOccurred, &loop, &QEventLoop::quit);
		QTimer::singleShot(1000, &loop, &QEventLoop::quit); // timeout

		// Show indefinite progress
		QProgressDialog progressDialog;
		progressDialog.setWindowModality(Qt::WindowModal);
		progressDialog.setLabelText(tr("Loading image %1...").arg(url.toString()));
		progressDialog.setCancelButton(nullptr);
		progressDialog.setRange(0, 0);
		progressDialog.show();

		loop.exec(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents);

		if (!reply)
			return {};

		reply->deleteLater();
		return reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
	}

	void onErrorOccurred() {
		QNetworkReply* reply = qobject_cast<QNetworkReply*>(sender());
		if (reply)
			qWarning() << "Failed to download" << reply->url().toString() << ":" << reply->errorString();
	}

	QNetworkAccessManager nam;
};

QVariant CTextEditWithImageSupport::loadResource(int type, const QUrl& name)
{
	if (name.isLocalFile())
		return CTextEditWithLineNumbers::loadResource(type, name);

	if (!_downloader)
		_downloader = new Downloader{ this };

	return _downloader->get(name);
}
