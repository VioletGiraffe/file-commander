#pragma once

#include "diskenumerator/volumeinfo.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <optional>
#include <stdint.h>
#include <vector>

class QPushButton;

// A checkable button per volume, wrapping into rows. On Windows a button's context menu is the shell's menu for the volume.
// The stylesheet selects the buttons by this widget's object name, _driveButtonsWidget.
class CDriveBar final : public QWidget
{
	Q_OBJECT

public:
	explicit CDriveBar(QWidget* parent = nullptr);

	// Volumes that are not ready get no button
	void setVolumes(const std::vector<VolumeInfo>& volumes);
	// Empty: no button is checked
	void setCurrentVolume(std::optional<uint64_t> volumeId);
	void setButtonSpacing(int spacing);

signals:
	void volumeClicked(uint64_t volumeId);

private:
	struct DriveButton {
		uint64_t volumeId = 0;
		QPushButton* button = nullptr;
	};
	std::vector<DriveButton> _buttons;
};
