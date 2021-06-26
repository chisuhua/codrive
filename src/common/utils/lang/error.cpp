#include <cxxabi.h>
#include <execinfo.h>
#include <memory>

#include "error.h"
#include "string.h"
#include "terminal.h"


namespace utils
{


Exception::~Exception()
{
}


void Exception::Dump(std::ostream &os) const
{
	// Print in clean paragraphs
	StringFormatter formatter;

	// Prefixes
	formatter << prefixes;

	// Type
	if (!type.empty())
		formatter << type << ": ";

	// Message
	formatter << getMessage();

	// Print in red
	Terminal::Red(os);

	// Dump it
	os << '\n' << formatter << "\n\n";

	// Call stack
	if (!call_stack.empty())
	{
		formatter.Clear();
		formatter.setIndent(8);
		formatter.setFirstLineIndent(0);
		formatter << "Call stack:\n" << call_stack << "\n\n";
		os << formatter;
	}

	// Recover original color
	Terminal::Reset(os);
}


std::string Exception::DemangleSymbol(const char* const symbol)
{
	const std::unique_ptr<char, decltype(&std::free)> demangled(
			abi::__cxa_demangle(symbol, 0, 0, 0 ), &std::free);
	return demangled ? demangled.get() : symbol;
}


void Exception::SaveCallStack()
{
	// Get back trace and symbol names
	void *addresses[256];
	const int n = backtrace(addresses,
			std::extent<decltype(addresses)>::value);
	const std::unique_ptr<char *, decltype(&std::free)> symbols(
			backtrace_symbols(addresses, n), &std::free);

	// Code below obtained from:
	// http://stackoverflow.com/questions/19190273/how-to-print-call-stack-
	// in-c-c-more-beautifully
	//
	// We start at two, in order to skip the two top levels of the stack,
	// which are this function and the constructor of the exception.
	for (int i = 2; i < n; ++i)
	{
		char *const symbol = symbols.get()[i];
		char *end = symbol;

		while (*end)
			++end;
		while (end != symbol && *end != '+')
			--end;
		char *begin = end;
		while (begin != symbol && *begin != '(')
			--begin;

		call_stack += utils::fmt("[%d] ", i - 1);
		if (begin != symbol)
		{
			call_stack += std::string(symbol, ++begin - symbol);
			*end++ = '\0';
			call_stack += DemangleSymbol(begin) + '+' + end;
		}
		else
		{
			call_stack += symbol;
		}
		call_stack += '\n';
	}
}


}

