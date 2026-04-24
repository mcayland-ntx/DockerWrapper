//
// DockerWrapper.cpp
//
// Generate a docker.exe wrapper around nerdctl for use on Jenkins with the
// docker-workflow-plugin. It basically acts as a passthrough except for when
// invoked as docker --version, at which point it injects a higher version
// number so that the docker-workflow-plugin will use the working codepath
// for Windows containers.
//
// See https://github.com/jenkinsci/docker-workflow-plugin/pull/370 for more
// background and an upstream patch that should work around it.
//
// Heavily inspired by https://learn.microsoft.com/en-us/windows/win32/procthread/creating-a-child-process-with-redirected-input-and-output
// and https://learn.microsoft.com/en-us/windows/win32/ipc/multithreaded-pipe-server.
//
// Written by Mark Cave-Ayland <mark.caveayland@nutanix.com>
//
// License: MIT
//

#include "windows.h"
#include <stdio.h>
#include <string.h>
#include <string>

// The buffer size to use when reading from the child
#define BUFSIZE 4096

// The version of docker that we want to appear as
#define DOCKER_VERSION "18"


int wmain(int argc, wchar_t *argv[])
{
    BOOL success;
    BOOL isversion = FALSE;
    STARTUPINFO si;
    PROCESS_INFORMATION pi;
    std::wstring cmdLine = L"nerdctl";

    ZeroMemory(&si, sizeof(STARTUPINFO));
    si.cb = sizeof(STARTUPINFO);
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    if (argc == 1) {
        isversion = FALSE;
    } else if (argc > 1 && !wcscmp(argv[1], L"--version")) {
        isversion = TRUE;
    }

    // Build up whole command line
    for (int i = 1; i < argc; i++) {
        cmdLine += L" ";
        cmdLine += argv[i];
    }

    if (isversion) {
        // The caller has invoked --version so let's substitute the version
        // number for something that makes the Jenkins docker-workflow-plugin
        // happy using nerdctl on Windows.
        SECURITY_ATTRIBUTES sa = {
            .nLength = sizeof(SECURITY_ATTRIBUTES),
            .lpSecurityDescriptor = NULL,
            .bInheritHandle = TRUE
        };

        HANDLE childStdout_Read = NULL;
        HANDLE childStdout_Write = NULL;
        HANDLE parentStdout = NULL;

        // Get our stdout
        parentStdout = GetStdHandle(STD_OUTPUT_HANDLE);

        // Create pipe to capture child process stdout
        if (!CreatePipe(&childStdout_Read, &childStdout_Write, &sa, 0)) {
            printf("Unable to create childStdout: %d\n", GetLastError());
            return -1;
        }

        // Ensure the read handle is not inherited
        if (!SetHandleInformation(childStdout_Read, HANDLE_FLAG_INHERIT, 0)) {
            printf("Unable to SetHandleInformation");
        }

        // Create the child process, mapping stdout to our pipe above
        si.hStdOutput = childStdout_Write;
        si.dwFlags |= STARTF_USESTDHANDLES;

        success = CreateProcess(
            NULL,
            cmdLine.data(),
            NULL,
            NULL,
            TRUE,
            0,
            NULL,
            NULL,
            &si,
            &pi);

        if (!success) {
            printf("CreateProcess failed (%d).\n", GetLastError());
        }

        // Close child handle process and thread
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        // Close the unused stdout handle to ensure the handle can indicate back to
        // ReadFile when the process has terminated.
        CloseHandle(childStdout_Write);

        // Constantly read from the child process stdout, and write back to the parent
        for (;;) {
            DWORD dwRead;
            DWORD dwWritten;
            CHAR chBuf[BUFSIZE];
            CHAR chTmp[] = { DOCKER_VERSION };
            BOOL bVersionFound = FALSE;

            success = ReadFile(childStdout_Read, chBuf, BUFSIZE, &dwRead, NULL);
            if (!success || dwRead == 0) {
                break;
            }

            if (!bVersionFound) {
                // Check to see if "version" is in the buffer
                char *sstr;
                int pos;
                int digitcnt = 0;

                if ((sstr = strstr(chBuf, "version"))) {
                    pos = (int)(sstr - chBuf + strlen("version "));

                    // See how many major digits there are
                    while (chBuf[pos] != '.') {
                        digitcnt++;
                        pos++;
                    }

                    // Copy all the way from the start of the buffer up to the major digit
                    success = WriteFile(parentStdout, chBuf, pos - 1, &dwWritten, NULL);
                    if (!success) {
                        break;
                    }

                    // Inject the new major version
                    success = WriteFile(parentStdout, chTmp, (DWORD)strlen(chTmp), &dwWritten, NULL);
                    if (!success) {
                        break;
                    }

                    // Copy the remainder of the bufer
                    success = WriteFile(parentStdout, chBuf + pos + digitcnt - 1, dwRead - pos - digitcnt,
                                        &dwWritten, NULL);
                    if (!success) {
                        break;
                    }

                    FlushFileBuffers(parentStdout);
                    bVersionFound = TRUE;
                }
            } else {
                // Write back to the parent as-is
                success = WriteFile(parentStdout, chBuf, dwRead, &dwWritten, NULL);
                if (!success) {
                    break;
                }
            }
        }

        CloseHandle(childStdout_Read);

        return 0;
    } else {
        // Create the child process using parent handles (passthrough)
        success = CreateProcess(
            NULL,
            cmdLine.data(),
            NULL,
            NULL,
            FALSE,
            0,
            NULL,
            NULL,
            &si,
            &pi);

        if (!success) {
            printf("CreateProcess failed (%d).\n", GetLastError());
        }

        WaitForSingleObject(pi.hProcess, INFINITE);

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}
