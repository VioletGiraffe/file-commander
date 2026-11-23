#pragma once

#include "diskenumerator/volumeinfo.hpp"


// Submodule includes
#include "compiler/compiler_warnings_control.h"


DISABLE_COMPILER_WARNINGS
#include <QString>
#include <QWidget>
RESTORE_COMPILER_WARNINGS

#include <functional>
#include <optional>
#include <stdint.h>
#include <vector>

class QPushButton;

// Such as "212.43 GiB"
[[nodiscard]] QString volumeSizeText(uint64_t size);
// Such as "77% used (719.08 GiB)"
[[nodiscard]] QString volumeUsageText(const VolumeInfo& volume);
// Label, path, file system, kind, space and device, one per line
[[nodiscard]] QString volumeToolTip(const VolumeInfo& volume);

// A checkable button per volume, wrapping into rows.
// The stylesheet selects the buttons by this widget's object name, _driveButtonsWidget.
class CDriveBar final : public QWidget
{
	Q_OBJECT

public:
	// Empty text: no tooltip
	using VolumeToolTipBuilder = std::function<QString (uint64_t volumeId)>;

	explicit CDriveBar(QWidget* parent = nullptr);

	// Volumes that are not ready get no button
	void setVolumes(const std::vector<VolumeInfo>& volumes);
	// Empty: no button is checked
	void setCurrentVolume(std::optional<uint64_t> volumeId);
	void setButtonSpacing(int spacing);
	// Called each time a button's tooltip is about to show
	void setVolumeToolTipBuilder(VolumeToolTipBuilder builder);

signals:
	void volumeClicked(uint64_t volumeId);
	void volumeContextMenuRequested(uint64_t volumeId);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	struct DriveButton {
		uint64_t volumeId = 0;
		QPushButton* button = nullptr;
	};
	std::vector<DriveButton> _buttons;
	VolumeToolTipBuilder _volumeToolTipBuilder;
};
