// ParallelCopy.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"

#include "log.h"
CLog g_log(CLog::eS_INFORMATION, "output.log");


int main()
{
	LOG_INFORMATION("Testing logging");
	return 0;
}

