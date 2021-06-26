#pragma once

#include <iostream>


namespace utils
{

/// Class with static functions to manipulate the terminal.
class Terminal
{
	// Returns whether an output stream is associated with a terminal. This
	// function is used to detect whether color information should be dumped.
	static bool isTerminal(std::ostream &os);

public:

	/// Begin blue text.
	static void Blue(std::ostream &os = std::cout);

	/// Begin red text.
	static void Red(std::ostream &os = std::cout);

	/// Begin black text.
	static void Black(std::ostream &os = std::cout);

	/// Reset original color.
	static void Reset(std::ostream &os = std::cout);
};

} // namespace utils


