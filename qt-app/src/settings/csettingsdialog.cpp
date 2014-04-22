#include "csettingsdialog.h"
#include "csettingspage.h"
#include "ui_csettingsdialog.h"

#include <QListWidgetItem>

CSettingsDialog::CSettingsDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::CSettingsDialog)
{
	ui->setupUi(this);

	// The list doesn't expand, the settings pane does
	ui->splitter->setStretchFactor(0, 0);
	ui->splitter->setStretchFactor(1, 1);

	connect(ui->pageList, SIGNAL(itemActivated(QListWidgetItem*)), SLOT(pageChanged(QListWidgetItem*)));
}

CSettingsDialog::~CSettingsDialog()
{
	delete ui;
}

void CSettingsDialog::addSettingsPage(QWidget *page, const QString &pageName)
{
	ui->pages->addWidget(page);

	QListWidgetItem * item = new QListWidgetItem(pageName.isEmpty() ? page->windowTitle() : pageName);
	item->setData(Qt::UserRole, ui->pages->count()-1);
	ui->pageList->addItem(item);

	if (ui->pages->count() == 1)
		ui->pageList->setCurrentRow(0);

	ui->pageList->adjustSize();
}

void CSettingsDialog::pageChanged(QListWidgetItem * item)
{
	const int pageIndex = item->data(Qt::UserRole).toInt();
	ui->pages->setCurrentIndex(pageIndex);
}

void CSettingsDialog::accept()
{
	for (int i = 0; i < ui->pages->count(); ++i)
	{
		CSettingsPage * page = dynamic_cast<CSettingsPage*>(ui->pages->widget(i));
		Q_ASSERT(page);
		page->acceptSettings();
	}

	emit settingsChanged();

	QDialog::accept();
}
