#pragma once
#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QStyle>
RESTORE_COMPILER_WARNINGS

class CFocusFrameStyle final : public QStyle
{
public:
	void drawComplexControl(ComplexControl control, const QStyleOptionComplex * option, QPainter * painter, const QWidget * widget = nullptr) const override;
	void drawControl(ControlElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget = nullptr) const override;
	void drawItemPixmap(QPainter * painter, const QRect & rectangle, int alignment, const QPixmap & pixmap) const override;
	void drawItemText(QPainter * painter, const QRect & rectangle, int alignment, const QPalette & palette, bool enabled, const QString & text, QPalette::ColorRole textRole = QPalette::NoRole) const override;
	void drawPrimitive(PrimitiveElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget = nullptr) const override;
	QPixmap generatedIconPixmap(QIcon::Mode iconMode, const QPixmap & pixmap, const QStyleOption * option) const override;
	QStyle::SubControl hitTestComplexControl(ComplexControl control, const QStyleOptionComplex * option, const QPoint & position, const QWidget * widget = nullptr) const override;
	QRect itemPixmapRect(const QRect & rectangle, int alignment, const QPixmap & pixmap) const override;
	QRect itemTextRect(const QFontMetrics & metrics, const QRect & rectangle, int alignment, bool enabled, const QString & text) const override;
	int pixelMetric(PixelMetric metric, const QStyleOption * option = nullptr, const QWidget * widget = nullptr) const override;
	void polish(QWidget * widget) override;
	void polish(QApplication * application) override;
	void polish(QPalette & palette) override;
	QSize sizeFromContents(ContentsType type, const QStyleOption * option, const QSize & contentsSize, const QWidget * widget = nullptr) const override;
	QPalette standardPalette() const override;
	int styleHint(StyleHint hint, const QStyleOption * option = nullptr, const QWidget * widget = nullptr, QStyleHintReturn * returnData = nullptr) const override;
	QRect subControlRect(ComplexControl control, const QStyleOptionComplex * option, SubControl subControl, const QWidget * widget = nullptr) const override;
	QRect subElementRect(SubElement element, const QStyleOption * option, const QWidget * widget = nullptr) const override;
	QPixmap standardPixmap(QStyle::StandardPixmap pixmap, const QStyleOption* option, const QWidget*widget = nullptr) const override;
	void unpolish(QWidget * widget) override;
	void unpolish(QApplication * application) override;
	QIcon standardIcon(StandardPixmap standardIcon, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
	int layoutSpacing(QSizePolicy::ControlType control1, QSizePolicy::ControlType control2, Qt::Orientation orientation, const QStyleOption *option = nullptr, const QWidget *widget = nullptr) const override;
};
