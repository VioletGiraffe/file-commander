#include "cbreadcrumbbar.h"

#include "themes/theme.h"

#include "cfilesystemobject.h"
#include "filesystemhelperfunctions.h"


// Submodule includes
#include "theme/ctintedsvgiconengine.h"
#include "widgets/chistorycombobox.h"
#include "widgets/clineedit.h"


DISABLE_COMPILER_WARNINGS
#include <QCursor>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QSizePolicy>
RESTORE_COMPILER_WARNINGS

#include <algorithm>

namespace {

constexpr int sidePadding = 8, folderPadding = 3, separatorPadding = 3;
constexpr int arrowGap = 6; // between the last part and the arrow
// Titled: the current folder's font size relative to the other parts', and the space above and below it
constexpr qreal titleScale = 1.6;
constexpr int titleVerticalPadding = 2;
constexpr QSize arrowSize{ 10, 7 };
// A separator is a chevron, pointing down while its menu is open
constexpr QSize chevronSize{ 7, 10 };
constexpr int separatorPartWidth = std::max(chevronSize.width(), arrowSize.width()) + 2 * separatorPadding;
const QString hiddenFoldersText = QStringLiteral("...");

}

CBreadcrumbBar::CBreadcrumbBar(QWidget* parent) :
	QWidget(parent),
	_editor{ new CHistoryComboBox(this) }
{
	setMouseTracking(true);

	_editor->setLineEdit(new CLineEdit);
	_editor->lineEdit()->setFocusPolicy(Qt::ClickFocus);
	_editor->setFocusPolicy(Qt::ClickFocus);
	// The default policy measures the entries on the first show only
	_editor->setSizeAdjustPolicy(QComboBox::AdjustToContents);
	_editor->hide();
	// QComboBox is its line edit's focus proxy, so the focus events arrive at the combo box
	_editor->installEventFilter(this);
}

CHistoryComboBox& CBreadcrumbBar::editor() const
{
	return *_editor;
}

void CBreadcrumbBar::setPath(const QString& posixPath)
{
	std::vector<QString> hierarchy = pathHierarchy(posixPath); // the path first, the root last
	if (hierarchy.empty())
		return;

	std::ranges::reverse(hierarchy);
	if (!hierarchy.back().endsWith('/'))
		hierarchy.back() += '/';

	if (!_folders.empty() && _folders.back().path == hierarchy.back())
		return;

	_folders.clear();
	_folders.reserve(hierarchy.size());
	for (size_t i = 0; i < hierarchy.size(); ++i)
	{
		const QString& path = hierarchy[i];
		Folder folder{ .path = path };
		if (i == 0)
		{
			const QString rootName = toNativeSeparators(path.chopped(1));
			folder.name = rootName.isEmpty() ? QStringLiteral("/") : rootName;
		}
		else
			folder.name = path.mid(hierarchy[i - 1].size()).chopped(1);

		_folders.push_back(std::move(folder));
	}

	layOutParts();
	updateGeometry();
	update();
}

void CBreadcrumbBar::setShownAsActive(const bool active)
{
	_shownAsActive = active;
	update();
}

void CBreadcrumbBar::applyTheme()
{
	_style = appliedTheme().pathBar;
	// Strip: centred by the path row's spacers
	setSizePolicy(_style == PathBarStyle::Strip ? QSizePolicy::Maximum : QSizePolicy::Preferred, QSizePolicy::Fixed);
	layOutParts();
	updateGeometry();
	update();
}

void CBreadcrumbBar::startEditing()
{
	if (!_folders.empty())
		_editor->setEditText(toNativeSeparators(_folders.back().path));

	setHoveredPart(noPart);
	_editor->show();
	updateGeometry();

	QLineEdit* const lineEdit = _editor->lineEdit();
	lineEdit->setFocus(Qt::OtherFocusReason);
	lineEdit->selectAll(); // CLineEdit selects all only on gaining focus, and it may already have it
}

void CBreadcrumbBar::stopEditing()
{
	_editor->hide();
	updateGeometry();
	update();
}

QSize CBreadcrumbBar::sizeHint() const
{
	return { _editor->isHidden() ? fullStripWidth() : _editor->sizeHint().width(), barHeight() };
}

// Not the editor's while editing: QComboBox makes that as wide as the longest history entry
QSize CBreadcrumbBar::minimumSizeHint() const
{
	const int width = 2 * sidePadding + fontMetrics().horizontalAdvance(hiddenFoldersText) + 2 * folderPadding + arrowGap + arrowSize.width();
	return { width, barHeight() };
}

