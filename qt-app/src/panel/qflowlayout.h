#pragma once

#include "compiler/compiler_warnings_control.h"

DISABLE_COMPILER_WARNINGS
#include <QLayout>
#include <QStyle>
RESTORE_COMPILER_WARNINGS

class FlowLayout final : public QLayout
{
public:
	explicit FlowLayout(QWidget *parent, int margin = -1, int hSpacing = -1, int vSpacing = -1);
	~FlowLayout() override;

	void addItem(QLayoutItem *item) override;
	[[nodiscard]] int horizontalSpacing() const;
	[[nodiscard]] int verticalSpacing() const;
	[[nodiscard]] Qt::Orientations expandingDirections() const override;
	[[nodiscard]] bool hasHeightForWidth() const override;
	[[nodiscard]] int heightForWidth(int) const override;
	[[nodiscard]] int count() const override;
	[[nodiscard]] QLayoutItem *itemAt(int index) const override;
	[[nodiscard]] QSize minimumSize() const override;
	void setGeometry(const QRect &rect) override;
	[[nodiscard]] QSize sizeHint() const override;
	[[nodiscard]] QLayoutItem *takeAt(int index) override;

private:
	int doLayout(const QRect &rect, bool testOnly) const;
	[[nodiscard]] int smartSpacing(QStyle::PixelMetric pm) const;

private:
	QList<QLayoutItem *> m_itemList;
	int m_hSpace;
	int m_vSpace;
};
