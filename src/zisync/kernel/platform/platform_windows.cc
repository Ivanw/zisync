/**
 * @file platform_linux.cc
 * @author Likun Liu <liulikun@gmail.com>
 *
 * @brief Linux platform implementation.
 *
 * Copyright (C) 2009 Likun Liu <liulikun@gmail.com>
 * Free Software License:
 *
 * All rights are reserved by the author, with the following exceptions:
 * Permission is granted to freely reproduce and distribute this software,
 * possibly in exchange for a fee, provided that this copyright notice appears
 * intact. Permission is also granted to adapt this software to produce
 * derivative works, as long as the modified versions carry this copyright
 * notice and additional notices stating that the work has been modified.
 * This source code may be translated into executable form and incorporated
 * into proprietary software; there is no requirement for such software to
 * contain a copyright notice related to this source.
 *
 * $Id: $
 * $Name: $
 */
#include <WinSock2.h>
#include <Windows.h>
#include <Shlwapi.h>
#include <ShlObj.h>
#include <atlstr.h>
#include <RpcDce.h>
#include <strsafe.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#pragma comment(lib, "IPHLPAPI.lib")

#include <openssl/ssl.h>

#include <stdio.h>
#include <errno.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

#include "zisync_kernel.h"
#include "zisync/kernel/platform/platform.h"


void __cdecl odprintf(const char *format, ...) {
  char buf[1024];
  va_list args;

  va_start(args, format);
  _vsnprintf_s(buf, _TRUNCATE, 1023, format, args);
  va_end(args);

  buf[1023] = '\0';

  OutputDebugStringA(buf);
} 

namespace zs {

static const int64_t kEPOCH_DIFF = 11644473600000L;

static int64_t ConvertToUnixTime(int64_t i64WinTime) {
  assert(i64WinTime > 0);
  return i64WinTime / 10000 - kEPOCH_DIFF;
}

static int64_t ConvertToWinTime(int64_t i64UnixTime) {
  assert(i64UnixTime >= -(11644473600000L));
  return (i64UnixTime + kEPOCH_DIFF) * 10000;
}

/*  exist, no matter file or dir */
bool OsExists(const char* path) {
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  if (PathFileExists(tpath)) {
    return true;
  } else {
    return false;
  }
}
/* exist and is a file */
bool OsFileExists(const char* path) {
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));  

  //PathIsFileSpec return false for tpath has ':''\'
  if (PathFileExists(tpath) && !PathIsDirectory(tpath)) {
    return true;
  } else {
    return false;
  }
}
/* exist and is a dir */
bool OsDirExists(const char *path) {
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  if (PathFileExists(tpath) && PathIsDirectory(tpath)) {
    return true;
  } else {
    return false;
  }
}

static int MoveToTrash(const CString& tpath) {
  if (!PathFileExists(tpath)) {  // Directory Not Exist
    return 0;
  }

  size_t length;
  TCHAR path_to_delete[MAX_PATH + 1];

  StringCchCopy(path_to_delete, MAX_PATH, tpath);
  StringCchLength(path_to_delete, MAX_PATH, &length);

  path_to_delete[length + 1] = _T('\0');

  // Perform Operation
  SHFILEOPSTRUCT file_op;
  ZeroMemory(&file_op, sizeof(file_op));
  file_op.hwnd   = NULL;
  file_op.wFunc  = FO_DELETE;
  file_op.fFlags = FOF_SILENT | FOF_NOCONFIRMATION | FOF_NOERRORUI ;
  file_op.fFlags |= FOF_FILESONLY | FOF_ALLOWUNDO;

  file_op.pFrom  = path_to_delete;

  int ret = SHFileOperation(&file_op);
  if (ret != 0 && PathFileExists(path_to_delete)) {
    return -1;
  }
  return 0;
}

int OsDeleteFile(const char* path, bool move_to_trash) {
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  if (move_to_trash) {
    return MoveToTrash(tpath);
  }

  if (::DeleteFile(tpath)) {
    return 0;
  }

  if (!PathFileExists(tpath)) {
    return 0;
  }

  DWORD dwAttr = GetFileAttributes(tpath);
  if (!(dwAttr & FILE_ATTRIBUTE_READONLY)) {
    return -1;
  }

  // Remove ReadOnly attributes and try again.
  SetFileAttributes(tpath, dwAttr & ~FILE_ATTRIBUTE_READONLY);

  return ::DeleteFile(tpath) ? 0 : -1;
}

int OsDeleteDirectory(const char* path) {
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  BOOL rv = ::RemoveDirectory(tpath);
  if (rv == TRUE) {
    return 0;
  } else {
    if (!PathFileExists(tpath)) {
      return 0;
    }
    return -1;
  }
}

BOOL OsDeleteChildRen(LPCTSTR tpath) {
	assert(PathIsDirectory(tpath));

	HANDLE hFind;
	WIN32_FIND_DATA FileData;
	TCHAR path_to_delete[MAX_PATH + 1];
	TCHAR path_buffer[MAX_PATH + 1];
	StringCchCopy(path_to_delete, MAX_PATH, tpath);
	StringCchCat(path_to_delete, MAX_PATH, _T("\\*"));

	hFind = ::FindFirstFile(path_to_delete, &FileData);
	if (hFind == INVALID_HANDLE_VALUE) {
		return FALSE;
	}

	BOOL rc = TRUE;
	do {
		if ( (_tcscmp(FileData.cFileName, _T("."))==0)
			|| (_tcscmp(FileData.cFileName, _T(".."))==0))
			continue;
		StringCchCopy(path_buffer, MAX_PATH, tpath);
		StringCchCat(path_buffer, MAX_PATH, _T("\\"));
		StringCchCat(path_buffer, MAX_PATH, FileData.cFileName);
		
		if (PathIsDirectory(path_buffer)) {
			 rc |= OsDeleteChildRen(path_buffer);
			 rc |= ::RemoveDirectory(path_buffer);
		} else {
			rc |= DeleteFile(path_buffer);
		}
	} while (rc && ::FindNextFile(hFind, &FileData));
	::FindClose(hFind);

	return rc;
}

int OsDeleteDirectories(const char* path, bool delete_self /* = true */) {
	CString tpath = CA2T(path, CP_UTF8);
	tpath.Replace(_T('/'), _T('\\'));

	if (!PathFileExists(tpath)) {  // Directory Not Exist
		return 0;
	}

	if (PathIsDirectory(tpath)) {
		if (!OsDeleteChildRen(tpath)) {
			return -1;
		}
		if (delete_self) {
			if (!::RemoveDirectory(tpath)) {
				return -1;
			}
		}
		return 0;
	} else {
		if (delete_self) {
			if (!::DeleteFile(tpath)) {
				return -1;
			}
		}
		return 0;
	}
}

int OsCreateDirectory(const char* path, bool delete_file_existing) {
  //  std::replace(sRpath.begin(), sRpath.end(), '\\', '/');
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));
  if (PathFileExists(tpath)) {
    if (PathIsDirectory(tpath)) {
      return 0;
    } else {
      if (delete_file_existing == false) {
        return -1;
      }

      zs::OsDeleteFile(path, false);

	  if(CreateDirectory(tpath, NULL)) {
	    return 0;
	  } else {
        return -1;
	  }
    }
  } else {
    if (OsCreateDirectory(OsDirName(path), delete_file_existing) == 0) {
		if (CreateDirectory(tpath, NULL)) {
			return 0;
		} else {
			return -1;
		}
	} else {
	  return -1;
	}
  }
}


int OsPathAppend(char* path1, int path1_capacity, const char* path2) {
  assert(path1 != NULL && path2 != NULL);
  int length = static_cast<int>(strlen(path1));

  if (path1[length - 1] == '/') {
#if defined _WIN32 || defined _WIN64
    _snprintf_s(path1 + length, _TRUNCATE,
                path1_capacity - length, "%s", path2);
#else
    snprintf(path1 + length, path1_capacity - length, "%s", path2);
#endif
  } else {
    path1[length] = '/';
    path1[length + 1] = '\0';
#if defined _WIN32 || defined _WIN64
    _snprintf_s(path1 + length + 1, _TRUNCATE,
                path1_capacity - length -2, "%s", path2);
#else
    snprintf(path1 + length + 1,
             path1_capacity - length -2, "%s", path2);
#endif
  }

  return 0;
}

