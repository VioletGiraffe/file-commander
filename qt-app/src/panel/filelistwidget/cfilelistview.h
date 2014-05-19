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
	void returnPressOrDoubleClick(QModelIndex index);
	void returnPressed();
	void ctrlEnterPressed();
	void ctrlShiftEnterPressed();

protected:
	// For controlling selection
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	// For showing context menu
	void mouseReleaseEvent(QMouseEvent *event) override;
	// For managing selection and cursor
	void keyPressEvent(QKeyEvent * event) override;

	bool edit ( const QModelIndex & index, EditTrigger trigger, QEvent * event ) override;

protected slots:
	void closeEditor ( QWidget * editor, QAbstractItemDelegate::EndEditHint hint ) override;
	void editorDestroyed ( QObject * editor ) override;

private:
	void selectRegion(const QModelIndex& start, const QModelIndex& end);

private slots:
	void modelAboutToBeReset();

private:
	QByteArray          _headerGeometry;
	QByteArray          _headerState;
	CController       & _controller;
	enum Panel          _panelPosition;
	bool                _bHeaderAdjustmentRequired;
	bool                _bEditInProgress;
	QPoint              _singleMouseClickPos;
	bool                _singleMouseClickValid;
};
