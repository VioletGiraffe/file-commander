#pragma once

#include "functionkeyform.h"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QPushButton>
#include <QString>
RESTORE_COMPILER_WARNINGS

class QPainter;
class QRect;

// A button of the function key bar. Draws its key apart from the action's label.
// The style draws the button itself, as it does for any push button.
class CFunctionKeyButton final : public QPushButton
{
	Q_OBJECT

public:
	using QPushButton::QPushButton;

	void setForm(FunctionKeyForm form);

	// key: such as "F3"
	void setCaption(const QString& key, const QString& label);
	void setLabel(const QString& label);
	[[nodiscard]] const QString& label() const;

	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	// The size of the key and the label together, in the current form
	[[nodiscard]] QSize captionSize() const;
	void paintKeyBoxCaption(QPainter& painter, const QRect& content) const;
	void paintLabelCellCaption(QPainter& painter, const QRect& content) const;

	[[nodiscard]] QSize keyBoxSize() const;
	[[nodiscard]] QFont keyBoxFont() const;
	[[nodiscard]] QFont labelCellFont() const;

private:
	QString _key;
	QString _label;
	FunctionKeyForm _form = FunctionKeyForm::KeyBox;
};