int OsPathAppend(std::string* path1, const std::string& path2) {
  assert(path1 != NULL);

  if (path1->size() > 0) {
    if (path1->at(path1->size() - 1) == '/') {
      path1->append(path2);
    } else {
      path1->append(1, '/');
      path1->append(path2);
    }
  } else {
    path1->append(path2);
  }

  return 0;
}

int OsGetFullPath(const char* relative_path, std::string* full_path) {
  full_path->resize(MAX_PATH);
  // @TODO: buggy, call GetFullPathName instead.
  std::string::size_type nbytes = GetFullPathNameA(
      relative_path, MAX_PATH, const_cast<char*>(full_path->data()), NULL);
  if (nbytes <= 0) {
    return -1;
  }

  if (nbytes < MAX_PATH) {
    full_path->resize(nbytes);
  } else {
    full_path->resize(nbytes + 1);
    nbytes = GetFullPathNameA(
        relative_path, nbytes, const_cast<char*>(full_path->data()), NULL);
    assert(nbytes > 0);
  }
  std::replace(full_path->begin(), full_path->end(), '\\', '/');

  return 0;
}

BOOL DirName(LPTSTR tpath, TCHAR tpath_seprator = _T('\\'))
{
  TCHAR* p = _tcsrchr(tpath, tpath_seprator);
  if (p == NULL) {
    return FALSE;
  }

  if (p == tpath) {									// \foo
    *(p + 1) = _T('\0');
    return TRUE;
  } 

  if (p == (tpath + 2) && tpath[1] == _T(':')) {	// X:\foo
    *(p + 1) = _T('\0');
    return TRUE;
  }

  *p = _T('\0');
  return TRUE;
}

LPCTSTR BaseName(LPCTSTR tpath, TCHAR tpath_seprator = _T('\\') )
{
  TCHAR* p = _tcsrchr((TCHAR*) tpath, tpath_seprator);
  if (p == NULL) {
    return tpath;
  }

  if (*(p + 1) == _T('\0')) {									// \foo
    return p;
  } 

  return p+1;
}

int OsRename(const char* src, const char* dst, bool is_overwrite /* = true */) {
  DWORD flags = MOVEFILE_COPY_ALLOWED;
  if (is_overwrite) {
    flags |= MOVEFILE_REPLACE_EXISTING;
  }

  CString tsrc = CA2T(src, CP_UTF8);
  CString tdst = CA2T(dst, CP_UTF8);
  tsrc.Replace(_T('/'), _T('\\'));
  tdst.Replace(_T('/'), _T('\\'));

  BOOL ok = MoveFileEx(tsrc, tdst, flags);
  if (ok) {
    return 0;
  }
  DWORD last_error = GetLastError();
  const char* error_msg = OsGetLastErr();


  // check whether parent directory exists 
  TCHAR parent_dir[MAX_PATH];
  StringCchCopy(parent_dir, MAX_PATH, tdst);
  if (!DirName(parent_dir)) {
    return -1;
  }

  if (!PathFileExists(parent_dir)) {
    DWORD ret = SHCreateDirectory(NULL, parent_dir);
    if (ret == ERROR_SUCCESS) {
      ok = MoveFileEx(tsrc, tdst, flags);
    }
  }

  return ok ? 0 : -1;
}

FILE* OsFopen(const char* path, const char* mode) {
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));
  return fopen(CT2A(tpath), mode);
}

int OsMutex::Initialize() {
  InitializeCriticalSection(&critical_section_);
  return 0;
}

int OsMutex::Initialize(int type) {
  return Initialize();
}

int OsMutex::CleanUp() {
  DeleteCriticalSection(&critical_section_);
  return 0;
}

int OsMutex::AquireMutex() {
  EnterCriticalSection(&critical_section_);
  return 0;
}

bool OsMutex::TryAquireMutex() {
  return TryEnterCriticalSection(&critical_section_) ? true : false;
}

int OsMutex::ReleaseMutex() {
  LeaveCriticalSection(&critical_section_);
  return 0;
}

int OsRwLock::s_has_srw_ = -1;
InitializeSRWLockPtr OsRwLock::SRWInit = NULL;
ReleaseSRWLockExclusivePtr OsRwLock::SRWEndWrite = NULL;
ReleaseSRWLockSharedPtr OsRwLock::SRWEndRead = NULL;
AcquireSRWLockExclusivePtr OsRwLock::SRWStartWrite = NULL;
AcquireSRWLockSharedPtr OsRwLock::SRWStartRead = NULL;

int OsRwLock::Initialize() {
  if (s_has_srw_ == -1) {
    HMODULE hModule = LoadLibrary(_T("KERNEL32.DLL"));
    SRWInit = (InitializeSRWLockPtr)GetProcAddress(hModule, "InitializeSRWLock");
    if(SRWInit != NULL) {
      s_has_srw_ = 1;
      SRWEndWrite = (ReleaseSRWLockExclusivePtr) GetProcAddress(hModule, "ReleaseSRWLockExclusive");
      SRWEndRead = (ReleaseSRWLockSharedPtr) GetProcAddress(hModule, "ReleaseSRWLockShared");
      SRWStartWrite = (AcquireSRWLockExclusivePtr) GetProcAddress(hModule, "AcquireSRWLockExclusive");
      SRWStartRead = (AcquireSRWLockSharedPtr) GetProcAddress(hModule, "AcquireSRWLockShared");
    } else {
      s_has_srw_ = 0;
    }
    FreeModule(hModule);
  }

  if (s_has_srw_ == 1) {  // If >= Vista
    SRWInit(&srw_lock_);
  } else {                // If xp
    assert(s_has_srw_ == 0);

    m_cReaders = 0;
    InitializeCriticalSection(&m_csWrite);
    InitializeCriticalSection(&m_csReaderCount);
    m_hevReadersCleared = CreateEvent(NULL, TRUE, TRUE, NULL);
  }    
  return 0;
}

int OsRwLock::CleanUp() {
  if (s_has_srw_ == 0) {  // If xp
    WaitForSingleObject(m_hevReadersCleared, INFINITE);
    CloseHandle(m_hevReadersCleared);
    DeleteCriticalSection(&m_csWrite);
    DeleteCriticalSection(&m_csReaderCount);
  } else {
    assert(s_has_srw_ == 1);  // If >= Vista
  }
  return 0;
}


int OsRwLock::AquireRdLock() {
  if (s_has_srw_ == 1) {  // If >= Vista
    SRWStartRead(&srw_lock_);
  } else {  // If xp
    assert(s_has_srw_ == 0);
    EnterCriticalSection(&m_csWrite);
    EnterCriticalSection(&m_csReaderCount);
    if (++m_cReaders == 1)
      ResetEvent(m_hevReadersCleared);
    LeaveCriticalSection(&m_csReaderCount);
    LeaveCriticalSection(&m_csWrite);
  }
  return 0;
}

int OsRwLock::AquireWrLock() {
  if (s_has_srw_ == 1) {  // If >= Vista
    SRWStartWrite(&srw_lock_);
  } else {  // If xp
    assert(s_has_srw_ == 0);
    EnterCriticalSection(&m_csWrite);
    WaitForSingleObject(m_hevReadersCleared,INFINITE);
  }

  return 0;
}

int OsRwLock::ReleaseRdLock() {
  if (s_has_srw_ == 1) {  // If >= Vista
    SRWEndRead(&srw_lock_);
  } else {  // If xp
    assert(s_has_srw_ == 0);
    EnterCriticalSection(&m_csReaderCount);
    if (--m_cReaders == 0)
      SetEvent(m_hevReadersCleared);
    LeaveCriticalSection(&m_csReaderCount);
  }

  return 0;
}

int OsRwLock::ReleaseWrLock() {
  if (s_has_srw_ == 1) {  // If >= Vista
    SRWEndWrite(&srw_lock_);
  } else {  // If xp
    assert(s_has_srw_ == 0);
    LeaveCriticalSection(&m_csWrite);
  }
  return 0;
}


//
// Usage: SetThreadName (-1, "MainThread");
//
const DWORD MS_VC_EXCEPTION = 0x406D1388;

