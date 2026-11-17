#include "csettingspageinterface.h"

#include "themes/theme.h"

#include "settings.h"


// Submodule includes
#include "theme/cthemecontroller.h"


DISABLE_COMPILER_WARNINGS
#include "ui_csettingspageinterface.h"

#include <QComboBox>
#include <QFontDialog>
#include <QSettings>
#include <QStringBuilder>
RESTORE_COMPILER_WARNINGS

static inline QString describeFont(const QFont& font)
{
	return font.family() % ", " % QString::number(font.pointSize()) % ", " % font.styleName();
}

// The scheme combo's items are System, Light, Dark: mapped explicitly, not by the enum's numbering
static int colorSchemeIndex(Qt::ColorScheme scheme)
{
	switch (scheme)
	{
	case Qt::ColorScheme::Light: return 1;
	case Qt::ColorScheme::Dark: return 2;
	case Qt::ColorScheme::Unknown: break;
	}
	return 0;
}

static Qt::ColorScheme colorSchemeAtIndex(int index)
{
	switch (index)
	{
	case 1: return Qt::ColorScheme::Light;
	case 2: return Qt::ColorScheme::Dark;
	default: return Qt::ColorScheme::Unknown;
	}
}

CSettingsPageInterface::CSettingsPageInterface(QWidget *parent) :
	CSettingsPage(parent),
	_fontDialog(std::make_unique<QFontDialog>(this)),
	ui(new Ui::CSettingsPageInterface)
{
	ui->setupUi(this);
	_fontDialog->resize(_fontDialog->sizeHint().width(), _fontDialog->sizeHint().height() * 3/2);

	QSettings s;

	_fileListFont = fileListFont();

	updateFontInfoLabel();
	// A stylesheet change restores the font the label had when first polished
	connect(&CThemeController::instance(), &CThemeController::themeChanged, this, &CSettingsPageInterface::updateFontInfoLabel);

	connect(ui->_btnSelectFileListFont, &QPushButton::clicked, this, [this]() {
		_fontDialog->setCurrentFont(_fileListFont);
		if (_fontDialog->exec() != QDialog::Accepted)
			return;

		_fileListFont = _fontDialog->selectedFont();
		updateFontInfoLabel();
	});

	ui->_cbRespectLastCursorPos->setChecked(s.value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, INTERFACE_RESPECT_LAST_CURSOR_POS_DEFAULT).toBool());
	ui->_cbDecoratedFolderIcons->setChecked(s.value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool());
	ui->_cbColumnSeparators->setChecked(s.value(KEY_INTERFACE_SHOW_COLUMN_SEPARATORS, INTERFACE_SHOW_COLUMN_SEPARATORS_DEFAULT).toBool());

	ui->_colorScheme->setCurrentIndex(colorSchemeIndex(_initialThemeSelection.schemePreference));
	connect(ui->_colorScheme, &QComboBox::currentIndexChanged, this, [](int index) {
		CThemeController::instance().setSchemePreference(colorSchemeAtIndex(index));
	});

	const auto fillThemeSelector = [this](QComboBox* selector, bool dark, const QString& storedName) {
		for (const Theme& theme : allThemes())
		{
			if (theme.dark == dark)
				selector->addItem(theme.name);
		}
		// A stored name the list does not hold leaves the first theme current, the one CThemeController::selectedTheme() resolves it to
		selector->setCurrentText(storedName);
		connect(selector, &QComboBox::currentTextChanged, this, [dark](const QString& name) {
			CThemeController::instance().setThemeName(dark, name);
		});
	};
	fillThemeSelector(ui->_lightTheme, false, _initialThemeSelection.lightThemeName);
	fillThemeSelector(ui->_darkTheme, true, _initialThemeSelection.darkThemeName);
}

CSettingsPageInterface::~CSettingsPageInterface()
{
	delete ui;
}

void CSettingsPageInterface::acceptSettings()
{
	QSettings s;
	s.setValue(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, ui->_cbRespectLastCursorPos->isChecked());
	// Saved only when changed: an unsaved font keeps following the default
	if (_fileListFont != fileListFont())
		s.setValue(KEY_INTERFACE_FILE_LIST_FONT, _fileListFont.toString());
	s.setValue(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, ui->_cbDecoratedFolderIcons->isChecked());
	s.setValue(KEY_INTERFACE_SHOW_COLUMN_SEPARATORS, ui->_cbColumnSeparators->isChecked());
}

void CSettingsPageInterface::rejectSettings()
{
	CThemeController::instance().setSelection(_initialThemeSelection);
}

void CSettingsPageInterface::updateFontInfoLabel()
{
	ui->_lblCurrentFileListFontDescription->setFont(_fileListFont);
	ui->_lblCurrentFileListFontDescription->setText(describeFont(_fileListFont));
}
