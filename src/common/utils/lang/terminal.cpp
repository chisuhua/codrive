#include <unistd.h>

#include "terminal.h"


namespace utils
{

bool Terminal::isTerminal(std::ostream &os)
{
	return (&os == &std::cout && isatty(fileno(stdout))) ||
			(&os == &std::cerr && isatty(fileno(stderr)));
}


void Terminal::Blue(std::ostream &os)
{
	if (isTerminal(os))
		os << "\033[34m";
}


void Terminal::Black(std::ostream &os)
{
	if (isTerminal(os))
		os << "\033[30m";
}


void Terminal::Red(std::ostream &os)
{
	if (isTerminal(os))
		os << "\033[31m";
}


void Terminal::Reset(std::ostream &os)
{
	if (isTerminal(os))
		os << "\e[m";
}

}