#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
  DWORD dwType;      // Must be 0x1000.
  LPCSTR szName;     // Pointer to name (in user addr space).
  DWORD dwThreadID;  // Thread ID (-1=caller thread).
  DWORD dwFlags;     // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

void SetThreadName(DWORD dwThreadID, const char* threadName) {
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = threadName;
  info.dwThreadID = dwThreadID;
  info.dwFlags = 0;

  __try {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info)/sizeof(ULONG_PTR), (ULONG_PTR*)&info); // NOLINT
  } __except(EXCEPTION_EXECUTE_HANDLER) {
  }
}



UINT WINAPI OsThread::ThreadEntry(LPVOID lpParam) {
  // OsThread* t = static_cast<OsThread *>(lpParam);
  // // save the handle since t may be deleted in Run()
  // HANDLE thread_handle = t->thread_handle_;
  // 
  // int rc = t->Run();
  // 
  // // try to close the handle if possible.
  // CloseHandle(thread_handle);
  // return rc;
  return static_cast<OsThread*>(lpParam)->Run();
}

OsThread::OsThread(
    const char* thread_name /* = NULL */)
: thread_id_(0), thread_handle_(INVALID_HANDLE_VALUE)
    , started_(false), thread_name_(thread_name) {
    }

OsThread::~OsThread() {
  //
  // NOTE: no need to call _endthreadex(); it will be called automatically
  //
  if (thread_handle_ != INVALID_HANDLE_VALUE) {
    //
    // NOTE: do not call WaitForSingleObject(thread_handle_ ...), this will cause
    //       deadlock if the thread try to delete itsself in its Run() function.
    //
    // if(started_) {
    //   WaitForSingleObject(thread_handle_, INFINITE);
    //   started_ = false;
    // }
    CloseHandle(thread_handle_);
    thread_handle_ = INVALID_HANDLE_VALUE;
  }


}

int OsThread::Startup() {
  thread_handle_ = reinterpret_cast<HANDLE>(
      _beginthreadex(NULL, 0, ThreadEntry, this, CREATE_SUSPENDED, &thread_id_));
  assert(thread_handle_ != INVALID_HANDLE_VALUE);

  if (!thread_name_.empty()) {
    SetThreadName(thread_id_, thread_name_.c_str());
  }

  started_ = true;
  DWORD nResumeCode = ResumeThread(thread_handle_);
  assert(nResumeCode != 0xFFFFFFFF);
  return zs::ZISYNC_SUCCESS;
}

int OsThread::Shutdown() {
  if (thread_handle_ != INVALID_HANDLE_VALUE) {
    if (started_) {
      WaitForSingleObject(thread_handle_, INFINITE);
      started_ = false;
    }
  }
  return zs::ZISYNC_SUCCESS;
}

UINT WINAPI OsWorkerThread::ThreadFunc(LPVOID lpParam) {
  int ret = 0;
  OsWorkerThread* thread = static_cast<OsWorkerThread*>(lpParam);
  if (thread->runnable_) {
    ret = thread->runnable_->Run();
    if (thread->auto_delete_) {
      delete thread->runnable_;
      thread->runnable_ = NULL;
      thread->auto_delete_ = false;
    }
  }
  return ret;
}

OsWorkerThread::OsWorkerThread(const char* thread_name,
                               IRunnable* runnable,
                               bool auto_delete)
: runnable_(runnable), auto_delete_(auto_delete)
    , thread_handle_(NULL), thread_id_(0)
    ,started_(false), thread_name_(thread_name) {
    }

OsWorkerThread::~OsWorkerThread() {
  //
  // NOTE: no need to call _endthreadex(); it will be called automatically
  //
  if (thread_handle_ != INVALID_HANDLE_VALUE) {
    //
    // NOTE: do not call WaitForSingleObject(thread_handle_ ...), this will cause
    //       deadlock if the thread try to delete itsself in its Run() function.
    //
    // if(started_) {
    //   WaitForSingleObject(thread_handle_, INFINITE);
    //   started_ = false;
    // }
    CloseHandle(thread_handle_);
    thread_handle_ = INVALID_HANDLE_VALUE;
  }
}

int OsWorkerThread::Startup() {
  thread_handle_ = reinterpret_cast<HANDLE>(
      _beginthreadex(NULL, 0, ThreadFunc, this, CREATE_SUSPENDED, &thread_id_));

  if (!thread_name_.empty()) {
    SetThreadName(thread_id_, thread_name_.c_str());
  }
  DWORD nResumeCode = ResumeThread(thread_handle_);
  assert(nResumeCode != 0xFFFFFFFF);
  started_ = true;
  return zs::ZISYNC_SUCCESS;
}

int OsWorkerThread::Shutdown() {
  if (thread_handle_ != INVALID_HANDLE_VALUE) {
    if (started_) {
      WaitForSingleObject(thread_handle_, INFINITE);
      started_ = false;
    }
  }
  return zs::ZISYNC_SUCCESS;
}

OsTcpSocket::OsTcpSocket(const char* uri) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ != INVALID_SOCKET);
  uri_ = uri;
}

OsTcpSocket::OsTcpSocket(const std::string& uri) {
  fd_ = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd_ != INVALID_SOCKET);
  uri_ = uri;
}

OsTcpSocket::~OsTcpSocket() {
  if (fd_ != INVALID_SOCKET) {
    closesocket(fd_);
  }
}

int OsTcpSocket::Bind() {
  struct sockaddr_in servaddr;
  size_t pos_first_colon = uri_.find(":");
  assert(pos_first_colon > 0 && pos_first_colon < uri_.length() - 2);
  size_t pos_second_colon = uri_.find(":", pos_first_colon + 1);
  assert(pos_second_colon > pos_first_colon + 1 &&
         pos_second_colon < uri_.length() - 1);

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(atoi(uri_.substr(pos_second_colon + 1).c_str()));
  std::string addr =
      uri_.substr(pos_first_colon + 3, pos_second_colon - pos_first_colon - 3);
  if (addr == "*") {
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  } else {
    // int ret = inet_aton(AF_INET, addr.c_str(), &servaddr.sin_addr.s_addr);
    // assert(ret == 1);
    servaddr.sin_addr.s_addr = inet_addr(addr.c_str());
  }

  // int optval = 1;
  // if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
  //   return -1;
  // }
  int ret = bind(fd_, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret == -1 && WSAGetLastError() == WSAEADDRINUSE) {
    return EADDRINUSE;
  } else {
    return ret;
  }
}

int OsTcpSocket::Listen(int backlog) {
  return listen(fd_, backlog);
}

int OsTcpSocket::Accept(OsTcpSocket **accepted_socket) {
  struct sockaddr_in sockaddr;
  int length = sizeof(sockaddr);
  SOCKET connfd = accept(fd_, (struct sockaddr*)&sockaddr, &length);
  int ret_ = WSAGetLastError();

  if (connfd == -1) {
    *accepted_socket = NULL;
    return -1;
  }

  const int URI_LEN = 64;
  char uri[URI_LEN] = {0};
  char addr[INET_ADDRSTRLEN] = {0};
  int32_t port = ntohs(sockaddr.sin_port);
  const char *ret = inet_ntoa(sockaddr.sin_addr);
  assert(ret != NULL);
  int ret_val = _snprintf_s(uri, _TRUNCATE, sizeof(uri), "tcp://%s:%" PRId32, addr, port);
  assert(ret_val > 0 && ret_val < URI_LEN);
  OsTcpSocket* socket = new OsTcpSocket(uri);
  assert(socket != NULL);
  socket->SetSocket(static_cast<int>(connfd));  // Safe cast
  *accepted_socket = socket;

  return 0;
}

int OsTcpSocket::Connect() {
  struct sockaddr* sa;
  OsSocketAddress address(uri_);

  for(sa = address.NextSockAddr(); sa != NULL; sa = address.NextSockAddr()) {
    int ret = connect(fd_, sa, sizeof(*sa));
    if (ret == 0) {
      return ret;
    } else {
      DWORD error = WSAGetLastError();
      odprintf("connet() faid: %d", error);
    }
  }

  return -1;
}

int OsTcpSocket::GetSockOpt(
    int level, int optname, void* optval, int* optlen) {
  return getsockopt(
      fd_, level, optname, reinterpret_cast<char*>(optval), optlen);
}

