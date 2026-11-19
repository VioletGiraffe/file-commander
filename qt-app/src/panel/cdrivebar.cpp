#include "cdrivebar.h"

#include "themes/stylesheet.h"


// Submodule includes
#include "assert/advanced_assert.h"
#include "widgets/layouts/cflowlayout.h"


DISABLE_COMPILER_WARNINGS
#include <QIcon>
#include <QPushButton>
RESTORE_COMPILER_WARNINGS

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
