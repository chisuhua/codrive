#pragma once

#include <iostream>


namespace utils
{

/// Base abstract class for all error messages. This class should not be
/// instantiated directly.
class Exception
{
	// Prefixes added in brackets to the message
	std::string prefixes;

	// Exception type
	std::string type;

	// Error message
	std::string message;

	// Call stack is saved here
	std::string call_stack;

	// Demangle symbol from stack trace
	static std::string DemangleSymbol(const char* const symbol);

public:

	/// Constructor
	Exception(const std::string &message) : message(message)
	{
	}

	/// Prevent direct instantiations of this class by providing a pure
	/// virtual destructor.
	virtual ~Exception() = 0;

	/// Get the error message in a string format
	std::string getMessage() const { return message; }

	/// Dump the exception into an output stream
	void Dump(std::ostream &os = std::cerr) const;

	/// Alternative syntax for Dump()
	friend std::ostream &operator<<(std::ostream &os, const Exception &e)
	{
		e.Dump(os);
		return os;
	}

	/// Add a prefix in brackets to the final error message. The final error
	/// message will prepend all prefixes in the order they were added. For
	/// example:
	///
	///     [x86] [pid 102] Error: Illegal memory access
	///
	void AppendPrefix(const std::string &prefix)
	{
		prefixes += "[" + prefix + "] ";
	}

	/// Add a prefix in brackets in the beginning of the prefix list. See
	/// AppendPrefix() for details.
	void PrependPrefix(const std::string &prefix)
	{
		prefixes = "[" + prefix + "] " + prefixes;
	}

	/// Set the exception type (e.g., `Error`). This word is displayed after
	/// the prefixes and before the error message, followed by a colon and a
	/// space.
	void setType(const std::string &type) { this->type = type; }

	/// Save the current call stack. This function should be invoked in the
	/// constructor of any derived exception class if the call stack should
	/// be dumped for that particular exception.
	void SaveCallStack();
};


/// Exception thrown for user input error messages. Different simulation modules
/// can create subclasses of this exception that automatically add the module
/// name as a prefix for the exception.
class Error : public Exception
{
public:

	/// Constructor
	Error(const std::string &message) : Exception(message)
	{
		// Set exception type
		setType("Error");
	}
};


/// Exception thrown for program logic error messages. Different modules can
/// create subclasses of this exception that automatically add certain prefixes
/// to the error message.
class Panic : public Exception
{
public:

	/// Constructor
	Panic(const std::string &message) : Exception(message)
	{
		// Set exception type
		setType("Panic");

		// Save the call stack
		SaveCallStack();
	}
};

}