int OsTcpSocket::SetSockOpt(
    int level, int optname, const void *optval, int optlen) {
  return setsockopt(
      fd_, level, optname, reinterpret_cast<const char*>(optval), optlen);
}

int OsTcpSocket::Shutdown(const char* how) {
  if (strcmp(how, "r") == 0) {
    return shutdown(fd_, SD_RECEIVE);
  } else if (strcmp(how, "w") == 0) {
    return shutdown(fd_, SD_SEND);
  } else if (strcmp(how, "rw") == 0) {
    return shutdown(fd_, SD_BOTH);
  }

  return -1;
}
int OsTcpSocket::Send(const char *buffer, int length, int flags) {
  return send(fd_, buffer, length, flags);
}

int OsTcpSocket::Send(const std::string &buffer, int flags) {
  int buffer_length = static_cast<int>(buffer.length());
  return send(fd_, buffer.data(), buffer_length, flags);
}

int OsTcpSocket::Recv(char *buffer, int length, int flags) {
  return recv(fd_, buffer, length, flags);
}

int OsTcpSocket::Recv(std::string *buffer, int flags) {
  char recv_buffer[512 + 1] = {0};
  int length = recv(fd_, recv_buffer, 512, flags);
  if (length == -1) {
    return -1;
  }
  *buffer = recv_buffer;

  return length;
}

void OsTcpSocket::SetSocket(int socket) {
  fd_ = socket;
}

class SslTcpSocket : public OsTcpSocket {
 public:
  explicit SslTcpSocket(const std::string& uri, void* ctx)
      : OsTcpSocket(uri) {
        uri_ = uri;
        ctx_ = reinterpret_cast<SSL_CTX*>(ctx);
        ssl_ = SSL_new(ctx_);
        assert(ssl_ != NULL);
      }

  virtual ~SslTcpSocket() {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
  }

  virtual int Accept(OsTcpSocket** accepted_socket) {
    struct sockaddr_in sockaddr;
    int length = 0;
    SOCKET connfd = accept(fd_, (struct sockaddr*)&sockaddr, &length);
    if (connfd == -1) {
      *accepted_socket = NULL;
      return -1;
    }

    char uri[30] = {0};
    char addr[INET_ADDRSTRLEN] = {0};
    int16_t port = ntohs(sockaddr.sin_port);
    const char *ret = inet_ntoa(sockaddr.sin_addr);
    assert(ret != NULL);
    int ret_val = _snprintf_s(uri, _TRUNCATE, sizeof(uri), "tcp://%s:%d", addr, port);
    assert(ret_val > 0 && ret_val < 30);
    SslTcpSocket* socket = new SslTcpSocket(uri, ctx_);
    assert(socket != NULL);
    socket->SetSocket(static_cast<int>(connfd));  // safe cast
    *accepted_socket = socket;

    return socket->Accept();
  }

  /* @return : 0 if success, EADDRINUSE if addr in use, others -1*/
  virtual int Connect() {
    if (OsTcpSocket::Connect() == -1) {
      return -1;
    }

    if (SSL_set_fd(ssl_, static_cast<int>(fd_)) != 1) {  // safe cast
      return -1;
    }
    if (SSL_connect(ssl_) != 1) {
      return -1;
    }

    return 0;
  }
  /**
   * @param how: how to shutdown, "r", "w", and "rw"
   */
  virtual int Send(const char *buffer, int length, int flags) {
    return SSL_write(ssl_, buffer, length);
  }

  virtual int Recv(char *buffer, int length, int flags) {
    return SSL_read(ssl_, buffer, length);
  }

 protected:
  int Accept() {
    if (SSL_set_fd(ssl_, static_cast<int>(fd_)) != 1) {  // safe cast
      return -1;
    }
    if (SSL_accept(ssl_) != 1) {
      return -1;
    }

    return 0;
  }

 private:
  SSL* ssl_;
  SSL_CTX* ctx_;
};

class FileDumyTcpSocket : public OsTcpSocket {
 public:
  explicit FileDumyTcpSocket(const std::string& uri)
      : OsTcpSocket(uri), fp_(NULL) {
        first_read_ = 1;
        open_read_file_ = 0;
      }
  virtual ~FileDumyTcpSocket() {
    if (fp_ != NULL) {
      fclose(fp_);
    }
  }

  // the addr should be proto://IP:Port
  virtual int Bind() {
    return 0;
  }

  virtual int Listen(int /* backlog */) {
    return 0;
  }
  virtual int Accept(OsTcpSocket * /* accepted_socket */) {
    return 0;
  }

  virtual int Shutdown(const char* how) {
    return 0;
  }

  virtual int Connect() {
    size_t index = uri_.find(":");
    if (static_cast<int>(index) == -1 ||
        index == 0 ||
        index == uri_.length() - 1) {
      return -1;
    }

    if (uri_.at(index + 1) != '/' || uri_.at(index + 2) != '/') {
      return -1;
    }

    if (uri_.length() == index + 3) {
      return -1;
    }

    file_name_ = uri_.substr(index + 3);
    if (fopen_s(&fp_, uri_.substr(index + 3).c_str(), "w") == 0) {
      return -1;
    }

    return 0;
  }

  virtual int Send(const char *buffer, int length, int flags) {
    return static_cast<int>(fwrite(buffer, 1, length, fp_));
  }

  virtual int Send(const std::string &buffer, int flags) {
    return static_cast<int>(
        fwrite(buffer.data(), 1, buffer.length(), fp_));
  }

  virtual int Recv(char *buffer, int length, int flags) {
    return static_cast<int>(fread(buffer, 1, length, fp_));
  }

  virtual int Recv(std::string *buffer, int flags) {
    return 0;
  }

 private:
  std::string file_name_;
  int first_read_;
  int open_read_file_;
  FILE* fp_;

  FileDumyTcpSocket(FileDumyTcpSocket&);
  void operator=(FileDumyTcpSocket&);
};

OsTcpSocket* OsTcpSocketFactory::Create(
    const std::string& uri, void* arg) {
  if (strncmp(uri.c_str(), "tcp", 3) == 0 && arg == NULL) {
    return new OsTcpSocket(uri);
  } else if (strncmp(uri.c_str(), "file", 4) == 0) {
    return new FileDumyTcpSocket(uri);
  } else if (strncmp(uri.c_str(), "tcp", 3) == 0 && arg != NULL) {
    return new SslTcpSocket(uri, arg);
  }

  return NULL;
}

OsUdpSocket::OsUdpSocket(const char* uri) {
  fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  assert(fd_ != INVALID_SOCKET);
  uri_ = uri;
}

OsUdpSocket::OsUdpSocket(const std::string& uri) {
  fd_ = socket(AF_INET, SOCK_DGRAM, 0);
  assert(fd_ != INVALID_SOCKET);
  uri_ = uri;
}

OsUdpSocket::~OsUdpSocket() {
  if (fd_ != INVALID_SOCKET) {
    closesocket(fd_);
  }
}


int OsUdpSocket::Bind() {
  assert (fd_ != INVALID_SOCKET);
  struct sockaddr_in servaddr;
  size_t pos_first_colon = uri_.find(":");
  assert(pos_first_colon > 0 && pos_first_colon < uri_.length() - 1);
  size_t pos_second_colon = uri_.find(":", pos_first_colon + 1);
  assert(pos_second_colon > pos_first_colon + 1 &&
         pos_second_colon < uri_.length() - 1);

  servaddr.sin_family = AF_INET;
  servaddr.sin_port = htons(atoi(uri_.substr(pos_second_colon + 1).c_str()));
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);

  // int optval = 1;
  // if (setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
  //   return -1;
  // }
  int ret = bind(fd_, (struct sockaddr *)&servaddr, sizeof(servaddr));
  if (ret == -1 && WSAGetLastError() == WSAEADDRINUSE) {
    return EADDRINUSE;
  } else {
    return ret;
  }
}

int OsUdpSocket::Shutdown(const char* how) {
  if (strcmp(how, "r") == 0) {
    return shutdown(fd_, SD_RECEIVE);
  } else if (strcmp(how, "w") == 0) {
    return shutdown(fd_, SD_SEND);
  } else if (strcmp(how, "rw") == 0) {
    return shutdown(fd_, SD_BOTH);
  }

  return -1;
}

