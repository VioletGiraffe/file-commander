#include "cvolumeusagebar.h"

#include "themes/theme.h"

// Submodule includes
#include "theme/colorutils.h"


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
	if (volumeSize == _volumeSize && freeSize == _freeSize)
		return;

	_volumeSize = volumeSize;
	_freeSize = freeSize;
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
	// Not borderSubtle: the bar's end must stand out from the background
	painter.setBrush(ColorUtils::mix(palette.border, palette.borderStrong, 0.5f));
	painter.drawPath(trough);

	if (_freeSize == _volumeSize)
		return;

	const qreal fillWidth = bar.width() * (qreal)(_volumeSize - _freeSize) / (qreal)_volumeSize;
	const qreal fillRadius = std::min(radius, fillWidth / 2);
	QPainterPath fill;
	fill.addRoundedRect(QRectF{ bar.topLeft(), QSizeF{ fillWidth, bar.height() } }, fillRadius, fillRadius);
	const bool lowOnSpace = _freeSize < std::min(_volumeSize / 10, lowSpaceCap);
	painter.setBrush(lowOnSpace ? palette.errorText : _shownAsActive ? palette.accent : palette.textDim);
	painter.drawPath(fill.intersected(trough));
}
