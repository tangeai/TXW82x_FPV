#ifdef _WIN32
# include <windows.h>
DWORD WINAPI timeGetTime(void) { return GetTickCount(); }
#else
/* POSIX: zbar's timer.h uses clock_gettime/gettimeofday, nothing needed */
#endif