int OsUdpSocket::EnableBroadcast() {
  //  Ask operating system to let us do broadcasts from socket
  int on = 1;
  return setsockopt (
      fd(), SOL_SOCKET, SO_BROADCAST, (const char*)&on, sizeof (on));
}

int OsUdpSocket::EnableMulticast(const std::string& multicast_addr) {
  struct ip_mreq mreq;
  mreq.imr_multiaddr.s_addr=inet_addr(multicast_addr.c_str());
  mreq.imr_interface.s_addr=htonl(INADDR_ANY);
  if (setsockopt(fd(),IPPROTO_IP,IP_ADD_MEMBERSHIP,
                 (const char*)&mreq, sizeof(mreq)) < 0) {
    return -1;
  }
  return 0;
}


int OsUdpSocket::RecvFrom(string *buffer, int flags, string *src_addr) {
  buffer->resize(2048);
  struct sockaddr addr;
  socklen_t addr_length = sizeof(addr);
  int nbytes = recvfrom(
      fd_, const_cast<char*>(buffer->data()), 2048, flags, &addr, &addr_length);
  if (nbytes >= 0) {
    buffer->resize(nbytes);
    if (src_addr != NULL) {
      src_addr->assign(reinterpret_cast<char*>(&addr), addr_length);
    }
    return nbytes;
  } else {
    buffer->reserve(0);
    // error occur
    // ZSLOG_ERROR("recvfrom failed(%d): %s", errno, strerror(errno));
    DWORD last_error = WSAGetLastError();
    // fprintf(stderr, "recvfrom failed(%d)", last_error);
    if (last_error == 10054) {
      return 0;
    }
    return -1;
  }
}

// AtomicInt64::AtomicInt64(int64_t init_value) {
//   InitializeCriticalSection(&mutex_);
// 
//   EnterCriticalSection(&mutex_);
//   value_ = init_value;
//   LeaveCriticalSection(&mutex_);
// }
// 
// AtomicInt64::~AtomicInt64() {
//   DeleteCriticalSection(&mutex_);
// }
// 
// int64_t AtomicInt64::value() {
//   EnterCriticalSection(&mutex_);
//   int64_t value = value_;
//   LeaveCriticalSection(&mutex_);
// 
//   return value;
// }
// void AtomicInt64::set_value(int64_t new_value) {
//   EnterCriticalSection(&mutex_);
//   value_ = new_value;
//   LeaveCriticalSection(&mutex_);
// }
// 
// int64_t AtomicInt64::FetchAndInc(int64_t count) {
//   EnterCriticalSection(&mutex_);
//   int64_t value = value_;
//   value_ += count;
//   LeaveCriticalSection(&mutex_);
// 
//   return value;
// }
// 
// int64_t AtomicInt64::FetchAndSub(int64_t count) {
//   EnterCriticalSection(&mutex_);
//   int64_t value = value_;
//   value_ -= count;
//   LeaveCriticalSection(&mutex_);
// 
//   return value;
// }
// 
// AtomicInt32::AtomicInt32(int32_t init_value) {
//   InitializeCriticalSection(&mutex_);
// 
//   EnterCriticalSection(&mutex_);
//   value_ = init_value;
//   LeaveCriticalSection(&mutex_);
// }
// 
// AtomicInt32::~AtomicInt32() {
//   DeleteCriticalSection(&mutex_);
// }
// 
// int32_t AtomicInt32::value() {
//   EnterCriticalSection(&mutex_);
//   int32_t value = value_;
//   LeaveCriticalSection(&mutex_);
// 
//   return value;
// }
// void AtomicInt32::set_value(int32_t new_value) {
//   EnterCriticalSection(&mutex_);
//   value_ = new_value;
//   LeaveCriticalSection(&mutex_);
// }
// 
// int32_t AtomicInt32::FetchAndInc(int32_t count) {
//   EnterCriticalSection(&mutex_);
//   int32_t value = value_;
//   value_ += count;
//   LeaveCriticalSection(&mutex_);
// 
//   return value;
// }
// 
// int32_t AtomicInt32::FetchAndSub(int32_t count) {
//   EnterCriticalSection(&mutex_);
//   int32_t value = value_;
//   value_ -= count;
//   LeaveCriticalSection(&mutex_);
// 
//   return value;
// }

int OsGetThreadId() {
  return static_cast<int>(GetCurrentThreadId());
}

bool OsGetTimeString(char* buffer, int buffer_length) {
  SYSTEMTIME lt;
  GetLocalTime(&lt);

  _snprintf_s(
      buffer, _TRUNCATE, buffer_length, "%4d%02d%02d+%02d:%02d:%02d",
      lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);

  return true;
}

void OsGenUuid(std::string* uuid) {
  UUID uuid_buffer;
  RPC_CSTR szUuid;
  UuidCreate(&uuid_buffer);
  UuidToStringA(&uuid_buffer, &szUuid);
  *uuid = reinterpret_cast<char*>(szUuid);
  RpcStringFreeA(&szUuid);
}

int64_t OsTimeInS() {
  return time(NULL);
}

int64_t OsTimeInMs() {
  // Note: some broken versions only have 8 trailing zero's,
  // the correct epoch has 9 trailing zero's
  static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

  SYSTEMTIME  system_time;
  FILETIME    file_time;
  uint64_t    time;
  uint64_t    millisecond;

  GetSystemTime( &system_time );
  SystemTimeToFileTime( &system_time, &file_time );
  time =  ((uint64_t)file_time.dwLowDateTime )      ;
  time += ((uint64_t)file_time.dwHighDateTime) << 32;

  millisecond = ((time - EPOCH) / 10000L);
  millisecond += (system_time.wMilliseconds);
  return millisecond;
}


int OsGetHostname(std::string* hostname) {
  TCHAR buffer[256];
  DWORD length = 256;
  if (GetComputerNameEx(ComputerNameDnsHostname, buffer, &length)) {
    *hostname = CT2A(buffer, CP_UTF8);
    return 0;
  } else {
    hostname->clear();
    return -1;
  }
}

const char* OsGetLastErr() {
  static char message_buffer[512];
  LPVOID lpMsgBuf;
  DWORD dw = GetLastError(); 

  FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | 
      FORMAT_MESSAGE_FROM_SYSTEM |
      FORMAT_MESSAGE_IGNORE_INSERTS,
      NULL,
      dw,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      (LPSTR) &lpMsgBuf,
      0, NULL );

  _snprintf_s(message_buffer, _TRUNCATE, 512, "%s", lpMsgBuf);

  LocalFree(lpMsgBuf);

  return message_buffer;	
}

static BOOL GetLastWriteTimeUnix(LPCTSTR lpszPath, int64_t* lpi64UnixTime)
{
  HANDLE hFile;
  FILETIME ftWrite;

  hFile = CreateFile(
      lpszPath, 
      FILE_READ_ATTRIBUTES, 
      FILE_SHARE_READ, 
      NULL,
      OPEN_EXISTING, 
      FILE_FLAG_BACKUP_SEMANTICS, 
      NULL
      );
  if(hFile == INVALID_HANDLE_VALUE)
  {
    return FALSE;
  }

  CHandle hFileAuto(hFile);

  // Retrieve the file times for the file.
  if (!GetFileTime(hFile, NULL, NULL, &ftWrite)) {
    // CloseHandle(hFile);
    return FALSE;
  }

  LARGE_INTEGER liTime;
  liTime.HighPart = ftWrite.dwHighDateTime;
  liTime.LowPart = ftWrite.dwLowDateTime;

  *lpi64UnixTime = ConvertToUnixTime(liTime.QuadPart);

  return TRUE;
}

#define SUPPORTED_FILE_ATTRIBUTES (FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED|FILE_ATTRIBUTE_OFFLINE|FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_SYSTEM|FILE_ATTRIBUTE_TEMPORARY)
#define SUPPORTED_DIR_ATTRIBUTES  (FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_HIDDEN|FILE_ATTRIBUTE_NOT_CONTENT_INDEXED|FILE_ATTRIBUTE_READONLY|FILE_ATTRIBUTE_SYSTEM)


