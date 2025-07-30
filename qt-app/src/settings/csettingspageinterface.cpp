#include "csettingspageinterface.h"

#include "settings.h"
#include "settings/csettings.h"

DISABLE_COMPILER_WARNINGS
#include "ui_csettingspageinterface.h"

#include <QButtonGroup>
#include <QFontDialog>
#include <QStringBuilder>
#include <QStyleFactory>
#include <QStyleHints>
RESTORE_COMPILER_WARNINGS

inline QString describeFont(const QFont& font)
{
	return font.family() % ", " % QString::number(font.pointSize()) % ", " % font.styleName();
}

CSettingsPageInterface::CSettingsPageInterface(QWidget *parent) :
	CSettingsPage(parent),
	_fontDialog(std::make_unique<QFontDialog>(this)),
	ui(new Ui::CSettingsPageInterface)
{
	ui->setupUi(this);
	_fontDialog->resize(_fontDialog->sizeHint().width(), _fontDialog->sizeHint().height() * 3/2);

	CSettings s;

	QFont font;
	if (font.fromString(s.value(KEY_INTERFACE_FILE_LIST_FONT, INTERFACE_FILE_LIST_FONT_DEFAULT).toString()))
		_fontDialog->setCurrentFont(font);

	updateFontInfoLabel();

	ui->_lblCurrentFileListFontDescription->setText(describeFont(_fontDialog->currentFont()));
	connect(ui->_btnSelectFileListFont, &QPushButton::clicked, this, [this]() {
		if (_fontDialog->exec() == QDialog::Accepted)
			updateFontInfoLabel();
	});

	ui->_cbRespectLastCursorPos->setChecked(s.value(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, false).toBool());
	ui->_cbSortingNumbersAfterLetters->setChecked(s.value(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, false).toBool());
	ui->_cbDecoratedFolderIcons->setChecked(s.value(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, false).toBool());

	ui->_styleSheetEdit->setPlainText(s.value(KEY_INTERFACE_STYLE_SHEET).toString());

	int currentStyleIndex = -1;
	for (const QString& style: QStyleFactory::keys())
	{
		ui->_cbStyle->addItem(style);
		if (style.compare(QApplication::style()->name(), Qt::CaseInsensitive) == 0)
			currentStyleIndex = ui->_cbStyle->count() - 1;
	}
	ui->_cbStyle->setCurrentIndex(currentStyleIndex);

	connect(ui->_cbStyle, &QComboBox::textActivated, this, [this](const QString& style) {
		if (style == QApplication::style()->name())
			return;
		QApplication::setStyle(style);
		_styleChanged = true;
	});

	QButtonGroup* colorSchemeGroup = new QButtonGroup(this);
	colorSchemeGroup->addButton(ui->_rbColorSchemeSystem);
	colorSchemeGroup->addButton(ui->_rbColorSchemeLight);
	colorSchemeGroup->addButton(ui->_rbColorSchemeDark);

#if QT_VERSION >= QT_VERSION_CHECK(6,8,0)
	if (const auto colorScheme = QApplication::styleHints()->colorScheme(); colorScheme == Qt::ColorScheme::Dark)
		ui->_rbColorSchemeDark->setChecked(true);
	else if (colorScheme == Qt::ColorScheme::Light)
		ui->_rbColorSchemeLight->setChecked(true);
	else
		ui->_rbColorSchemeSystem->setChecked(true);

	connect(colorSchemeGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton* btn) {
		const auto scheme = static_cast<Qt::ColorScheme>(colorSchemeFromButton(btn));
		if (auto* styleHints = QApplication::styleHints(); scheme != styleHints->colorScheme())
		{
			styleHints->setColorScheme(scheme);
			_colorSchemeChanged = true;
		}
	});
#endif

	const auto settingsDialog = dynamic_cast<QDialog*>(parent);
	connect(settingsDialog, &QDialog::finished, this, [this] {
		_colorSchemeChanged = false;
		_styleChanged = false;
	});
}

CSettingsPageInterface::~CSettingsPageInterface()
{
	delete ui;
}

void CSettingsPageInterface::acceptSettings()
{
	CSettings s;
	s.setValue(KEY_INTERFACE_RESPECT_LAST_CURSOR_POS, ui->_cbRespectLastCursorPos->isChecked());
	s.setValue(KEY_INTERFACE_NUMBERS_AFFTER_LETTERS, ui->_cbSortingNumbersAfterLetters->isChecked());
	s.setValue(KEY_INTERFACE_FILE_LIST_FONT, _fontDialog->currentFont().toString());
	s.setValue(KEY_INTERFACE_SHOW_SPECIAL_FOLDER_ICONS, ui->_cbDecoratedFolderIcons->isChecked());
	s.setValue(KEY_INTERFACE_STYLE_SHEET, ui->_styleSheetEdit->toPlainText());

	if (const auto styleName = ui->_cbStyle->currentText(); _styleChanged && !styleName.isEmpty())
		s.setValue(KEY_INTERFACE_STYLE, styleName);
	if (_colorSchemeChanged)
		s.setValue(KEY_INTERFACE_COLOR_SCHEME, colorSchemeFromButton(ui->_rbColorSchemeDark->group()->checkedButton()));
}

void CSettingsPageInterface::updateFontInfoLabel()
{
	ui->_lblCurrentFileListFontDescription->setFont(_fontDialog->currentFont());
	ui->_lblCurrentFileListFontDescription->setText(describeFont(_fontDialog->currentFont()));
}

int CSettingsPageInterface::colorSchemeFromButton(QAbstractButton* btn)
{
#if QT_VERSION >= QT_VERSION_CHECK(6,8,0)
	if (btn == ui->_rbColorSchemeDark)
		return static_cast<int>(Qt::ColorScheme::Dark);
	else if (btn == ui->_rbColorSchemeLight)
		return static_cast<int>(Qt::ColorScheme::Light);
	else
		return static_cast<int>(Qt::ColorScheme::Unknown);
#else
	return 0;
#endif
}
