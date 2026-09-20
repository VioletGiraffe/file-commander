#pragma once

// Private to cimageviewerwindow.cpp: nothing else includes this header, so everything in it has internal linkage.

#include "compiler/compiler_warnings_control.h"
#include "utils/scoped_cursor.h"

DISABLE_COMPILER_WARNINGS
#include <QColorSpace>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QImage>
#include <QImageReader>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QScrollArea>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>
#include <QtMath>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <numeric>
#include <utility>
#include <vector>

namespace {

// Frames that share a delay, as one entry. Frame numbers are 1-based and inclusive.
struct DelayRun
{
	int firstFrame;
	int lastFrame;
	int delayMs;
};

// Empty unless the file holds more than one frame. Qt exposes a frame's delay only at that frame, and the GIF
// handler cannot seek, so every frame must be decoded to read them all.
[[nodiscard]] std::vector<int> readFrameDelays(const QString& imagePath)
{
	QImageReader reader(imagePath);
	if (!reader.supportsAnimation()) // True for every GIF, single-frame ones included
		return {};

	const ScopedWaitCursor waitCursor;

	std::vector<int> delaysMs;
	while (!reader.read().isNull())
		delaysMs.push_back(reader.nextImageDelay());

	if (delaysMs.size() <= 1)
		return {};

	return delaysMs;
}

[[nodiscard]] std::vector<DelayRun> collapseDelayRuns(const std::vector<int>& delaysMs)
{
	std::vector<DelayRun> runs;
	for (size_t i = 0; i < delaysMs.size(); ++i)
	{
		const int frameNumber = static_cast<int>(i) + 1;
		if (!runs.empty() && runs.back().delayMs == delaysMs[i])
			runs.back().lastFrame = frameNumber;
		else
			runs.push_back({ frameNumber, frameNumber, delaysMs[i] });
	}

	return runs;
}

// The whole width is the animation's duration, marked wherever the frame delay changes.
// Not marked per frame: at a few hundred frames the marks merge into a solid block.
class CFrameDelayBar final : public QWidget
{
public:
	// Requires a non-empty delaysMs, so the total is never zero.
	CFrameDelayBar(std::vector<int> delaysMs, QWidget* parent);

	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* e) override;

private:
	const std::vector<int> _delaysMs;
	const int _totalMs;
};

inline CFrameDelayBar::CFrameDelayBar(std::vector<int> delaysMs, QWidget* parent) :
	QWidget(parent),
	_delaysMs{ std::move(delaysMs) },
	_totalMs{ std::accumulate(_delaysMs.begin(), _delaysMs.end(), 0) }
{
	setSizePolicy(QSizePolicy::MinimumExpanding, QSizePolicy::Fixed);
}

inline QSize CFrameDelayBar::sizeHint() const
{
	return QSize{ 240, fontMetrics().height() };
}

inline void CFrameDelayBar::paintEvent(QPaintEvent*)
{
	QPainter painter{ this };
	const QRect bar = rect();

	painter.fillRect(bar, palette().color(QPalette::Highlight));

	painter.setPen(palette().color(QPalette::Base));
	int elapsedMs = 0;
	for (size_t i = 0; i + 1 < _delaysMs.size(); ++i) // The last frame ends at the right edge, which needs no mark
	{
		elapsedMs += _delaysMs[i];
		if (_delaysMs[i + 1] == _delaysMs[i])
			continue;

		const int x = bar.x() + qRound((qreal)bar.width() * elapsedMs / _totalMs);
		painter.drawLine(x, bar.top(), x, bar.bottom());
	}

	painter.setPen(palette().color(QPalette::Mid));
	painter.drawRect(bar.adjusted(0, 0, -1, -1));
}

class CImageInfoDialog final : public QDialog
{
public:
	// image is the frame on screen; imagePath is re-read for the facts only the file carries.
	CImageInfoDialog(const QString& imagePath, const QImage& image, QWidget* parent);

private:
	QLabel* addRow(QFormLayout* form, const QString& label, const QString& value); // Returns the value label
};

inline QLabel* CImageInfoDialog::addRow(QFormLayout* form, const QString& label, const QString& value)
{
	QLabel* valueLabel = new QLabel(value, this);
	valueLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
	form->addRow(label, valueLabel);
	return valueLabel;
}