// At least as tall as the editor, so that showing the editor does not move the rows below
int CBreadcrumbBar::barHeight() const
{
	_editor->ensurePolished(); // the stylesheet sets the editor's padding
	const int editorHeight = _editor->sizeHint().height();
	if (_style != PathBarStyle::Titled)
		return editorHeight;

	return std::max(editorHeight, QFontMetrics{ currentFolderFont() }.height() + 2 * titleVerticalPadding);
}

void CBreadcrumbBar::paintEvent(QPaintEvent* /*event*/)
{
	if (!_editor->isHidden())
		return;

	const Theme& theme = appliedTheme();
	const CBasePalette& palette = theme.palette;

	QPainter painter{ this };
	painter.setRenderHint(QPainter::Antialiasing);

	// Strip: a highlighted part has the cell's colours swapped. Field and Titled: a highlighted part is a rounded tint.
	const bool strip = _style == PathBarStyle::Strip, titled = _style == PathBarStyle::Titled;
	const QColor background = strip && _shownAsActive ? palette.accent : palette.windowBg;
	const QColor foreground = strip && _shownAsActive ? palette.accentFg : titled ? palette.textDim : palette.text;
	const QColor titleColor = _shownAsActive ? palette.accent : palette.text;
	if (strip)
		painter.fillRect(rect(), background);
	else if (_style == PathBarStyle::Field)
	{
		painter.setPen(underMouse() ? palette.borderStrong : palette.border);
		painter.setBrush(background);
		const qreal radius = theme.metrics.controlRadius;
		painter.drawRoundedRect(QRectF{ rect() }.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
	}

	// Returns the colour for the content of the highlighted area, or the default when it is not highlighted
	const auto paintHighlight = [&](const QRect& area, const bool highlighted, const bool pressed, const QColor& defaultColor) {
		if (!highlighted)
			return defaultColor;

		painter.setPen(Qt::NoPen);
		if (strip)
		{
			painter.fillRect(area, foreground);
			return background;
		}

		const qreal radius = theme.metrics.menuItemRadius;
		painter.setBrush(pressed ? palette.accentBg : palette.surfaceAlt);
		painter.drawRoundedRect(area.adjusted(0, 3, 0, -3), radius, radius);
		return pressed ? palette.accentText : palette.text;
	};

	// Titled: the smaller text stands on the title's baseline, and the chevrons are centred on that text
	const QFontMetrics titleMetrics{ currentFolderFont() }, textMetrics = fontMetrics();
	const int baseline = (height() - titleMetrics.height()) / 2 + titleMetrics.ascent();
	const int textCentre = titled ? baseline - textMetrics.ascent() + textMetrics.height() / 2 : height() / 2;

	const QColor separatorColor = strip ? foreground : palette.textDim;
	const int currentFolderIndex = (int)_folders.size() - 1;
	for (int i = 0; i < (int)_parts.size(); ++i)
	{
		const Part& part = _parts[(size_t)i];
		const bool currentFolder = part.kind == PartKind::Folder && part.folderIndex == currentFolderIndex;
		const QRect area{ part.left, 0, part.width, height() };
		const QColor defaultColor = part.kind == PartKind::Separator ? separatorColor : titled && currentFolder ? titleColor : foreground;
		const QColor textColor = paintHighlight(area, i == _hoveredPart || i == _pressedPart, i == _pressedPart, defaultColor);

		if (part.kind == PartKind::Separator)
		{
			const bool menuOpen = i == _pressedPart;
			const QSize size = menuOpen ? arrowSize : chevronSize;
			const QRect iconRect{ QPoint{ part.left + (part.width - size.width()) / 2, textCentre - size.height() / 2 }, size };
			const QString icon = menuOpen ? QStringLiteral(":/theme/combobox_down_arrow.svg") : QStringLiteral(":/theme/chevron_right.svg");
			painter.drawPixmap(iconRect, tintedSvgPixmap(icon, textColor, size, devicePixelRatioF()));
			continue;
		}

		painter.setFont(currentFolder ? currentFolderFont() : font());
		painter.setPen(textColor);
		if (titled)
		{
			const QFontMetrics& metrics = currentFolder ? titleMetrics : textMetrics;
			painter.drawText(QRect{ part.left, baseline - metrics.ascent(), part.width, metrics.height() }, Qt::AlignHCenter | Qt::AlignTop, part.text);
		}
		else
			painter.drawText(area, Qt::AlignCenter, part.text);
	}

	const QRect arrow = arrowRect();
	const QRect arrowArea{ arrow.left() - arrowGap / 2, 0, width() - arrow.left() + arrowGap / 2, height() };
	const QColor arrowColor = paintHighlight(arrowArea, _hoveredPart == arrowPart, false, strip ? foreground : palette.textDim);
	painter.drawPixmap(arrow, tintedSvgPixmap(QStringLiteral(":/theme/combobox_down_arrow.svg"), arrowColor, arrowSize, devicePixelRatioF()));
}

void CBreadcrumbBar::resizeEvent(QResizeEvent* event)
{
	QWidget::resizeEvent(event);
	_editor->setGeometry(rect());
	layOutParts();
}

void CBreadcrumbBar::changeEvent(QEvent* event)
{
	QWidget::changeEvent(event);
	if (event->type() == QEvent::FontChange)
	{
		layOutParts();
		updateGeometry();
	}
}

void CBreadcrumbBar::mouseMoveEvent(QMouseEvent* event)
{
	setHoveredPart(partAt(event->position().toPoint()));
	QWidget::mouseMoveEvent(event);
}

// A menu opens on the press; any other part acts on the release over the part pressed
void CBreadcrumbBar::mousePressEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton)
	{
		QWidget::mousePressEvent(event);
		return;
	}

	const int part = partAt(event->position().toPoint());
	if (part >= 0 && _parts[(size_t)part].kind != PartKind::Folder)
		activatePart(part);
	else
	{
		_pressedPart = part;
		update();
	}
}

