#ifndef CHISTORYCOMBOBOX_H
#define CHISTORYCOMBOBOX_H

#include "QtAppIncludes"

class CHistoryComboBox : public QComboBox
{
	Q_OBJECT

public:
	CHistoryComboBox(QWidget * parent = 0);

public slots:
	// Moves the currently selected item to the top
	void currentItemActivated();
	// Enables or disables history mode (moving selected item to the top)
	void setHistoryMode(bool historyMode);
	// Switch to the next command following the current one
	void cycleLastCommands();
	// Set current index to 0 and clear line edit
	void reset();


	bool eventFilter(QObject *, QEvent *) override;

signals:
	void lineeditReturnPressed();

protected:
	void keyPressEvent(QKeyEvent * e) override;
};

#endif // CHISTORYCOMBOBOX_H