inline CImageInfoDialog::CImageInfoDialog(const QString& imagePath, const QImage& image, QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Image info"));

	const std::vector<int> delaysMs = readFrameDelays(imagePath);
	const int frameCount = delaysMs.empty() ? 1 : static_cast<int>(delaysMs.size());
	const qint64 fileSize = QFileInfo(imagePath).size();

	QFormLayout* fileForm = new QFormLayout;

	addRow(fileForm, tr("Format"), QString::fromLatin1(QImageReader(imagePath).format()).toUpper());
	addRow(fileForm, tr("File size"), QLocale{}.formattedDataSize(fileSize));
	addRow(fileForm, tr("Dimensions"), tr("%1 x %2 (%3 MP)").arg(image.width()).arg(image.height()).
		arg(image.width() * image.height() * 1e-6, 0, 'f', 1));
	const int channelCount = image.isGrayscale() ? 1 : (3 + (image.hasAlphaChannel() ? 1 : 0));
	addRow(fileForm, tr("Pixel format"), tr("%1, %2 bits per pixel, %3 bits per channel%4").
		arg(channelCount == 1 ? tr("1 channel") : tr("%1 channels").arg(channelCount)).
		arg(image.bitPlaneCount()).
		arg(image.bitPlaneCount() / channelCount).
		arg(image.hasAlphaChannel() ? tr(", with transparency") : QString{}));

	if (const QColorSpace colorSpace = image.colorSpace(); colorSpace.isValid())
		addRow(fileForm, tr("Color space"), colorSpace.description());

	if (fileSize > 0)
	{
		// The file holds every frame, so its size is spread over the pixels of all of them.
		const double encodedPixels = (double)image.width() * image.height() * frameCount;
		const double compressedBitsPerPixel = 8.0 * (double)fileSize / encodedPixels;

		addRow(fileForm, tr("Compressed size"), tr("%1 bits/pixel").arg(compressedBitsPerPixel, 0, 'f', 2));
		addRow(fileForm, tr("Compression ratio"), tr("%1:1 (vs. uncompressed %2 bpp)").
			arg(image.bitPlaneCount() / compressedBitsPerPixel, 0, 'f', 1).arg(image.bitPlaneCount()));
	}

	QLabel* pathLabel = addRow(fileForm, tr("Path"), imagePath);
	// Wrapped: a deep path breaks after each separator.
	// Capped: QLabel never breaks inside one component, whose width would otherwise be the dialog's minimum.
	pathLabel->setWordWrap(true);
	pathLabel->setMaximumWidth(fontMetrics().horizontalAdvance(u'x') * 80); // The width QTextDocument wraps to

	QVBoxLayout* layout = new QVBoxLayout(this);
	layout->addLayout(fileForm);

	if (!delaysMs.empty())
	{
		const int totalMs = std::accumulate(delaysMs.begin(), delaysMs.end(), 0);
		const std::vector<DelayRun> runs = collapseDelayRuns(delaysMs);

		QGroupBox* animationGroup = new QGroupBox(tr("Animation"), this);
		QFormLayout* animationForm = new QFormLayout(animationGroup);

		addRow(animationForm, tr("Frames"), QString::number(frameCount));
		addRow(animationForm, tr("Duration"), tr("%1 s").arg(totalMs / 1000.0, 0, 'f', 2));
		// The delays Qt reports, which are what actually plays: it clamps anything under 20 ms up to 100 ms.
		addRow(animationForm, tr("Average rate"), tr("%1 FPS").arg(frameCount * 1000.0 / totalMs, 0, 'f', 1));

		// Reported, not honored: the viewer always loops forever.
		const int loopCount = QImageReader(imagePath).loopCount();
		const QString loops = loopCount < 0 ? tr("forever") : (loopCount == 0 ? tr("once") : tr("%1 times").arg(loopCount));
		addRow(animationForm, tr("Loops"), loops);

		if (runs.size() == 1)
			addRow(animationForm, tr("Frame delay"), tr("%1 ms").arg(runs.front().delayMs));
		else
		{
			const auto [shortest, longest] = std::minmax_element(delaysMs.begin(), delaysMs.end());
			addRow(animationForm, tr("Frame delay"), tr("%1 - %2 ms").arg(*shortest).arg(*longest));

			animationForm->addRow(new CFrameDelayBar{ delaysMs, animationGroup });

			QStringList breakdown;
			for (const DelayRun& run : runs)
			{
				breakdown += run.firstFrame == run.lastFrame
					? tr("frame %1: %2 ms").arg(run.firstFrame).arg(run.delayMs)
					: tr("frames %1-%2: %3 ms").arg(run.firstFrame).arg(run.lastFrame).arg(run.delayMs);
			}

			QLabel* breakdownLabel = new QLabel(breakdown.join('\n'), animationGroup);
			breakdownLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);

			// Scrolled rather than shown whole: a file whose every frame differs would otherwise size the dialog off-screen.
			QScrollArea* breakdownArea = new QScrollArea(animationGroup);
			breakdownArea->setWidget(breakdownLabel);
			breakdownArea->setWidgetResizable(true);
			breakdownArea->setFrameShape(QFrame::NoFrame);
			breakdownArea->setMaximumHeight(fontMetrics().height() * 8);
			animationForm->addRow(breakdownArea);
		}

		layout->addWidget(animationGroup);
	}

	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(buttons);
}

} // namespace
