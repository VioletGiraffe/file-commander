#pragma once

#include "cpanel.h"
#include "ccontroller.h"
#include "../../QtAppIncludes"
#include <vector>

class QMouseEvent;
class CController;
class CFileListView : public QTreeView
{
	Q_OBJECT

public:
	explicit CFileListView(QWidget *parent = 0);
	// Sets the position (left or right) of a panel that this model represents
	void setPanelPosition(enum Panel p);

	void setHeaderAdjustmentRequired(bool required);

	// Preserves item's selection state
	void moveCursorToItem(const QModelIndex &item);

	// Header management
	void saveHeaderState();
	void restoreHeaderState();

signals:
	void contextMenuRequested (QPoint pos);

protected:
	// For controlling selection
	virtual void mousePressEvent(QMouseEvent *e);
	// For showing context menu
	virtual void mouseReleaseEvent(QMouseEvent *event);
	// For managing selection and cursor
	virtual void keyPressEvent(QKeyEvent * event);

private:
	void selectRegion(const QModelIndex& start, const QModelIndex& end);

private slots:
	void modelAboutToBeReset();

private:
	QByteArray    _headerGeometry;
	QByteArray    _headerState;
	CController * _controller;
	enum Panel    _panelPosition;
	bool          _bHeaderAdjustmentRequired;
};
