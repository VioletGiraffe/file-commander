#include "cvolumeusagebar.h"

#include "themes/theme.h"


DISABLE_COMPILER_WARNINGS
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

constexpr QSize barSize{ 90, 8 };
// Low on space: under a tenth of the volume, and under this
constexpr uint64_t lowSpaceCap = 10ull * 1024 * 1024 * 1024;

}

CVolumeUsageBar::CVolumeUsageBar(QWidget* parent) :
	QWidget(parent)
{
	// Fixed: a box layout centres the bar vertically in its row
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void CVolumeUsageBar::setSpace(const uint64_t volumeSize, uint64_t freeSize)
{
	freeSize = std::min(freeSize, volumeSize);
	const double usedFraction = volumeSize > 0 ? (double)(volumeSize - freeSize) / (double)volumeSize : 0.0;
	const bool lowOnSpace = freeSize < std::min(volumeSize / 10, lowSpaceCap);
	if (usedFraction == _usedFraction && lowOnSpace == _lowOnSpace)
		return;

	_usedFraction = usedFraction;
	_lowOnSpace = lowOnSpace;
	update();
}

void CVolumeUsageBar::setShownAsActive(const bool active)
{
	if (active == _shownAsActive)
		return;

	_shownAsActive = active;
	update();
}

QSize CVolumeUsageBar::sizeHint() const
{
	return barSize;
}

void CVolumeUsageBar::paintEvent(QPaintEvent* /*event*/)
{
	const Theme& theme = appliedTheme();
	const CBasePalette& palette = theme.palette;

	QPainter painter{ this };
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);

	const QRectF bar{ rect() };
	const qreal radius = std::min<qreal>(theme.metrics.controlRadius, bar.height() / 2);
	QPainterPath trough;
	trough.addRoundedRect(bar, radius, radius);
	painter.setBrush(palette.borderSubtle);
	painter.drawPath(trough);

	if (_usedFraction <= 0.0)
		return;

	const qreal fillWidth = bar.width() * _usedFraction;
	const qreal fillRadius = std::min(radius, fillWidth / 2);
	QPainterPath fill;
	fill.addRoundedRect(QRectF{ bar.topLeft(), QSizeF{ fillWidth, bar.height() } }, fillRadius, fillRadius);
	painter.setBrush(_lowOnSpace ? palette.errorText : _shownAsActive ? palette.accent : palette.textDim);
	painter.drawPath(fill.intersected(trough));
}
