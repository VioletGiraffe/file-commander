#pragma once

#include "filecomparator/cfoldercomparison.h"

#include <memory>
#include <optional>

class CController;

// Compares the two panels' folders and lists the differences.
// Keeps the last comparison: the command lists it again while each panel shows its compared folder or one below it.
class CTuiFolderComparison
{
public:
	explicit CTuiFolderComparison(CController& controller) noexcept : _controller(controller) {}

	// The entry to go to; nullopt when closed or cancelled
	[[nodiscard]] std::optional<CFolderComparison::EntryLocation> run();

private:
	CController& _controller;
	std::unique_ptr<CFolderComparison> _lastComparison; // Finished, with a result
	int _cursorRow = 0; // In the last comparison's list
};
