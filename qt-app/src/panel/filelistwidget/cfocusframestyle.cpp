#include "cfocusframestyle.h"
#include <QPen>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QApplication>

void CFocusFrameStyle::drawComplexControl(ComplexControl control, const QStyleOptionComplex * option, QPainter * painter, const QWidget * widget) const
{
	QApplication::style()->drawComplexControl(control, option, painter, widget);
}

void CFocusFrameStyle::drawControl(ControlElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget) const
{
	QApplication::style()->drawControl(element, option, painter, widget);
}

void CFocusFrameStyle::drawItemPixmap(QPainter * painter, const QRect & rectangle, int alignment, const QPixmap & pixmap) const
{
	QApplication::style()->drawItemPixmap(painter, rectangle, alignment, pixmap);
}

void CFocusFrameStyle::drawItemText(QPainter * painter, const QRect & rectangle, int alignment, const QPalette & palette, bool enabled, const QString & text, QPalette::ColorRole textRole) const
{
	QApplication::style()->drawItemText(painter, rectangle, alignment, palette, enabled, text, textRole);
}

void CFocusFrameStyle::drawPrimitive(PrimitiveElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget) const
{
	if (element == QStyle::PE_FrameFocusRect) {
		if (const auto fropt = qstyleoption_cast<const QStyleOptionFocusRect *>(option)) {
			QColor bg = fropt->backgroundColor;
			QPen oldPen = painter->pen();
			QPen newPen;
			if (bg.isValid()) {
				int h, s, v;
				bg.getHsv(&h, &s, &v);
				if (v >= 128)
					newPen.setColor(Qt::black);
				else
					newPen.setColor(Qt::white);
			} else {
				newPen.setColor(option->palette.foreground().color());
			}
			newPen.setWidth(0);
			newPen.setStyle(Qt::DotLine);
			painter->setPen(newPen);
			QRect focusRect = option->rect /*.adjusted(1, 1, -1, -1) */;
			painter->drawRect(focusRect.adjusted(0, 0, -1, -1)); //draw pen inclusive
			painter->setPen(oldPen);
		}
	} else
		QApplication::style()->drawPrimitive(element, option, painter, widget);
}

QPixmap CFocusFrameStyle::generatedIconPixmap(QIcon::Mode iconMode, const QPixmap & pixmap, const QStyleOption * option) const
{
	return QApplication::style()->generatedIconPixmap(iconMode, pixmap, option);
}

QStyle::SubControl CFocusFrameStyle::hitTestComplexControl(ComplexControl control, const QStyleOptionComplex * option, const QPoint & position, const QWidget * widget) const
{
	return QApplication::style()->hitTestComplexControl(control, option, position, widget);
}

QRect CFocusFrameStyle::itemPixmapRect(const QRect & rectangle, int alignment, const QPixmap & pixmap) const
{
	return QApplication::style()->itemPixmapRect(rectangle, alignment, pixmap);
}

QRect CFocusFrameStyle::itemTextRect(const QFontMetrics & metrics, const QRect & rectangle, int alignment, bool enabled, const QString & text) const
{
	return QApplication::style()->itemTextRect(metrics, rectangle, alignment, enabled, text);
}

int CFocusFrameStyle::pixelMetric(PixelMetric metric, const QStyleOption * option, const QWidget * widget) const
{
	return QApplication::style()->pixelMetric(metric, option, widget);
}

void CFocusFrameStyle::polish(QWidget * widget)
{
	QApplication::style()->polish(widget);
}

void CFocusFrameStyle::polish(QApplication * application)
{
	QApplication::style()->polish(application);
}

void CFocusFrameStyle::polish(QPalette & palette)
{
	QApplication::style()->polish(palette);
}

QSize CFocusFrameStyle::sizeFromContents(ContentsType type, const QStyleOption * option, const QSize & contentsSize, const QWidget * widget) const
{
	return QApplication::style()->sizeFromContents(type, option, contentsSize, widget);
}

QPalette CFocusFrameStyle::standardPalette() const
{
	return QApplication::style()->standardPalette();
}

int CFocusFrameStyle::styleHint(StyleHint hint, const QStyleOption * option, const QWidget * widget, QStyleHintReturn * returnData) const
{
	return QApplication::style()->styleHint(hint, option, widget, returnData);
}

QRect CFocusFrameStyle::subControlRect(ComplexControl control, const QStyleOptionComplex * option, SubControl subControl, const QWidget * widget) const
{
	return QApplication::style()->subControlRect(control, option, subControl, widget);
}

QRect CFocusFrameStyle::subElementRect(SubElement element, const QStyleOption * option, const QWidget * widget) const
{
	return QApplication::style()->subElementRect(element, option, widget);
}

QPixmap CFocusFrameStyle::standardPixmap(QStyle::StandardPixmap pixmap, const QStyleOption* option, const QWidget*widget) const
{
	return QApplication::style()->standardPixmap(pixmap, option, widget);
}

void CFocusFrameStyle::unpolish(QWidget * widget)
{
	QApplication::style()->unpolish(widget);
}

void CFocusFrameStyle::unpolish(QApplication * application)
{
	QApplication::style()->unpolish(application);
}

#if QT_VERSION >= QT_VERSION_CHECK (5,0,0)
QIcon CFocusFrameStyle::standardIcon(QStyle::StandardPixmap standardIcon, const QStyleOption *option, const QWidget *widget) const
{
	return QApplication::style()->standardIcon(standardIcon, option, widget);
}

int CFocusFrameStyle::layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2, Qt::Orientation orientation, const QStyleOption *option, const QWidget *widget) const
{
	return QApplication::style()->layoutSpacing(control1, control2, orientation, option, widget);
}
#endif
