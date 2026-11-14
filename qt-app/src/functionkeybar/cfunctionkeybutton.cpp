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

// Form::KeyBox
constexpr int keyBoxHPadding = 5, keyBoxVPadding = 1, keyBoxRadius = 4;
constexpr int keyToLabelGap = 8;

// Form::LabelCell
constexpr int plainKeyLeftPadding = 8, plainKeyRightPadding = 4;
constexpr int labelCellHPadding = 8, labelCellVPadding = 4;

} // namespace

void CFunctionKeyButton::setForm(const Form form)
{
	_form = form;
	updateGeometry();
	update();
}

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

QFont CFunctionKeyButton::keyBoxFont() const
{
	QFont smaller = font();
	smaller.setPointSizeF(QFontInfo{ smaller }.pointSizeF() * 0.9); // QFontInfo: a font sized in pixels has no point size of its own
	return smaller;
}

QFont CFunctionKeyButton::labelCellFont() const
{
	QFont bold = font();
	bold.setBold(true);
	return bold;
}

QSize CFunctionKeyButton::keyBoxSize() const
{
	const QFontMetrics metrics{ keyBoxFont() };
	return { metrics.horizontalAdvance(_key) + 2 * keyBoxHPadding, metrics.height() + 2 * keyBoxVPadding };
}

QSize CFunctionKeyButton::captionSize() const
{
	if (_form == Form::KeyBox)
	{
		const QSize keyBox = keyBoxSize();
		return { keyBox.width() + keyToLabelGap + fontMetrics().horizontalAdvance(_label), std::max(keyBox.height(), fontMetrics().height()) };
	}

	const QFontMetrics labelMetrics{ labelCellFont() };
	const int keyWidth = plainKeyLeftPadding + fontMetrics().horizontalAdvance(_key) + plainKeyRightPadding;
	return { keyWidth + labelMetrics.horizontalAdvance(_label) + 2 * labelCellHPadding, labelMetrics.height() + 2 * labelCellVPadding };
}

QSize CFunctionKeyButton::sizeHint() const
{
	ensurePolished();

	QStyleOptionButton option;
	initStyleOption(&option);
	return style()->sizeFromContents(QStyle::CT_PushButton, &option, captionSize(), this);
}

void CFunctionKeyButton::paintEvent(QPaintEvent* /*event*/)
{
	QStyleOptionButton option;
	initStyleOption(&option);

	QStylePainter painter{ this };
	painter.drawControl(QStyle::CE_PushButtonBevel, option);

	const QRect content = style()->subElementRect(QStyle::SE_PushButtonContents, &option, this);
	if (_form == Form::KeyBox)
		paintKeyBoxCaption(painter, content);
	else
		paintLabelCellCaption(painter, content);
}

void CFunctionKeyButton::paintKeyBoxCaption(QPainter& painter, const QRect& content) const
{
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

	painter.setFont(keyBoxFont());
	painter.setPen(keyColor);
	painter.drawText(keyRect, Qt::AlignCenter, _key);

	painter.setFont(font());
	painter.setPen(labelColor);
	painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, fontMetrics().elidedText(_label, Qt::ElideRight, labelWidth));
}

void CFunctionKeyButton::paintLabelCellCaption(QPainter& painter, const QRect& content) const
{
	const QPalette::ColorGroup group = isEnabled() ? QPalette::Active : QPalette::Disabled;
	const QColor accent = palette().color(group, QPalette::Accent);

	const int keyTextWidth = fontMetrics().horizontalAdvance(_key);
	const QRect keyRect{ content.left() + plainKeyLeftPadding, content.top(), keyTextWidth, content.height() };
	QRect cell = content;
	cell.setLeft(keyRect.right() + 1 + plainKeyRightPadding);

	painter.setPen(palette().color(group, QPalette::ButtonText));
	painter.drawText(keyRect, Qt::AlignLeft | Qt::AlignVCenter, _key);

	if (cell.width() <= 0)
		return;

	painter.fillRect(cell, accent);

	const QFont labelFont = labelCellFont();
	const QRect labelRect = cell.adjusted(labelCellHPadding, 0, -labelCellHPadding, 0);
	painter.setFont(labelFont);
	painter.setPen(ColorUtils::readableTextOn(accent, palette().color(group, QPalette::Base)));
	painter.drawText(labelRect, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics{ labelFont }.elidedText(_label, Qt::ElideRight, std::max(labelRect.width(), 0)));
}
