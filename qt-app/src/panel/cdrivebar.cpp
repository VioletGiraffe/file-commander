#include "cdrivebar.h"

#include "themes/stylesheet.h"

#include "filesystemhelperfunctions.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "qtcore_helpers/displayformatting.hpp"
#include "widgets/layouts/cflowlayout.h"


DISABLE_COMPILER_WARNINGS
#include <QHelpEvent>
#include <QIcon>
#include <QPushButton>
#include <QStringList>
#include <QToolTip>
RESTORE_COMPILER_WARNINGS

#include <algorithm>
#include <cmath>
#include <utility>

static QString volumeKindText(const VolumeInfo::Kind kind)
{
	switch (kind)
	{
	case VolumeInfo::Kind::Internal:
		// Not shown: it also stands for the volumes not told apart
		return {};
	case VolumeInfo::Kind::External:
		return QObject::tr("Removable");
	case VolumeInfo::Kind::Optical:
		return QObject::tr("Optical disc");
	case VolumeInfo::Kind::Network:
		return QObject::tr("Network");
	case VolumeInfo::Kind::DiskImage:
		return QObject::tr("Disk image");
	case VolumeInfo::Kind::RamDisk:
		return QObject::tr("RAM disk");
	case VolumeInfo::Kind::Home:
		return QObject::tr("Home folder");
	}

	return {};
}

QString volumeSizeText(const uint64_t size)
{
	return fileSizeToString(size, { .significantPlaces = 3, .decimalPlaces = 2 });
}

QString volumeUsageText(const VolumeInfo& volume)
{
	const uint64_t usedSize = volume.volumeSize - std::min(volume.freeSize, volume.volumeSize);
	const double usedFraction = volume.volumeSize > 0 ? (double)usedSize / (double)volume.volumeSize : 0.0;
	return QObject::tr("%1% used (%2)").arg(std::lround(usedFraction * 100.0)).arg(volumeSizeText(usedSize));
}

QString volumeToolTip(const VolumeInfo& volume)
{
	QStringList lines;

	const QString path = toNativeSeparators(volume.rootObjectInfo.fullAbsolutePath());
	lines.push_back(volume.volumeLabel.isEmpty() || volume.volumeLabel == path ? path : volume.volumeLabel + QStringLiteral(" — ") + path);

	QStringList typeParts{ volume.fileSystemName, volumeKindText(volume.kind) };
	typeParts.removeAll(QString{});
	if (!typeParts.isEmpty())
		lines.push_back(typeParts.join(QStringLiteral(" · ")));

	if (volume.volumeSize > 0)
		lines.push_back(QObject::tr("%1 free of %2, %3").arg(volumeSizeText(volume.freeSize), volumeSizeText(volume.volumeSize), volumeUsageText(volume)));

	if (!volume.devicePath.isEmpty())
		lines.push_back(volume.devicePath);

	return lines.join(QLatin1Char('\n'));
}

CDriveBar::CDriveBar(QWidget* parent) :
	QWidget(parent)
{
	setLayout(new CFlowLayout(this, 0)); // setButtonSpacing() sets the spacing
}

void CDriveBar::setVolumes(const std::vector<VolumeInfo>& volumes)
{
	for (const DriveButton& driveButton : _buttons)
	{
		layout()->removeWidget(driveButton.button);
		driveButton.button->deleteLater();
	}
	_buttons.clear();

	for (const VolumeInfo& volume : volumes)
	{
		if (!volume.isReady || !volume.rootObjectInfo.isValid())
			continue;

#ifdef _WIN32
		const QString name = QString{ volume.rootObjectInfo.fullAbsolutePath() }.remove(QLatin1Char('/'));
#else
		const QString name = volume.volumeLabel;
#endif

		auto* button = new QPushButton;
		button->setFocusPolicy(Qt::NoFocus);
		button->setCheckable(true);
		if (const QString glyph = driveGlyphName(volume.kind); !glyph.isEmpty())
		{
			button->setProperty(driveGlyphProperty, glyph);
			// The stylesheet's tinted icon replaces this one when drawn; QPushButton's size hint counts only its own icon
			button->setIcon(QIcon{ QStringLiteral(":/theme/") + glyph + QStringLiteral(".svg") });
		}
		button->setText(name);
		button->installEventFilter(this);

		const uint64_t volumeId = volume.id();
		assert_r(connect(button, &QPushButton::clicked, this, [this, volumeId] { emit volumeClicked(volumeId); }));
		button->setContextMenuPolicy(Qt::CustomContextMenu);
		assert_r(connect(button, &QPushButton::customContextMenuRequested, this, [this, volumeId] { emit volumeContextMenuRequested(volumeId); }));

		layout()->addWidget(button);
		_buttons.push_back({ .volumeId = volumeId, .button = button });
	}
}

void CDriveBar::setCurrentVolume(const std::optional<uint64_t> volumeId)
{
	for (const DriveButton& driveButton : _buttons)
		driveButton.button->setChecked(driveButton.volumeId == volumeId);
}

void CDriveBar::setButtonSpacing(const int spacing)
{
	layout()->setSpacing(spacing);
}

void CDriveBar::setVolumeToolTipBuilder(VolumeToolTipBuilder builder)
{
	_volumeToolTipBuilder = std::move(builder);
}

bool CDriveBar::eventFilter(QObject* watched, QEvent* event)
{
	if (event->type() == QEvent::ToolTip && _volumeToolTipBuilder)
	{
		for (const DriveButton& driveButton : _buttons)
		{
			if (driveButton.button == watched)
			{
				QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), _volumeToolTipBuilder(driveButton.volumeId), driveButton.button);
				return true;
			}
		}
	}

	// Also reached by a button that setVolumes() replaced and deleteLater() has not destroyed yet
	return QWidget::eventFilter(watched, event);
}
