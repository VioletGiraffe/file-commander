#include "cfunctionkeybutton.h"


// Submodule includes
#include "theme/colorutils.h"


DISABLE_COMPILER_WARNINGS
#include <QColor>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QPalette>
#include <QPen>
#include <QRect>
#include <QRectF>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStylePainter>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

constexpr int keyBoxHPadding = 5, keyBoxVPadding = 1, keyBoxRadius = 4;
constexpr int keyToLabelGap = 8;

} // namespace

void CFunctionKeyButton::setCaption(const QString& key, const QString& label)
{
	_key = key;
	setLabel(label);
}

void CFunctionKeyButton::setLabel(const QString& label)
{
	_label = label;
	setText(_key + QLatin1Char(' ') + _label); // not drawn: the accessible name comes from it

	updateGeometry();
	update();
}

const QString& CFunctionKeyButton::label() const
{
	return _label;
}

QFont CFunctionKeyButton::keyFont() const
{
	QFont smaller = font();
	smaller.setPointSizeF(QFontInfo{ smaller }.pointSizeF() * 0.9); // QFontInfo: a font sized in pixels has no point size of its own
	return smaller;
}

QSize CFunctionKeyButton::keyBoxSize() const
{
	const QFontMetrics metrics{ keyFont() };
	return { metrics.horizontalAdvance(_key) + 2 * keyBoxHPadding, metrics.height() + 2 * keyBoxVPadding };
}

QSize CFunctionKeyButton::sizeHint() const
{
	ensurePolished();

	const QSize keyBox = keyBoxSize();
	const QSize content{ keyBox.width() + keyToLabelGap + fontMetrics().horizontalAdvance(_label), std::max(keyBox.height(), fontMetrics().height()) };

	QStyleOptionButton option;
	initStyleOption(&option);
	return style()->sizeFromContents(QStyle::CT_PushButton, &option, content, this);
}

void CFunctionKeyButton::paintEvent(QPaintEvent* /*event*/)
{
	QStyleOptionButton option;
	initStyleOption(&option);

	QStylePainter painter{ this };
	painter.drawControl(QStyle::CE_PushButtonBevel, option);

	const QRect content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
	const QSize keyBox = keyBoxSize();
	// The label gives way to the key: it is elided in a button too narrow for both
	const int labelWidth = std::clamp(content.width() - keyBox.width() - keyToLabelGap, 0, fontMetrics().horizontalAdvance(_label));
	const int captionWidth = keyBox.width() + keyToLabelGap + labelWidth;

	const QRect keyRect{ content.left() + (content.width() - captionWidth) / 2, content.top() + (content.height() - keyBox.height()) / 2, keyBox.width(), keyBox.height() };
	const QRect labelRect{ keyRect.right() + 1 + keyToLabelGap, content.top(), labelWidth, content.height() };

	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const QColor labelColor = palette().color(group, QPalette::ButtonText);
	const QColor keyColor = palette().color(group, QPalette::PlaceholderText);

	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen{ ColorUtils::mix(palette().color(group, QPalette::Base), labelColor, 0.25f), 1.0 });
	painter.setBrush(Qt::NoBrush);
	// Inset by half the pen: the stroke stays inside the box
	painter.drawRoundedRect(QRectF{ keyRect }.adjusted(0.5, 0.5, -0.5, -0.5), keyBoxRadius, keyBoxRadius);

	painter.setFont(keyFont());
	painter.setPen(keyColor);
	painter.drawText(keyRect, Qt::AlignCenter, _key);

	painter.setFont(font());
	painter.setPen(labelColor);
	painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(_label, Qt::ElideRight, labelWidth));
}
