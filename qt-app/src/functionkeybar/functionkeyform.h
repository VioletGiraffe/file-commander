#pragma once

// How CFunctionKeyButton draws its key and its label
enum class FunctionKeyForm
{
	KeyBox,    // the key in a small outlined box, the pair centred in the button
	LabelCell, // the key as plain text, then the label on an accent-filled cell that reaches the button's right edge
	KeyCap     // as KeyBox, the box square and both the box and the key in the accent colour, with the theme's glow
};
