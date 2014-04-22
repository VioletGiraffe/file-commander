#pragma once

#include <QStyle>

class QPainter;
class QWidget;
class QStyleOptionComplex;

class CFocusFrameStyle: public QStyle
{
public:
	CFocusFrameStyle() {}
	virtual void drawComplexControl(ComplexControl control, const QStyleOptionComplex * option, QPainter * painter, const QWidget * widget = 0) const;
	virtual void drawControl(ControlElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget = 0) const;
	virtual void drawItemPixmap(QPainter * painter, const QRect & rectangle, int alignment, const QPixmap & pixmap) const;
	virtual void drawItemText(QPainter * painter, const QRect & rectangle, int alignment, const QPalette & palette, bool enabled, const QString & text, QPalette::ColorRole textRole = QPalette::NoRole) const;
	virtual void drawPrimitive(PrimitiveElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget = 0) const;
	virtual QPixmap generatedIconPixmap(QIcon::Mode iconMode, const QPixmap & pixmap, const QStyleOption * option) const;
	virtual QStyle::SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex * option, const QPoint & position, const QWidget * widget = 0) const;
	virtual QRect itemPixmapRect(const QRect & rectangle, int alignment, const QPixmap & pixmap) const;
	virtual QRect itemTextRect(const QFontMetrics & metrics, const QRect & rectangle, int alignment, bool enabled, const QString & text) const;
	virtual int pixelMetric(PixelMetric metric, const QStyleOption * option = 0, const QWidget * widget = 0) const;
	virtual void polish(QWidget * widget);
	virtual void polish(QApplication * application);
	virtual void polish(QPalette & palette);
	virtual QSize sizeFromContents(ContentsType type, const QStyleOption * option, const QSize & contentsSize, const QWidget * widget = 0) const;
	virtual QPalette standardPalette() const;
	virtual int styleHint(StyleHint hint, const QStyleOption * option = 0, const QWidget * widget = 0, QStyleHintReturn * returnData = 0) const;
	virtual QRect subControlRect(ComplexControl control, const QStyleOptionComplex * option, SubControl subControl, const QWidget * widget = 0) const;
	virtual QRect subElementRect(SubElement element, const QStyleOption * option, const QWidget * widget = 0) const;
	virtual QPixmap standardPixmap(QStyle::StandardPixmap pixmap, const QStyleOption* option, const QWidget*widget = 0) const;
	virtual void unpolish(QWidget * widget);
	virtual void unpolish(QApplication * application);
#if QT_VERSION >= QT_VERSION_CHECK (5,0,0)
	virtual QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption *option = 0, const QWidget *widget = 0) const;
	virtual int layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2, Qt::Orientation orientation, const QStyleOption *option = 0, const QWidget *widget = 0) const;
#endif
};