DWORD NormalizeFileAttributes(DWORD dwAttributes, BOOL bIsDirectory) {
  if (bIsDirectory) {
    return dwAttributes & SUPPORTED_DIR_ATTRIBUTES;
  } 

  DWORD dwRetVal = dwAttributes & SUPPORTED_FILE_ATTRIBUTES;
  if (dwRetVal == 0) {
    // dwRetVal = FILE_ATTRIBUTE_NORMAL;
    dwRetVal = 0;
  }

  return dwRetVal;
}

class CWin32FileAttr {
 public:
  CWin32FileAttr(LPCTSTR lpszPath);

  CWin32FileAttr(LPWIN32_FIND_DATA lpFindData);

  CWin32FileAttr(
      DWORD dwAttributes, 
      int64_t i64LastWriteTimeUnix, 
      int64_t i64FileSize) {
    m_bInitialized = TRUE;
    m_dwAttributes = dwAttributes;
    m_i64FileSize = i64FileSize;
    m_i64UnixTime = i64LastWriteTimeUnix;
  }

  BOOL IsInit() {
    return m_bInitialized;
  }

  BOOL IsDir() {
    return (m_dwAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
  }

  int64_t LastWriteTimeUnix() 
  {
    return m_i64UnixTime;
  }

  int64_t FileSize() 
  {
    return m_i64FileSize;
  }

  DWORD Attributes() {
    return NormalizeFileAttributes(m_dwAttributes, IsDir());
  }

 protected:
  BOOL   m_bInitialized;
  DWORD  m_dwAttributes;
  int64_t m_i64UnixTime;
  int64_t m_i64FileSize;

};

CWin32FileAttr::CWin32FileAttr( LPCTSTR lpszPath )
{
  WIN32_FILE_ATTRIBUTE_DATA AttrData;
  m_bInitialized = GetFileAttributesEx(
      lpszPath, GetFileExInfoStandard, &AttrData);

  if (!m_bInitialized) {
    // ZSLOG(ERROR, "GetFileAttributesEx() Failed[%d]", ::GetLastError());
    return;
  }

  LARGE_INTEGER liFileSize, liFileTime;

  m_dwAttributes = AttrData.dwFileAttributes;

  liFileSize.HighPart = AttrData.nFileSizeHigh;
  liFileSize.LowPart  = AttrData.nFileSizeLow;
  m_i64FileSize = liFileSize.QuadPart;

  liFileTime.HighPart = AttrData.ftLastWriteTime.dwHighDateTime;
  liFileTime.LowPart = AttrData.ftLastWriteTime.dwLowDateTime;
  m_i64UnixTime = ConvertToUnixTime(liFileTime.QuadPart);
}

CWin32FileAttr::CWin32FileAttr( LPWIN32_FIND_DATA lpFindData )
{
  LARGE_INTEGER liFileSize, liFileTime;

  m_dwAttributes = lpFindData->dwFileAttributes;

  liFileSize.HighPart = lpFindData->nFileSizeHigh;
  liFileSize.LowPart  = lpFindData->nFileSizeLow;
  m_i64FileSize = liFileSize.QuadPart;

  liFileTime.HighPart = lpFindData->ftLastWriteTime.dwHighDateTime;
  liFileTime.LowPart = lpFindData->ftLastWriteTime.dwLowDateTime;
  m_i64UnixTime = ConvertToUnixTime(liFileTime.QuadPart);

  m_bInitialized = TRUE;
}

VOID CALLBACK OsTimer::TimerRoute(PVOID lpParam, BOOLEAN TimerOrWaitFired) {
  UNREFERENCED_PARAMETER(TimerOrWaitFired);
  IOnTimer *on_timer = reinterpret_cast<IOnTimer*>(lpParam);
  on_timer->OnTimer();
}

int OsTimer::Initialize() {
  if (!CreateTimerQueueTimer(&timer_, NULL, 
                             (WAITORTIMERCALLBACK)TimerRoute, timer_func_, 
                             due_time_in_ms_, 
                             interval_in_ms_, WT_EXECUTEINTIMERTHREAD)) {
    // ZSLOG_ERROR("CreateTimerQueueTimer() Failed[%d].", GetLastError());
    return -1;
  }
  return 0;
}

int OsTimer::CleanUp() {
  if (timer_ != INVALID_HANDLE_VALUE) {
    DeleteTimerQueueTimer(NULL, timer_, INVALID_HANDLE_VALUE);
    timer_ = INVALID_HANDLE_VALUE;
  }
  return 0;
}

int OsTimeOut::CleanUp() {
  if (timer_ != INVALID_HANDLE_VALUE) {
    DeleteTimerQueueTimer(NULL, timer_, NULL);
    timer_ = INVALID_HANDLE_VALUE;
  }
  return 0;
}

int OsStat(const std::string& path, const std::string &alias, 
	OsFileStat* file_stat) {
  CString tpath = CA2T(path.data(), CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  CWin32FileAttr Attr(tpath);
  if(Attr.IsInit() == FALSE) {
    if (PathFileExists(tpath) == FALSE) {
      return ENOENT;
    }
    return EIO;
  }

  file_stat->path = path;
  file_stat->mtime = Attr.LastWriteTimeUnix();
  file_stat->length = Attr.FileSize();
  file_stat->attr = Attr.Attributes();
  file_stat->type = Attr.IsDir() ? OS_FILE_TYPE_DIR : OS_FILE_TYPE_REG;

  return 0;
}

void MakeOsStat(OsFileStat* file_stat,
                LPCTSTR tpath, LPWIN32_FIND_DATA find_data) {
  file_stat->path = CT2A(tpath, CP_UTF8);
  std::replace(file_stat->path.begin(), file_stat->path.end(), '\\', '/');

  CWin32FileAttr Attr(find_data);
  assert(Attr.IsInit());

  file_stat->mtime = Attr.LastWriteTimeUnix();
  file_stat->length = Attr.FileSize();
  file_stat->attr = Attr.Attributes();
  file_stat->type = Attr.IsDir() ? OS_FILE_TYPE_DIR : OS_FILE_TYPE_REG;
}

static LPCTSTR PathCombine(
    LPTSTR szResultBuffer, LPCTSTR szFirstPath, LPCTSTR szSecondPath) {

  StringCchCopy(szResultBuffer, MAX_PATH, szFirstPath);
  if (PathAppend(szResultBuffer, szSecondPath)) {
    return szResultBuffer;
  } else {
    assert(FALSE);
    return NULL;
  }
}

BOOL OsFsTraverser::TraverseRecurcively(LPCTSTR szSubtree)
{
  HANDLE hFind;
  WIN32_FIND_DATA FileData;
  TCHAR szPathFilter[MAX_PATH + 1];
  TCHAR szPathBuffer[MAX_PATH + 1];

  // Construct filter
  zs::PathCombine(szPathFilter, szSubtree, _T("\\*"));

  // Find all file
  hFind = ::FindFirstFile(szPathFilter, &FileData);
  if (hFind == INVALID_HANDLE_VALUE) 
    return TRUE;
  do {
    // Skip "." and ".."
    if ( (_tcscmp(FileData.cFileName, _T("."))==0)
        || (_tcscmp(FileData.cFileName, _T(".."))==0))
      continue;

    // Construct full-pathname
    OsFileStat file_stat;
    zs::PathCombine(szPathBuffer, szSubtree, FileData.cFileName);
    MakeOsStat(&file_stat, szPathBuffer, &FileData);

    if (visitor->IsIgnored(file_stat.path)) {
      continue;
    }

    if (visitor->Visit(file_stat) < 0) {
      return false;
    }

    // lpTooltip->OnProgressUpdate(1, ArkGetShortPath(szPathBuffer));

    if (FileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (!TraverseRecurcively(szPathBuffer)) {
        return FALSE;
      }
    }
  } while (::FindNextFile(hFind, &FileData));
  ::FindClose(hFind);

  return TRUE;
}

// Not include the root path
int OsFsTraverser::traverse() {
  CString tpath = CA2T(root.data(), CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  TraverseRecurcively(tpath);

  return 0;
}

const char* OsFile::ModeRead = "rb";
const char* OsFile::ModeWrite = "wb";

int OsFile::Open(const std::string& path, const std::string &alias, const char* mode)
{
  CString tpath = CA2T(path.c_str(), CP_UTF8);
  DWORD access=0;
  DWORD creatiton_dispotision=0;
  ChangeToWinMode(mode, &access, &creatiton_dispotision);

  pfile_ = CreateFile(tpath, access, 0, NULL, 
	  creatiton_dispotision, FILE_ATTRIBUTE_NORMAL, NULL);
  if (pfile_ == INVALID_HANDLE_VALUE) {
    CloseHandle(pfile_);
	return -1;
  }
	
  return 0;
}

size_t OsFile::Read(std::string* buffer)
{
  assert(pfile_ != INVALID_HANDLE_VALUE);
  void* ptr = &(*buffer->begin());
  DWORD nmemb = static_cast<DWORD>(buffer->size());
  DWORD nbytes = 0;
  
  BOOL result = ReadFile(pfile_, ptr, nmemb, &nbytes, NULL);
  if (result && (nbytes < nmemb)) {
	  file_end_ = 1;
  }

  if (nbytes != nmemb) {
	  buffer->resize(static_cast<size_t>(nbytes));
  }
  return static_cast<size_t>(nbytes);
}

size_t OsFile::Read(char *buf, size_t length)
{
  assert(pfile_ != INVALID_HANDLE_VALUE);
  DWORD nbytes=0;
  BOOL result = ReadFile(pfile_, buf, static_cast<DWORD>(length), &nbytes, NULL);
  if (result && (nbytes < length)) {
	  file_end_ = 1;
  }

  return static_cast<size_t>(nbytes);
}

size_t OsFile::Write(const std::string &buffer)
{
  assert(pfile_ != INVALID_HANDLE_VALUE);
  DWORD nmemb = static_cast<DWORD>(buffer.size());
  DWORD nbytes=0;
  WriteFile(pfile_, &(*buffer.begin()), nmemb, &nbytes, NULL);
  
  return static_cast<size_t>(nbytes);
}

size_t OsFile::Write(const char *data, size_t length)
{
  assert(pfile_ != INVALID_HANDLE_VALUE);
  DWORD nbytes=0;
  WriteFile(pfile_, data, static_cast<DWORD>(length), &nbytes, NULL);

  return static_cast<size_t>(nbytes);
}

size_t OsFile::ReadWholeFile(std::string* buffer)
{
  assert(pfile_ != INVALID_HANDLE_VALUE);
  if (pfile_ == INVALID_HANDLE_VALUE) {
    return -1;
  }

  DWORD buffer_size_high;
  DWORD buffer_size = GetFileSize(pfile_, &buffer_size_high);
  assert(buffer_size_high == 0);

  DWORD nbytes=0;
  buffer->resize(buffer_size);
  ReadFile(pfile_, &(*buffer->begin()), buffer_size, &nbytes, NULL);
  file_end_ = 1;

  return static_cast<size_t>(nbytes);
}

void OsFile::ChangeToWinMode(const char* mode, LPDWORD access, LPDWORD creation_disposition)
{
  if (strcmp(mode, OsFile::ModeRead) == 0) {
    *access = GENERIC_READ;
	*creation_disposition = OPEN_EXISTING;
  } else if (strcmp(mode, OsFile::ModeWrite) == 0) {
	*access = GENERIC_WRITE;
	*creation_disposition = OPEN_ALWAYS;
  }
}

int OsChmod(const char *path, int32_t new_attr) {
  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  DWORD new_attr_dw = new_attr;
  DWORD attr = GetFileAttributes(tpath); 
  if (attr==INVALID_FILE_ATTRIBUTES) return -1; 

  BOOL is_dir = attr & FILE_ATTRIBUTE_DIRECTORY;
  if (NormalizeFileAttributes(attr, is_dir) == new_attr_dw) {
    return 0;
  }

  if (is_dir) {
    attr = (attr & ~(SUPPORTED_DIR_ATTRIBUTES)) | new_attr_dw;
    attr |= FILE_ATTRIBUTE_DIRECTORY;
  } else {
    attr = (attr & ~(SUPPORTED_FILE_ATTRIBUTES)) | new_attr_dw;
    assert(!(attr & FILE_ATTRIBUTE_DIRECTORY));
  }

  if (!SetFileAttributes(tpath, attr)) {
    assert(FALSE);
    return -1;
  } 

  return 0;
}

int OsSetMtime(const char *path, int64_t mtime_in_ms) {
  LARGE_INTEGER liWinTime;
  liWinTime.QuadPart = ConvertToWinTime(mtime_in_ms);

  FILETIME ftLastWriteTime;

  ftLastWriteTime.dwHighDateTime = liWinTime.HighPart;
  ftLastWriteTime.dwLowDateTime  = liWinTime.LowPart;

  CString tpath = CA2T(path, CP_UTF8);
  tpath.Replace(_T('/'), _T('\\'));

  HANDLE hFile = CreateFile(tpath,
                            //GENERIC_READ|GENERIC_WRITE,
                            FILE_WRITE_ATTRIBUTES,
                            FILE_SHARE_READ|FILE_SHARE_DELETE,
                            NULL, OPEN_EXISTING,
                            FILE_FLAG_BACKUP_SEMANTICS,
                            NULL);
  if (hFile == INVALID_HANDLE_VALUE) {
    return -1;
  }

  CHandle hFileAuto(hFile);

  if (!SetFileTime(hFile, NULL, NULL, &ftLastWriteTime)) {
    return -1;
  }
  return 0;
}



// FUCK: windows has no equivalent for mkdtemp(). Both tempname() and 
// GetTempFileName can not be used. tempname() will generate result 
// according to environment TMP. GetTempFileName() will create the tmp file.
// FUCK Again!!!. We MUST implment our own.
bool OsTempPath(const string& dir, const string& prefix, string* tmp_path) {
  CString tdir = CA2T(dir.data(), CP_UTF8);
  CString tprefix = CA2T(prefix.data(), CP_UTF8);

  TCHAR   tmp_name[MAX_PATH]  = { 0 };

//  tdir.Replace(_T('/'), _T('\\'));

  srand((unsigned)time( NULL ));
  do {
    _sntprintf_s(tmp_name,
                 _TRUNCATE, MAX_PATH, _T("%s/%s%05d"), tdir, tprefix, rand());
  } while (PathFileExists(tmp_name));

  // // Get a temporary filename if one wasn't supplied
  // if(GetTempFileName(tdir, tprefix, 0, tmp_name) == 0) {
  //     return false;
  // }

  *tmp_path = CT2A(tmp_name, CP_UTF8);

  // char *tmp_path_ = tempnam(dir.c_str(), prefix.c_str());
  // if (tmp_path == NULL) {
  // 	return false;
  // }
  // *tmp_path = tmp_path_;
  // std::replace(tmp_path->begin(), tmp_path->end(), '\\', '/');
  // free(tmp_path_);
  return true;
}

int32_t OsGetRandomTcpPort() {
  SOCKET s_fd;
  struct sockaddr_in addr;
  s_fd = socket(AF_INET, SOCK_STREAM, 0);
  if(s_fd == INVALID_SOCKET){
    return -1;
  }
  // int optval = 1;
  // if (setsockopt(s_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int)) < 0) {
  //   return -1;
  // }
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(0);
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  if(bind(s_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    closesocket(s_fd);
    return -1;
  }
  socklen_t addr_len = sizeof(addr);
  if(getsockname(s_fd, (struct sockaddr *)&addr, &addr_len) == -1){
    closesocket(s_fd);
    return -1;
  }

  closesocket(s_fd);
  return ntohs(addr.sin_port); 
}

int SockAddrToString(struct sockaddr_in* sockaddr, std::string* host) {
  if (sockaddr->sin_family != AF_INET && sockaddr->sin_family != AF_INET6) {
    return -1;
  }

  // Save sin_port
  USHORT sin_port = sockaddr->sin_port;
  sockaddr->sin_port = 0;

  DWORD address_length;
  if (sockaddr->sin_family == AF_INET) {
    address_length = sizeof(SOCKADDR_IN);
  } else { // AF_INET6
    address_length = sizeof(SOCKADDR_IN6);
  }  

  host->resize(INET_ADDRSTRLEN);
  DWORD string_length = INET_ADDRSTRLEN; 
  int result = ::WSAAddressToStringA(
      (LPSOCKADDR)sockaddr, address_length, NULL, 
      const_cast<char*>(host->data()), &string_length);
  sockaddr->sin_port = sin_port;  // restore sin_port
  if (result != 0) {
    return -1;
  }
  host->resize(string_length);

  return 0;
}

int OsAddHiddenAttr(const char *path) {
  CString tpath = CA2T(path, CP_UTF8);

  DWORD dwAttrs = GetFileAttributes(tpath); 
  if (dwAttrs==INVALID_FILE_ATTRIBUTES) return -1; 

  dwAttrs |= FILE_ATTRIBUTE_HIDDEN;

  if (!SetFileAttributes(tpath, dwAttrs)) {
    assert(false);
    return -1;
  } 
  return 0;
}


OsSocketAddress::OsSocketAddress(const std::string& uri)
  : uri_(uri), result_(NULL), ptr_(NULL) {
    std::string schema, host, port;

    std::string::size_type found1 = uri_.find_first_of("://");
    assert(found1 != std::string::npos);
    if (found1 != std::string::npos) {
      schema = uri_.substr(0, found1);
      found1 += 3;
    } else {
      found1 = 0;
    }

    std::string::size_type found2 = uri_.find_last_of(':');
    assert (found2 != std::string::npos);
    host = uri_.substr(found1, found2 - found1);
    port = uri_.substr(found2 + 1);

    int ret;
    struct addrinfo hints;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    if (schema == "tcp" || schema == "http") {
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;
    } else {
      hints.ai_socktype = SOCK_DGRAM;
      hints.ai_protocol = IPPROTO_UDP;
    }

    ret = getaddrinfo(host.data(), port.data(), &hints, &result_);
    if (ret != 0) {
      return;
    } else {
      ptr_ = result_;
    }
  }

struct sockaddr* OsSocketAddress::NextSockAddr() {
  struct sockaddr* addr = ptr_ ? ptr_->ai_addr : NULL;
  if (ptr_ != NULL) {
    ptr_ = ptr_->ai_next;
  }

  return addr;
}

int ListIpAddresses(
    std::vector<struct sockaddr_in>* ipv4,
    std::vector<struct sockaddr_in6>* ipv6) {
  IP_ADAPTER_ADDRESSES* adapter_buffer = NULL;
  IP_ADAPTER_ADDRESSES* adapter = NULL;
  IP_ADAPTER_UNICAST_ADDRESS* address = NULL;

  // Start with a 16 KB buffer and resize if needed -
  // multiple attempts in case interfaces change while
  // we are in the middle of querying them.
  std::string buffer;
  DWORD buffer_size = 16 * 1024;
  for (int attempts = 0; attempts != 3; ++attempts) {
    buffer.resize(buffer_size);

    DWORD error = ::GetAdaptersAddresses(AF_UNSPEC, 
                                         GAA_FLAG_SKIP_ANYCAST | 
                                         GAA_FLAG_SKIP_MULTICAST | 
                                         GAA_FLAG_SKIP_DNS_SERVER |
                                         GAA_FLAG_SKIP_FRIENDLY_NAME, 
                                         NULL, 
                                         (IP_ADAPTER_ADDRESSES*)buffer.data(),
                                         &buffer_size);
    if (error == ERROR_SUCCESS) {
      break;  			// We're done here, people!
    }	else if (error == ERROR_BUFFER_OVERFLOW) {
      continue; // Try again with the new size
    } else {
      return -1;
    }
  }

  // Iterate through all of the adapters
  adapter = (IP_ADAPTER_ADDRESSES*) buffer.data();
  for (; adapter != NULL; adapter = adapter->Next)	{
    // Skip loopback adapters
    if (adapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK
        || adapter->OperStatus != IfOperStatusUp) {	
      continue;	
    }

    // Parse all IPv4 and IPv6 addresses
    for (address = adapter->FirstUnicastAddress; 
         address != NULL; address = address->Next)	{
      auto family = address->Address.lpSockaddr->sa_family;
      if (family == AF_INET) {
        SOCKADDR_IN* sin = 
            reinterpret_cast<SOCKADDR_IN*>(address->Address.lpSockaddr);
        const BYTE& b0 = sin->sin_addr.S_un.S_un_b.s_b1;
        const BYTE& b1 = sin->sin_addr.S_un.S_un_b.s_b2;
        if (!(b0 == 169 && b1 == 254)) {  // skip 169.254.X.X
          // IPv4
          ipv4->emplace_back(*sin);
        }
      }	else if (family == AF_INET6) {
        // IPv6
        // Detect and skip non-external addresses
        SOCKADDR_IN6* sin6 = 
            reinterpret_cast<SOCKADDR_IN6*>(address->Address.lpSockaddr);

        const BYTE& b0 = sin6->sin6_addr.u.Byte[0];
        const BYTE& b1 = sin6->sin6_addr.u.Byte[1];
        // const BYTE& b2 = sin6->sin6_addr.u.Byte[2];
        const WORD& w0 = sin6->sin6_addr.u.Word[0];
        const WORD& w1 = sin6->sin6_addr.u.Word[1];

        bool is_link_local(false);
        bool is_special_use(false);

        if (b0 == 0xFE && ((b1 & 0xF0) >= 0x80 &&  (b1 & 0xF0) <= 0xb0))	{
          is_link_local = true;
        }
        else if (w0 == 0x0120 && w1 == 0)	{
          is_special_use = true;
        }

        if (! (is_link_local || is_special_use)) {
          ipv6->emplace_back(*sin6);
        }
      }	else {
        // Skip all other types of addresses
        continue;
      }
    }
  }

  return 0;
}

std::string OsDirName(const std::string& path) {
  assert(path.find('\\') == std::string::npos);
  if (path.empty()) {
    return ".";
  }

  std::size_t pos = path.find_last_not_of('/');
  if (pos == std::string::npos) {
    return "/";  // format://////
  }
  pos = path.find_last_of('/', pos);
  if (pos == std::string::npos) {
    if (path.size() > 2 && path.substr(1, 3) == ":/") {
      return path.substr(0, 3);  // format: c:///
    } else if (path.size() == 2 && path.at(1) == ':') {
      return path;  // format: c:
    }else {
      return ".";  // format: usr
    }   
  }

  if (pos == 0) {
    return "/";  // format: /usr///
  }

  return path.substr(0, pos);
}

std::string OsGetMacAddress() {
  IP_ADAPTER_INFO AdapterInfo[16];
  DWORD buf_size = sizeof(AdapterInfo);
  DWORD ret = GetAdaptersInfo(AdapterInfo, &buf_size);	
  assert(ret == ERROR_SUCCESS);
  if (ret != ERROR_SUCCESS)
	return std::string();

  std::string mac_address;
  mac_address.resize(12);
  PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
  do {
    if (pAdapterInfo->Type == MIB_IF_TYPE_ETHERNET) {
	  int ret = _snprintf_s(&(*mac_address.begin()), _TRUNCATE, 12, "%02X%02X%02X%02X%02X%02X", 
		(char*)pAdapterInfo->Address[0], (char*)pAdapterInfo->Address[1],
		(char*)pAdapterInfo->Address[2], (char*)pAdapterInfo->Address[3],
		(char*)pAdapterInfo->Address[4], (char*)pAdapterInfo->Address[5]);
	  assert(ret == 12);
	  break;
	}
	pAdapterInfo = pAdapterInfo->Next;	
  } while (pAdapterInfo);
  
  return mac_address;
}

}  // namespace zs

#include "zisync/kernel/platform/os_file.cc"

extern "C" {

  int gettimeofday(struct timeval * tp, struct timezone * tzp) {
    // Note: some broken versions only have 8 trailing zero's, the correct epoch has 9 trailing zero's
    static const uint64_t EPOCH = ((uint64_t) 116444736000000000ULL);

    SYSTEMTIME  system_time;
    FILETIME    file_time;
    uint64_t    time;

    GetSystemTime( &system_time );
    SystemTimeToFileTime( &system_time, &file_time );
    time =  ((uint64_t)file_time.dwLowDateTime )      ;
    time += ((uint64_t)file_time.dwHighDateTime) << 32;

    tp->tv_sec  = (long) ((time - EPOCH) / 10000000L);
    tp->tv_usec = (long) (system_time.wMilliseconds * 1000);
    return 0;
  }

};
