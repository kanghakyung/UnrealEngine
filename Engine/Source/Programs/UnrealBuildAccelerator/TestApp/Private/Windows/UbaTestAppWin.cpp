// Copyright Epic Games, Inc. All Rights Reserved.

#define WINDOWS_LEAN_AND_MEAN
#include <Windows.h>
#include <wchar.h> 
#include <stdarg.h>
#include <time.h>

int LogError(const wchar_t* format, ...)
{
	va_list args;
	va_start(args, format);
	wchar_t buffer[1024];
	_vsnwprintf_s(buffer, 1024, _TRUNCATE, format, args);
	wprintf(L"%s\n", buffer);
	va_end(args);
	return -1;
}

int wmain(int argc, wchar_t* argv[])
{
	HMODULE detoursHandle = GetModuleHandleW(L"UbaDetours.dll");

	using UbaRequestNextProcessFunc = bool(unsigned int prevExitCode, wchar_t* outArguments, unsigned int outArgumentsCapacity);
	static UbaRequestNextProcessFunc* requestNextProcess = (UbaRequestNextProcessFunc*)(void*)GetProcAddress(detoursHandle, "UbaRequestNextProcess");

	if (argc == 1)
	{
		if (!detoursHandle)
			return LogError(L"Did not find UbaDetours.dll in process!!!\n");

		using UbaRunningRemoteFunc = bool();
		UbaRunningRemoteFunc* runningRemoteFunc = (UbaRunningRemoteFunc*)GetProcAddress(detoursHandle, "UbaRunningRemote");
		if (!runningRemoteFunc)
			return LogError(L"Couldn't find UbaRunningRemote function in UbaDetours.dll");
		bool runningRemote = (*runningRemoteFunc)();

		HMODULE modules[] = { 0, detoursHandle, GetModuleHandleW(L"UbaTestApp.exe") };
		for (HMODULE module : modules)
		{
			DWORD res1 = GetModuleFileNameW(module, NULL, 0);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				return LogError(L"Expected insufficient buffer");
			if (res1 != 0)
				return LogError(L"Expected zero");
			wchar_t name[512];
			DWORD realLen = GetModuleFileNameW(module, name, 512);
			if (realLen == 0)
				return LogError(L"Did not expect this function to fail");
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				return LogError(L"Expected sufficient buffer");
			name[realLen] = 254;
			name[realLen+1] = 254;
			DWORD res2 = GetModuleFileNameW(module, name, realLen);
			if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
				return LogError(L"Expected insufficient buffer");
			if (res2 != realLen)
				return LogError(L"Expected to return same as sent in");
			if (name[realLen] != 254)
				return LogError(L"Overwrite");
			if (name[realLen-1] != 0)
				return LogError(L"Not terminated");
			name[realLen] = 254;
			name[realLen + 1] = 254;
			DWORD res3 = GetModuleFileNameW(module, name, realLen+1);
			if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
				return LogError(L"Expected sufficient buffer");
			if (res3 != realLen)
				return LogError(L"Expected to return same as sent in");
			if (name[realLen+1] != 254)
				return LogError(L"Overwrite");
			if (name[realLen] != 0)
				return LogError(L"Not terminated");
		}

		wchar_t currentDir[MAX_PATH];
		DWORD currentDirLen = GetCurrentDirectoryW(MAX_PATH, currentDir);
		if (!currentDirLen)
			return LogError(L"GetCurrentDirectoryW failed");
		currentDir[currentDirLen] = '\\';
		currentDir[currentDirLen + 1] = 0;

		wchar_t notepad[] = L"c:\\windows\\system32\\notepad.exe";
		wchar_t localNotepad[MAX_PATH];
		wcscpy_s(localNotepad, MAX_PATH, currentDir);
		wcscat_s(localNotepad, MAX_PATH, L"notepad.exe");

		if (!CopyFileW(notepad, localNotepad, false))
			return LogError(L"CopyFileW failed");

		{
			HANDLE fh = CreateFileW(localNotepad, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (fh == INVALID_HANDLE_VALUE)
				return LogError(L"Failed to open %s for read", localNotepad);
			wchar_t path[MAX_PATH];
			DWORD res = GetFinalPathNameByHandleW(fh, path, MAX_PATH, 0);
			if (!res)
				return LogError(L"GetFinalPathNameByHandleW failed");
			if (res != wcslen(path))
				return LogError(L"GetFinalPathNameByHandleW did not return length of string");
			DWORD res2 = GetFinalPathNameByHandleW(fh, path, res, 0);
			if (res2 != res + 1)
				return LogError(L"GetFinalPathNameByHandleW should return full length plus terminating character");
			DWORD res3 = GetFinalPathNameByHandleW(fh, path, res+1, 0);
			if (res3 != res)
				return LogError(L"GetFinalPathNameByHandleW should return full length plus terminating character");
			// TODO: Test character after terminator char

			if (!runningRemote)
				GetFinalPathNameByHandleW(fh, path, MAX_PATH, VOLUME_NAME_NT); // Testing so it doesn't assert

			CloseHandle(fh);
		}

		{
			wchar_t testPath[] = L"R:.";
			wchar_t fullPathName[MAX_PATH];
			DWORD len = GetFullPathNameW(testPath, MAX_PATH, fullPathName, NULL);
			if (len != 3)
				return LogError(L"GetFullPathNameW failed");
			testPath[0] = currentDir[0];
			DWORD len2 = GetFullPathNameW(testPath, MAX_PATH, fullPathName, NULL);
			if (len2 != currentDirLen)
				return LogError(L"GetFullPathNameW returns length that does not match current dir");
			if (memcmp(fullPathName, currentDir, len*sizeof(wchar_t)) != 0)
				return LogError(L"GetFullPathNameW returned wrong path");
			// TODO: Test character after terminator char
		}

		{
			HANDLE fh = CreateFileW(L"FileW", GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);
			if (fh == INVALID_HANDLE_VALUE)
				return LogError(L"Failed to create file File");
			CloseHandle(fh);
			if (!MoveFile(L"FileW", L"FileW2"))
				return LogError(L"Failed to move file from FileW to FileW2");

			if (!CopyFile(L"FileW2", L"FileWF", false))
				return LogError(L"Failed to copy file from FileW2 to FileWF");
		}

		{
			if (!CreateDirectoryW(L"DirA", NULL))
				return LogError(L"Failed to create directory");

			if (GetFileAttributesW(L"DirA") == 0)
				return LogError(L"Failed to get attributes of directory");

			if (!RemoveDirectoryW(L"DirA"))
				return LogError(L"Failed to remove directory");

			if (GetFileAttributesW(L"DirA") != INVALID_FILE_ATTRIBUTES)
				return LogError(L"Found attributes of deleted directory");

			if (CreateDirectoryW(L"Dir2\\Dir3", NULL))
				return LogError(L"Should not succeed creation directory that exists");
			if (GetLastError() != ERROR_ALREADY_EXISTS)
				return LogError(L"Did not get correct error when failing to create existing directory");
			if (GetFileAttributesW(L"Dir2\\Dir3\\Dir4\\Dir5") == INVALID_FILE_ATTRIBUTES)
				return LogError(L"Failed to get attributes of directory");
		}


		{
			STARTUPINFOW si;
			memset(&si, 0, sizeof(si));
			PROCESS_INFORMATION pi;
			memset(&pi, 0, sizeof(pi));
			wchar_t arg[1024];
			wcscpy_s(arg, 1024, argv[0]);
			wcscat_s(arg, 1024, L" -child");
			if (!CreateProcessW(nullptr, arg, nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi))
				return LogError(L"Failed to create child process");
			CloseHandle(pi.hThread);
			
			if (WaitForSingleObject(pi.hProcess, 10000) != WAIT_OBJECT_0)
				return LogError(L"Failed waiting for child process");

			DWORD exitCode;
			if (!GetExitCodeProcess(pi.hProcess, &exitCode) || exitCode)
				return LogError(L"Child process failed");
			CloseHandle(pi.hProcess);
		}

	}
	else if (wcscmp(argv[1], L"-child") == 0)
	{
		if (GetFileAttributes(L"FileW2") == INVALID_FILE_ATTRIBUTES)
			return LogError(L"Child process could not get attributes of FileW2");
		if (GetFileAttributes(L"FileWF") == INVALID_FILE_ATTRIBUTES)
			return LogError(L"Child process could not get attributes of FileWF");
		if (GetFileAttributes(L"FileW") != INVALID_FILE_ATTRIBUTES)
			return LogError(L"Child process found FileW which should not exist anymore");
	}
	else if (wcscmp(argv[1], L"-reuse") == 0)
	{
		wchar_t arguments[1024];
		if (requestNextProcess(0, arguments, sizeof(arguments)))
			return LogError(L"Didn't expect another process");
	}
	else if (wcsncmp(argv[1], L"-file=", 6) == 0)
	{
		wchar_t arguments[1024];
		const wchar_t* file = argv[1] + 6;
		while (true)
		{
			HANDLE rh = CreateFileW(file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
			if (rh == INVALID_HANDLE_VALUE)
				return LogError(L"Failed to open file %s", file);
			char data[17] = {};
			DWORD bytesRead;
			if (!ReadFile(rh, data, 16, &bytesRead, NULL) || bytesRead != 16)
				return LogError(L"Failed to read 16 bytes from file %s", file);
			CloseHandle(rh);

			srand(GetProcessId(GetCurrentProcess()));
			Sleep(rand() % 2000);
			wchar_t outFile[1024];
			wcscpy_s(outFile, 1024, file);
			outFile[wcslen(file)-3] = 0;
			wcscat_s(outFile, 1024, L".out");

			HANDLE wh = CreateFileW(outFile, GENERIC_WRITE, FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL);
			if (wh == INVALID_HANDLE_VALUE)
				return LogError(L"Failed to create file File");
			data[16] = 1;
			DWORD bytesWritten;
			if (!WriteFile(wh, data, 17, &bytesWritten, NULL) || bytesWritten != 17)
				return LogError(L"Failed to read 16 bytes from file %s", file);

			CloseHandle(wh);

			// Request new process
			if (!requestNextProcess(0, arguments, 1024))
				break; // No process available, exit loop
			file = arguments + 6;
		}

		return 0;
	}
	else if (wcsncmp(argv[1], L"-GetFileAttributes=", 6) == 0)
	{
		const wchar_t* str = argv[1] + 19;
		DWORD attr = GetFileAttributesW(str);
		return attr == INVALID_FILE_ATTRIBUTES ? 255 : attr;
	}
	else if (wcsncmp(argv[1], L"-stdout=", 8) == 0)
	{
		const wchar_t* str = argv[1] + 8;
		if (wcscmp(str, L"rootprocess") == 0)
		{
			STARTUPINFOW si;
			memset(&si, 0, sizeof(si));
			PROCESS_INFORMATION pi;
			memset(&pi, 0, sizeof(pi));
			wchar_t arg[1024];
			wcscpy_s(arg, 1024, argv[0]);
			wcscat_s(arg, 1024, L" -stdout=childprocess");
			//wcscpy_s(arg, 1024, L"\"c:\\sdk\\AutoSDK/HostWin64/Win64/MetalDeveloperTools/4.1/metal/macos/bin/metal.exe\" -v --target=air64-apple-darwin18.7.0 16384");

			SECURITY_ATTRIBUTES saAttr;
			saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
			saAttr.bInheritHandle = TRUE;
			saAttr.lpSecurityDescriptor = NULL;
			HANDLE readPipe;
			HANDLE writePipe;
			if (!CreatePipe(&readPipe, &writePipe, &saAttr, 0))
				return 1;

			if (!SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0))
				return 2;

			si.dwFlags = STARTF_USESTDHANDLES;
			si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
			si.hStdOutput = writePipe;
			si.hStdError = writePipe;

			DWORD flags = 0;//CREATE_NO_WINDOW;
			if (!CreateProcessW(nullptr, arg, nullptr, nullptr, TRUE, flags, nullptr, nullptr, &si, &pi))
				return 3;
			CloseHandle(pi.hThread);
			CloseHandle(writePipe);

			char buf[4096] = { 0 };
			DWORD readCount = 0;
			if (!::ReadFile(readPipe, buf, sizeof(buf) - 1, &readCount, NULL))
			{
				LogError(L"Failed to read pipe %u %u", GetLastError(), readCount);
				return 4;
			}
			buf[readCount] = 0;
			if (strncmp(buf, "childprocess", 12) != 0)
				return 5;

			if (WaitForSingleObject(pi.hProcess, INFINITE) != WAIT_OBJECT_0)
				return 6;
			CloseHandle(pi.hProcess);
		}
		wprintf(L"%s\n", str);
	}
	else
	{
		if (!detoursHandle)
			return LogError(L"Did not find UbaDetours.dll in process!!!\n");

		using u32 = unsigned int;
		using UbaSendCustomMessageFunc = u32(const void* send, u32 sendSize, void* recv, u32 recvCapacity);

		UbaSendCustomMessageFunc* sendMessage = (UbaSendCustomMessageFunc*)GetProcAddress(detoursHandle, "UbaSendCustomMessage");
		if (!sendMessage)
			return LogError(L"Couldn't find UbaSendCustomMessage function in UbaDetours.dll");

		const wchar_t* helloMsg = L"Hello from client";
		wchar_t response[256];
		u32 responseSize = (*sendMessage)(helloMsg, u32(wcslen(helloMsg)) * 2, response, 256 * 2);
		if (responseSize == 0)
			return LogError(L"Didn't get proper response from session");
		//wprintf(L"Recv: %.*s\n", responseSize / 2, response);
	}

	return 0;
}
