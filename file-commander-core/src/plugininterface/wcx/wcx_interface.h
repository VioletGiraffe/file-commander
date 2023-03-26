#pragma once

#include <Windows.h>

enum OpenMode: int {
	PK_OM_LIST = 0, // Open file for reading of file names only
	PK_OM_EXTRACT = 1 // Open file for processing (extract or test)
};

struct tOpenArchiveDataW {
	WCHAR* ArcName = nullptr; // contains the name of the archive to open
	int OpenMode = PK_OM_LIST; // is set to one of the following values: PK_OM_LIST, PK_OM_EXTRACT
	int OpenResult = -1; // used to return one of the error values if an error occurs
	WCHAR* CmtBuf = nullptr; // The Cmt* variables are for the file comment. They are currently not used by Total Commander, so may be set to NULL
	int CmtBufSize = 0;
	int CmtSize = 0;
	int CmtState = 0;
};

enum PkPluginError : int {
	E_SUCCESS = 0,
	E_END_ARCHIVE = 10, // No more files in archive
	E_NO_MEMORY = 11, // Not enough memory
	E_BAD_DATA = 12, // CRC error in the data of the currently unpacked file
	E_BAD_ARCHIVE = 13, // The archive as a whole is bad, e.g.damaged headers
	E_UNKNOWN_FORMAT = 14, // Archive format unknown
	E_EOPEN = 15, // Cannot open existing file
	E_ECREATE = 16, // Cannot create file
	E_ECLOSE = 17, // Error closing file
	E_EREAD = 18, // Error reading from file
	E_EWRITE = 19, // Error writing to file
	E_SMALL_BUF = 20, // Buffer too small
	E_EABORTED = 21, // Function aborted by user
	E_NO_FILES = 22, // No files found
	E_TOO_MANY_FILES = 23, // Too many files to pack
	E_NOT_SUPPORTED = 24 // Function not supported
};

struct tHeaderDataExW {
	WCHAR ArcName[1024] = { 0 };
	WCHAR FileName[1024] = { 0 };
	int Flags = 0;
	unsigned int PackSize = 0;
	unsigned int PackSizeHigh = 0;
	unsigned int UnpSize = 0;
	unsigned int UnpSizeHigh = 0;
	int HostOS = 0;
	int FileCRC = 0;
	int FileTime = 0;
	int UnpVer = 0;
	int Method = 0;
	int FileAttr = 0;
	WCHAR* CmtBuf = nullptr;
	int CmtBufSize = 0;
	int CmtSize = 0;
	int CmtState = 0;
	WCHAR Reserved[1024] = { 0 };
};

/*
ArcName, FileName, PackSize, UnpSize contain the name of the archive, the name of the file within the archive, size of the file when packed, and the size of the file when extracted, respectively.
HostOS is there for compatibility with unrar.dll only, and should be set to zero.
FileCRC is the 32-bit CRC (cyclic redundancy check) checksum of the file. If not available, set to zero.
The Cmt* values can be used to transfer file comment information. They are currently not used in Total Commander, so they may be set to zero.
FileAttr can be set to any combination of the following values:
Value Description
0x1 Read-only file
0x2 Hidden file
0x4 System file
0x8 Volume ID file
0x10 Directory
0x20 Archive file
0x3F Any file
FileTime contains the date and the time of the file's last update. Use the following algorithm to set the value:
FileTime = (year - 1980) << 25 | month << 21 | day << 16 | hour << 11 | minute << 5 | second/2;
Make sure that:
year is in the four digit format between 1980 and 2100
month is a number between 1 and 12
hour is in the 24 hour format
*/

/************************************************
					Functions
************************************************/

// OpenArchive should return a unique handle representing the archive. The handle should remain valid until CloseArchive is called. If an error occurs, you should return zero, and specify the error by setting OpenResult member of ArchiveData.
// You can use the ArchiveData to query information about the archive being open, and store the information in ArchiveData to some location that can be accessed via the handle.
using OpenArchiveWFuncPtr = HANDLE (__stdcall*) (tOpenArchiveDataW *ArchiveData);
using CloseArchiveFuncPtr = int (__stdcall*) (HANDLE hArcData);

// Totalcmd calls ReadHeader to find out what files are in the archive.
using ReadHeaderExWFuncPtr = int (__stdcall*) (HANDLE hArcData, tHeaderDataExW *HeaderData);
/*
	ReadHeader is called as long as it returns zero (as long as the previous call to this function returned zero). Each time it is called, HeaderData is supposed to provide Totalcmd with information about the next file contained in the archive. When all files in the archive have been returned, ReadHeader should return E_END_ARCHIVE which will prevent ReaderHeader from being called again. If an error occurs, ReadHeader should return one of the error values or 0 for no error.
hArcData contains the handle returned by OpenArchive. The programmer is encouraged to store other information in the location that can be accessed via this handle. For example, you may want to store the position in the archive when returning files information in ReadHeader.
In short, you are supposed to set at least PackSize, UnpSize, FileTime, and FileName members of tHeaderData. Totalcmd will use this information to display content of the archive when the archive is viewed as a directory.
*/

using CanYouHandleThisFileWFuncPtr = BOOL (__stdcall*) (WCHAR* FileName);

enum ArchiveFileOperation : int {
	PK_SKIP = 0, // Skip this file
	PK_TEST = 1, // Test file integrity
	PK_EXTRACT = 2 // Extract to disk
};
using ProcessFileWFuncPtr = int (__stdcall*) (HANDLE hArcData, int Operation, WCHAR* DestPath, WCHAR* DestName);
