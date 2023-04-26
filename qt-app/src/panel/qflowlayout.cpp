#include "qflowlayout.h"

DISABLE_COMPILER_WARNINGS
#include <QWidget>
RESTORE_COMPILER_WARNINGS

FlowLayout::FlowLayout(QWidget *parent, int margin, int hSpacing, int vSpacing)
	: QLayout(parent), m_hSpace(hSpacing), m_vSpace(vSpacing)
{
	setContentsMargins(margin, margin, margin, margin);
}

FlowLayout::~FlowLayout()
{
	QLayoutItem *item = nullptr;
	while ((item = takeAt(0)) != nullptr)
		delete item;
}

void FlowLayout::addItem(QLayoutItem *item)
{
	m_itemList.append(item);
}

int FlowLayout::horizontalSpacing() const
{
	if (m_hSpace >= 0) {
		return m_hSpace;
	} else {
		return smartSpacing(QStyle::PM_LayoutHorizontalSpacing);
	}
}

int FlowLayout::verticalSpacing() const
{
	if (m_vSpace >= 0) {
		return m_vSpace;
	} else {
		return smartSpacing(QStyle::PM_LayoutVerticalSpacing);
	}
}

int FlowLayout::count() const
{
	return static_cast<int>(m_itemList.size());
}

QLayoutItem *FlowLayout::itemAt(int index) const
{
	return m_itemList.value(index);
}

QLayoutItem *FlowLayout::takeAt(int index)
{
	if (index >= 0 && index < m_itemList.size())
		return m_itemList.takeAt(index);
	else
		return nullptr;
}

Qt::Orientations FlowLayout::expandingDirections() const
{
	return {};
}

bool FlowLayout::hasHeightForWidth() const
{
	return true;
}

int FlowLayout::heightForWidth(int width) const
{
	int height = doLayout(QRect(0, 0, width, 0), true);
	return height;
}

void FlowLayout::setGeometry(const QRect &rect)
{
	QLayout::setGeometry(rect);
	doLayout(rect, false);
}

QSize FlowLayout::sizeHint() const
{
	return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
	QSize size;
	for (QLayoutItem *item: m_itemList)
		size = size.expandedTo(item->minimumSize());

	const auto margins = contentsMargins();
	size += QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
	return size;
}

int FlowLayout::doLayout(const QRect &rect, bool testOnly) const
{
	int left = 0, top = 0, right = 0, bottom = 0;
	getContentsMargins(&left, &top, &right, &bottom);
	QRect effectiveRect = rect.adjusted(+left, +top, -right, -bottom);
	int x = effectiveRect.x();
	int y = effectiveRect.y();
	int lineHeight = 0;

	for (QLayoutItem* item: m_itemList) {
		QWidget *wid = item->widget();
		int spaceX = horizontalSpacing();
		if (spaceX == -1)
			spaceX = wid->style()->layoutSpacing(
				QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Horizontal);
		int spaceY = verticalSpacing();
		if (spaceY == -1)
			spaceY = wid->style()->layoutSpacing(
				QSizePolicy::PushButton, QSizePolicy::PushButton, Qt::Vertical);

		int nextX = x + item->sizeHint().width() + spaceX;
		if (nextX - spaceX > effectiveRect.right() && lineHeight > 0) {
			x = effectiveRect.x();
			y = y + lineHeight + spaceY;
			nextX = x + item->sizeHint().width() + spaceX;
			lineHeight = 0;
		}

		if (!testOnly)
			item->setGeometry(QRect(QPoint(x, y), item->sizeHint()));

		x = nextX;
		lineHeight = qMax(lineHeight, item->sizeHint().height());
	}
	return y + lineHeight - rect.y() + bottom;
}

int FlowLayout::smartSpacing(QStyle::PixelMetric pm) const
{
	QObject *parent = this->parent();
	if (!parent) {
		return -1;
	} else if (parent->isWidgetType()) {
		QWidget *pw = static_cast<QWidget *>(parent);
		return pw->style()->pixelMetric(pm, nullptr, pw);
	} else {
		return static_cast<QLayout *>(parent)->spacing();
	}
}
