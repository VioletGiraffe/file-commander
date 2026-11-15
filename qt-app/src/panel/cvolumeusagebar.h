#pragma once

// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <stdint.h>

// How full a volume is, as a bar painted in the applied theme's colours.
// The fill is the accent in the active panel and dim in the other; a volume low on space has the error colour in both.
class CVolumeUsageBar final : public QWidget
{
public:
	explicit CVolumeUsageBar(QWidget* parent = nullptr);

	// freeSize: clamped to volumeSize
	void setSpace(uint64_t volumeSize, uint64_t freeSize);
	void setShownAsActive(bool active);

	[[nodiscard]] QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	double _usedFraction = 0.0;
	bool _lowOnSpace = false;
	bool _shownAsActive = false;
};
