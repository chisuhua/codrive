#include <fstream>
#include <iostream>

#include "debug.h"


namespace utils
{


Debug::Debug()
{
	os = nullptr;
	active = false;
}

Debug::~Debug()
{
	Flush();
	Close();
}


void Debug::Close()
{
	if (os && os != &std::cout && os != &std::cerr)
		delete os;
	os = nullptr;
}


void Debug::setPath(const std::string &path)
{
	// Release previous output stream
	Close();
	this->path = path;

	// Empty file
	if (path.empty())
		return;

	// File is standard output
	if (path == "stdout")
		os = &std::cout;
	else if (path == "stderr")
		os = &std::cerr;
	else
		os = new std::ofstream(path.c_str());

	// Create new output stream
	if (!*os)
	{
		std::cerr << "fatal: cannot open " << path << '\n';
		exit(1);
	}

	//activate the debug
	active = true;
}


void Debug::Flush()
{
	if (os)
		os->flush();
}



}  // namespace utils

