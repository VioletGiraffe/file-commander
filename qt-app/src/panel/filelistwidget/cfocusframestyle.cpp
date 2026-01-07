#include "cfocusframestyle.h"

#include <QPainter>
#include <QPen>
#include <QStyleOptionViewItem>

void CFocusFrameStyle::drawPrimitive(PrimitiveElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget) const
{
	if (element != QStyle::PE_FrameFocusRect) [[likely]]
	{
		CProxyStyle::drawPrimitive(element, option, painter, widget);
		return;
	}

	if (const auto fropt = qstyleoption_cast<const QStyleOptionFocusRect *>(option))
	{
		painter->setClipRect(option->rect);

		const QColor bg = fropt->backgroundColor;
		const QPen oldPen = painter->pen();

		QPen newPen;
		if (bg.isValid())
		{
			int h = 0, s = 0, l = 0;
			bg.getHsl(&h, &s, &l);
			newPen.setColor(l >= 128 ? QColor(70, 70, 70) : QColor(Qt::white));
		} else
			newPen.setColor(option->palette.windowText().color());

		newPen.setWidth(2);
		newPen.setStyle(Qt::DotLine);

		painter->setPen(newPen);
		painter->drawRect(option->rect); //draw pen inclusive
		painter->setPen(oldPen);

		painter->setClipRect(QRect(), Qt::NoClip);
	}
}
