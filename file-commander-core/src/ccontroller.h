#ifndef CCONTROLLER_H
#define CCONTROLLER_H

#include "QtCoreIncludes"

#include "fileoperationresultcode.h"
#include "cpanel.h"
#include "diskenumerator/cdiskenumerator.h"

struct ItemId
{
	ItemId (size_t index_, uint hash_) : index(index_), hash(hash_) {}
	size_t index;
	uint   hash;
};

class CController : private CDiskEnumerator::IDiskListObserver
{
public:
	// Disk list observer interface
	class IDiskListObserver
	{
	public:
		virtual ~IDiskListObserver() {}
		virtual void disksChanged(std::vector<CDiskEnumerator::Drive> drives, Panel p, size_t currentDriveIndex) = 0;
	};

	static CController * get ();

	void setPanelContentsChangedListener(PanelContentsChangedListener * listener);
	void setDisksChangedListener(IDiskListObserver * listener);

// Notifications from UI
	// Updates the list of files in the current directory this panel is viewing, and send the new state to UI
	void refreshPanelContents(Panel p);
	// Creates a new tab for the specified panel, returns tab ID
	int tabCreated (Panel p);
	// Removes a tab for the specified panel and tab ID
	void tabRemoved(Panel panel, int tabId);
	// Indicates that an item was activated and appropriate action should be taken.  Returns error message, if any
	FileOperationResultCode itemActivated(uint itemHash, Panel p);
	// A current disk has been switched
	void diskSelected(Panel p, size_t index);
	// Program settings have changed
	void settingsChanged();

// Operations
	// Navigates specified panel up the directory tree
	void navigateUp (Panel p);
	// Go to the previous location from history, if any
	void navigateBack(Panel p);
	// Go to the next location from history, if any
	void navigateForward(Panel p);
	// Sets the specified path, if possible. Otherwise reverts to the previously set path
	FileOperationResultCode setPath(Panel p, const QString& path);
	// Creates a folder with a specified name at the specified parent folder
	bool createFolder(const QString& parentFolder, const QString& name);
	// Creates a file with a specified name at the specified parent folder
	bool createFile(const QString& parentFolder, const QString& name);
	// Opens a terminal window in the specified folder
	void openTerminal(const QString& folder);
	// Rename a file or folder
	void rename(Panel p, size_t itemIdx, const QString& newName);
	// Calculates directory size, stores it in the corresponding CFileSystemObject and sends data change notification
	void calculateDirSize(Panel p, size_t dirIndex);

// Getters
	const CPanel& panel(Panel p) const;
	CPanel& panel(Panel p);
	const CFileSystemObject& itemByIndex(Panel p, size_t index) const;
	const CFileSystemObject& itemByHash(Panel p, uint hash) const;
	std::vector<CFileSystemObject> items (Panel p, const std::vector<uint>& hashes) const;
	size_t numItems(Panel p) const;
	QString itemPath(Panel p, uint hash) const;
	QString diskPath(size_t index) const;

	// Returns hash of an item that was the last selected in the specified dir
	uint currentItemInFolder(Panel p, const QString& dir) const;

private:
	CController();

	virtual void disksChanged();

	void saveDirectoryForCurrentDisk(Panel p);
	size_t currentDiskIndex(Panel p) const;

private:
	// panels
	CPanel              _leftPanel, _rightPanel;
	CDiskEnumerator &   _diskEnumerator;
	std::vector<IDiskListObserver*> _disksChangedListeners;
};

#endif // CCONTROLLER_H