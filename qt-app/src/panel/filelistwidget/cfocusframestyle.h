#pragma once
#include "ui/CProxyStyle.h"

class CFocusFrameStyle : public CProxyStyle
{
public:
	void drawPrimitive(PrimitiveElement element, const QStyleOption * option, QPainter * painter, const QWidget * widget = nullptr) const override;
};

template <class ParentStyle>
class NoHoverHighlightStyle final : public ParentStyle {
public:
	void drawPrimitive(QStyle::PrimitiveElement element, const QStyleOption* option,
		QPainter* painter, const QWidget* widget = nullptr) const override {
		if ((element == QStyle::PE_PanelItemViewItem || element == QStyle::PE_PanelItemViewRow) &&
			option && qstyleoption_cast<const QStyleOptionViewItem*>(option)) {
			// Copy and strip hover state
			QStyleOptionViewItem opt = *static_cast<const QStyleOptionViewItem*>(option);
			opt.state &= ~QStyle::State_MouseOver;
			ParentStyle::drawPrimitive(element, &opt, painter, widget);
			return;
		}

		ParentStyle::drawPrimitive(element, option, painter, widget);
	}
};
