// vim: set ts=2 sw=2 tw=99 et:
#if defined _shell_included
 #endinput
#endif
#define _shell_included

#include <float>

native void printnum(int n);
native void writenum(int n);
native void printfloat(float n);
native void writefloat(float n);
native void printnums(any:...);
native void print(const char[] str);
native void dump_stack_trace();
native void report_error();
native void unbound_native();
native int donothing();

// Return arg, but through a dynamically generated native.
native int dynamic_native(int arg);

// Read array[x][y] and store in |out|. If compiled with < 1.12, return false.
native bool access_2d_array(int[][] array, int x, int y, int& out);

typedef InvokeCallback = function void ();
// Invoke |fn| up to |count| times, returning false immediately on failure.
native bool invoke(InvokeCallback fn, int count);
// Invoke |fn|, |count| times, returning the number of successful invocations.
native int execute(InvokeCallback fn, int count);

typedef Array2dCallback = function void (const int[][] array, int length, int stride);

// Create a 2D array from the given "flat" array, length and stride.
native void copy_2d_array_to_callback(const int[] flat_array, int length, int stride,
                                      Array2dCallback callback);

enum Handle { INVALID_HANDLE = 0 }
native void CloseHandle(Handle h);
using __intrinsics__.Handle;
