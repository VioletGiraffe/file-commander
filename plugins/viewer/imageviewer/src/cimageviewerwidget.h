#ifndef CIMAGEVIEWERWIDGET_H
#define CIMAGEVIEWERWIDGET_H

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QImage>
#include <QSize>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

class CImageViewerWidget : public QWidget
{
public:
	explicit CImageViewerWidget(QWidget *parent = 0);

public:
	bool displayImage(const QImage& image);
	bool displayImage(const QString& imagePath);
	QString imageInfoString() const;

	QSize sizeHint() const override;

	QIcon imageIcon(const QList<QSize>& sizes) const;

protected:
	void paintEvent(QPaintEvent* e) override;

private:
	QImage _sourceImage;
	QImage _scaledImage;

	QString _currentImageFormat;
	qint64 _currentImageFileSize = 0;
};

#endif // CIMAGEVIEWERWIDGET_H