void CBreadcrumbBar::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() != Qt::LeftButton || _pressedPart == noPart)
	{
		QWidget::mouseReleaseEvent(event);
		return;
	}

	const int part = _pressedPart;
	_pressedPart = noPart;
	update();
	if (partAt(event->position().toPoint()) == part)
		activatePart(part);
}

void CBreadcrumbBar::leaveEvent(QEvent* event)
{
	setHoveredPart(noPart);
	QWidget::leaveEvent(event);
}

bool CBreadcrumbBar::eventFilter(QObject* watched, QEvent* event)
{
	if (watched == _editor && event->type() == QEvent::FocusOut)
	{
		const Qt::FocusReason reason = static_cast<QFocusEvent*>(event)->reason();
		if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason)
			stopEditing();
	}

	return QWidget::eventFilter(watched, event);
}

void CBreadcrumbBar::layOutParts()
{
	_parts.clear();
	if (_folders.empty())
		return;

	const int folderCount = (int)_folders.size();
	const auto widthWithSeparator = [&](const int i) {
		return folderPartWidth((size_t)i) + (i + 1 < folderCount ? separatorPartWidth : 0);
	};
	const int hiddenFoldersWidth = fontMetrics().horizontalAdvance(hiddenFoldersText) + 2 * folderPadding;

	const int partsRight = arrowRect().left() - arrowGap;
	int shownWidth = 0;
	for (int i = 0; i < folderCount; ++i)
		shownWidth += widthWithSeparator(i);

	// Hides folders from the root down while the rest does not fit; the current folder always stays
	int hiddenCount = 0;
	while (hiddenCount < folderCount - 1 && sidePadding + (hiddenCount > 0 ? hiddenFoldersWidth + separatorPartWidth : 0) + shownWidth > partsRight)
	{
		shownWidth -= widthWithSeparator(hiddenCount);
		++hiddenCount;
	}

	int left = sidePadding;
	const auto addPart = [&](const PartKind kind, const int folderIndex, QString text, const int width) {
		_parts.push_back({ .kind = kind, .folderIndex = folderIndex, .left = left, .width = width, .text = std::move(text) });
		left += width;
	};

	if (hiddenCount > 0)
	{
		addPart(PartKind::HiddenFolders, hiddenCount, hiddenFoldersText, hiddenFoldersWidth);
		addPart(PartKind::Separator, hiddenCount - 1, {}, separatorPartWidth);
	}

	for (int i = hiddenCount; i < folderCount; ++i)
	{
		QString name = _folders[(size_t)i].name;
		int width = folderPartWidth((size_t)i);
		// Only the current folder can still be too wide
		if (const int room = std::max(partsRight - left, 0); i == folderCount - 1 && width > room)
		{
			name = QFontMetrics{ currentFolderFont() }.elidedText(name, Qt::ElideRight, std::max(room - 2 * folderPadding, 0));
			width = room;
		}

		addPart(PartKind::Folder, i, std::move(name), width);
		if (i + 1 < folderCount)
			addPart(PartKind::Separator, i, {}, separatorPartWidth);
	}

	// The indices refer to the previous parts
	_pressedPart = noPart;
	_hoveredPart = noPart;
	setHoveredPart(underMouse() ? partAt(mapFromGlobal(QCursor::pos())) : noPart);
}

