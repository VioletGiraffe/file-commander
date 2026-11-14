#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QPushButton>
#include <QString>
RESTORE_COMPILER_WARNINGS

// A button of the function key bar. Draws its key in a small outlined box, apart from the action's label.
// The style draws the button itself, as it does for any push button.
class CFunctionKeyButton final : public QPushButton
{
	Q_OBJECT

public:
	using QPushButton::QPushButton;

	// key: such as "F3"
	void setCaption(const QString& key, const QString& label);
	void setLabel(const QString& label);
	[[nodiscard]] const QString& label() const;

	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	[[nodiscard]] QSize keyBoxSize() const;
	[[nodiscard]] QFont keyFont() const;

private:
	QString _key;
	QString _label;
};
