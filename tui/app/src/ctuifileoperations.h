#pragma once

#include "cpanel.h"

#include "fileoperations/fileoperationtypes.h"

class CController;

// Both act on the panel's selected items, or on the item under its cursor when nothing is selected.

// Asks for the destination, prefilled with the other panel's folder. Returns when the operation and its prompts have ended.
void transferSelectedItems(CController& controller, Panel sourcePanel, TransferKind kind);
// A permanent deletion returns when it has ended.
// The trash runs in the background: the items it refuses are offered for permanent deletion when it finishes.
void deleteSelectedItems(CController& controller, Panel sourcePanel, bool toTrash);