int CBreadcrumbBar::partAt(const QPoint pos) const
{
	if (!rect().contains(pos))
		return noPart;

	if (pos.x() >= arrowRect().left() - arrowGap / 2)
		return arrowPart;

	const auto part = std::ranges::find_if(_parts, [x = pos.x()](const Part& p) { return x >= p.left && x < p.left + p.width; });
	return part == _parts.end() ? emptyArea : (int)(part - _parts.begin());
}

void CBreadcrumbBar::activatePart(const int partIndex)
{
	if (partIndex == arrowPart || partIndex == emptyArea)
	{
		startEditing();
		if (partIndex == arrowPart)
			_editor->showPopup();
		return;
	}

	if (partIndex < 0)
		return;

	const Part part = _parts[(size_t)partIndex];
	switch (part.kind)
	{
	case PartKind::Folder:
		if (part.folderIndex == (int)_folders.size() - 1)
			startEditing();
		else
			emit goToAncestorRequested(_folders[(size_t)part.folderIndex].path, _folders[(size_t)part.folderIndex + 1].path);
		break;

	case PartKind::Separator:
	{
		QMenu menu{ this };
		emit folderMenuRequested(&menu, _folders[(size_t)part.folderIndex].path);
		showMenuBelowPart(partIndex, menu);
		break;
	}

	case PartKind::HiddenFolders:
	{
		QMenu menu{ this };
		for (size_t i = 0; i < (size_t)part.folderIndex; ++i)
		{
			// A root keeps its separator
			const QString& path = _folders[i].path;
			QAction* const action = menu.addAction(toNativeSeparators(i == 0 ? path : path.chopped(1)));
			connect(action, &QAction::triggered, this, [this, path, childOnPath = _folders[i + 1].path] { emit goToAncestorRequested(path, childOnPath); });
		}
		showMenuBelowPart(partIndex, menu);
		break;
	}
	}
}

// The menu's actions may change the path while it is open: the parts are laid out again
void CBreadcrumbBar::showMenuBelowPart(const int partIndex, QMenu& menu)
{
	if (menu.isEmpty())
		return;

	_pressedPart = partIndex;
	update();
	const QPoint position = mapToGlobal(QPoint{ _parts[(size_t)partIndex].left, height() });
	menu.exec(position);

	_pressedPart = noPart;
	setHoveredPart(partAt(mapFromGlobal(QCursor::pos())));
	update();
}

void CBreadcrumbBar::setHoveredPart(const int partIndex)
{
	if (partIndex == _hoveredPart)
		return;

	_hoveredPart = partIndex;
	const bool editsPath = partIndex == emptyArea ||
		(partIndex >= 0 && _parts[(size_t)partIndex].kind == PartKind::Folder && _parts[(size_t)partIndex].folderIndex == (int)_folders.size() - 1);
	if (editsPath)
		setCursor(Qt::IBeamCursor);
	else
		unsetCursor();

	update();
}

QFont CBreadcrumbBar::currentFolderFont() const
{
	QFont currentFolder = font();
	currentFolder.setBold(true);
	if (_style == PathBarStyle::Titled)
		currentFolder.setPointSizeF(currentFolder.pointSizeF() * titleScale);
	return currentFolder;
}

QRect CBreadcrumbBar::arrowRect() const
{
	return { QPoint{ width() - sidePadding - arrowSize.width(), (height() - arrowSize.height()) / 2 }, arrowSize };
}

int CBreadcrumbBar::folderPartWidth(const size_t folderIndex) const
{
	const QFontMetrics metrics{ folderIndex + 1 == _folders.size() ? currentFolderFont() : font() };
	return metrics.horizontalAdvance(_folders[folderIndex].name) + 2 * folderPadding;
}

int CBreadcrumbBar::fullStripWidth() const
{
	int width = 2 * sidePadding + arrowGap + arrowSize.width();
	for (size_t i = 0; i < _folders.size(); ++i)
		width += folderPartWidth(i) + (i + 1 < _folders.size() ? separatorPartWidth : 0);

	return width;
}
