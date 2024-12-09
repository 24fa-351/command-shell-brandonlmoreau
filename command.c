#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <ctype.h>
#include <direct.h>

#include "command.h"
#include "environment.h"

#ifndef MAX_ARGUMENTS
#define MAX_ARGUMENTS 128
#endif

#ifndef MAX_PIPELINE_COMMANDS
#define MAX_PIPELINE_COMMANDS 20
#endif

#ifndef INVALID_FILE_ATTRIBUTES_VALUE
#define INVALID_FILE_ATTRIBUTES_VALUE ((DWORD)-1)
#endif

#ifndef READ_MODE
#define READ_MODE GENERIC_READ
#endif

#ifndef WRITE_MODE
#define WRITE_MODE GENERIC_WRITE
#endif

#ifndef FILE_SHARE_FOR_READ
#define FILE_SHARE_FOR_READ FILE_SHARE_READ
#endif

#ifndef OPEN_EXISTING_FILE
#define OPEN_EXISTING_FILE OPEN_EXISTING
#endif

#ifndef CREATE_ALWAYS_FILE
#define CREATE_ALWAYS_FILE CREATE_ALWAYS
#endif

typedef struct CommandExecutionOptions
{
    char* inputFile;
    char* outputFile;
    int runInBackground;
} CommandExecutionOptions;

typedef struct ProcessInfo
{
    PROCESS_INFORMATION pi;
} ProcessInfo;

static int verifyFileExecutable(const char* candidateFile)
{
    DWORD fileAttr = GetFileAttributesA(candidateFile);
    if (fileAttr == INVALID_FILE_ATTRIBUTES_VALUE)
    {
        return 0;
    }
    return 1;
}

char** retrieveSystemPathList(void)
{
    char* fetchedPath = NULL;
    size_t pathLength = 0;
    _dupenv_s(&fetchedPath, &pathLength, "PATH");
    if (fetchedPath == NULL)
    {
        char** fallbackPaths = (char**)malloc(sizeof(char*) * 2);
        if (!fallbackPaths)
        {
            return NULL;
        }
        fallbackPaths[0] = _strdup(".");
        fallbackPaths[1] = NULL;
        return fallbackPaths;
    }

    char** paths = (char**)malloc(sizeof(char*) * 128);
    if (!paths)
    {
        free(fetchedPath);
        return NULL;
    }

    int countPaths = 0;
    {
        char* pathContext = NULL;
        char* singlePath = strtok_s(fetchedPath, ";", &pathContext);
        while (singlePath != NULL && countPaths < 127)
        {
            paths[countPaths] = _strdup(singlePath);
            if (!paths[countPaths])
            {
                for (int i = 0; i < countPaths; i++)
                {
                    free(paths[i]);
                }
                free(paths);
                free(fetchedPath);
                return NULL;
            }
            countPaths++;
            singlePath = strtok_s(NULL, ";", &pathContext);
        }
        paths[countPaths] = NULL;
    }

    free(fetchedPath);
    return paths;
}

void freePathList(char** paths)
{
    if (!paths) return;
    int i = 0;
    while (paths[i] != NULL)
    {
        free(paths[i]);
        i++;
    }
    free(paths);
}

static char* locateCommandPath(const char* cmdName, char** pathList)
{
    if (!pathList || !cmdName) return NULL;

    if (strchr(cmdName, '\\') != NULL || strchr(cmdName, '/') != NULL)
    {
        if (verifyFileExecutable(cmdName))
        {
            return _strdup(cmdName);
        }
        else
        {
            return NULL;
        }
    }

    int hasExt = (strrchr(cmdName, '.') != NULL) ? 1 : 0;

    char candidatePath[1024];
    int i = 0;
    while (pathList[i] != NULL)
    {
        _snprintf_s(candidatePath, sizeof(candidatePath), _TRUNCATE,
            "%s\\%s", pathList[i], cmdName);
        if (verifyFileExecutable(candidatePath))
        {
            return _strdup(candidatePath);
        }
        if (!hasExt)
        {
            _snprintf_s(candidatePath, sizeof(candidatePath), _TRUNCATE,
                "%s\\%s.exe", pathList[i], cmdName);
            if (verifyFileExecutable(candidatePath))
            {
                return _strdup(candidatePath);
            }
        }
        i++;
    }

    return NULL;
}

static void performVariableExpansion(char** args)
{
    if (!args) return;

    int i = 0;
    while (args[i] != NULL)
    {
        char* orig = args[i];
        char* dollarPos = strchr(orig, '$');
        if (dollarPos != NULL)
        {
            char rebuildBuf[4096];
            rebuildBuf[0] = '\0';
            char* parsePos = orig;

            while ((dollarPos = strchr(parsePos, '$')) != NULL)
            {
                strncat_s(rebuildBuf, sizeof(rebuildBuf),
                    parsePos, (dollarPos - parsePos));
                dollarPos++;
                char varName[256];
                int varI = 0;
                while (*dollarPos && (isalnum((unsigned char)*dollarPos) ||
                    *dollarPos == '_') && varI < 255)
                {
                    varName[varI++] = *dollarPos;
                    dollarPos++;
                }
                varName[varI] = '\0';

                const char* val = getEnvironmentVariableValue(varName);
                if (val == NULL)
                {
                    val = "";
                }
                strncat_s(rebuildBuf, sizeof(rebuildBuf), val, _TRUNCATE);
                parsePos = dollarPos;
            }
            strncat_s(rebuildBuf, sizeof(rebuildBuf), parsePos, _TRUNCATE);

            free(args[i]);
            args[i] = _strdup(rebuildBuf);
            if (!args[i])
            {
                fprintf(stderr, "Memory allocation failed in performVariableExpansion\n");
                return; 
            }
        }
        i++;
    }
}

static char** splitLineIntoTokens(const char* line)
{
    if (!line) return NULL;

    char* copyLine = _strdup(line);
    if (!copyLine) return NULL;

    char** tokens = (char**)malloc(sizeof(char*) * MAX_ARGUMENTS);
    if (!tokens)
    {
        free(copyLine);
        return NULL;
    }

    int tokenCount = 0;

    {
        char* context = NULL;
         char* tok = strtok_s(copyLine, " \t\r\n", &context);
        while (tok != NULL && tokenCount < (MAX_ARGUMENTS - 1))
        {
            tokens[tokenCount] = _strdup(tok);
            if (!tokens[tokenCount])
            {
                
                for (int i = 0; i < tokenCount; i++) free(tokens[i]);
                free(tokens);
                free(copyLine);
                return NULL;
            }
            tokenCount++;
            tok = strtok_s(NULL, " \t\r\n", &context);
        }
        tokens[tokenCount] = NULL;
    }

    free(copyLine);
    return tokens;
}

static void freeTokens(char** tokens)
{
    if (!tokens) return;

    int i = 0;
    while (tokens[i] != NULL)
    {
        free(tokens[i]);
        i++;
    }
    free(tokens);
}

static void analyzeRedirectionAndBackground(char** args,
    CommandExecutionOptions* opts)
{
    if (!args || !opts) return;

    opts->inputFile = NULL;
    opts->outputFile = NULL;
    opts->runInBackground = 0;

    int countArgs = 0;
    while (args[countArgs] != NULL)
    {
        countArgs++;
    }

    int pos = countArgs - 1;
    while (pos >= 0)
    {
        if (strcmp(args[pos], "&") == 0)
        {
            opts->runInBackground = 1;
            free(args[pos]);
            args[pos] = NULL;
            pos--;
            continue;
        }

        if (strcmp(args[pos], ">") == 0 && pos + 1 < countArgs &&
            args[pos + 1] != NULL)
        {
            opts->outputFile = args[pos + 1];
            args[pos + 1] = NULL;
            free(args[pos]);
            args[pos] = NULL;
            pos -= 2;
            continue;
        }

        if (strcmp(args[pos], "<") == 0 && pos + 1 < countArgs &&
            args[pos + 1] != NULL)
        {
            opts->inputFile = args[pos + 1];
            args[pos + 1] = NULL;
            free(args[pos]);
            args[pos] = NULL;
            pos -= 2;
            continue;
        }

        pos--;
    }

    {
        char* tempArgs[MAX_ARGUMENTS];
        for (int i = 0; i < MAX_ARGUMENTS; i++) tempArgs[i] = NULL;

        int writeI = 0;
        int readI = 0;
        while (args[readI] != NULL)
        {
            tempArgs[writeI++] = args[readI];
            readI++;
        }
        tempArgs[writeI] = NULL;
        for (int i = 0; i < writeI; i++)
        {
            args[i] = tempArgs[i];
        }
        args[writeI] = NULL;
    }
}

static char*** splitByPipe(char** tokens)
{
    if (!tokens) return NULL;

    char*** cmds = (char***)malloc(sizeof(char**) * (MAX_PIPELINE_COMMANDS + 1));
    if (!cmds) return NULL;

    int cmdCount = 0;

    int startPos = 0;
    int i = 0;
    while (tokens[i] != NULL)
    {
        if (strcmp(tokens[i], "|") == 0)
        {
            tokens[i] = NULL;
            cmds[cmdCount] = &tokens[startPos];
            cmdCount++;
            startPos = i + 1;
        }
        i++;
    }
    cmds[cmdCount] = &tokens[startPos];
    cmdCount++;
    cmds[cmdCount] = NULL;
    return cmds;
}

static int runSingleCommand(char** args,
    CommandExecutionOptions* opts,
    char** pathList)
{
    if (!args || !args[0]) return EXIT_SUCCESS;

    if (_stricmp(args[0], "cd") == 0)
    {
        if (args[1] != NULL)
        {
            if (SetCurrentDirectoryA(args[1]) == 0)
            {
                fprintf(stderr, "cd: cannot change directory to %s\n", args[1]);
            }
        }
        else
        {
            fprintf(stderr, "cd: missing argument\n");
        }
        return EXIT_SUCCESS;
    }
    else if (_stricmp(args[0], "pwd") == 0)
    {
        char cwdBuf[1024];
        if (_getcwd(cwdBuf, sizeof(cwdBuf)) != NULL)
        {
            printf("%s\n", cwdBuf);
        }
        else
        {
            fprintf(stderr, "pwd: error getting current directory\n");
        }
        return EXIT_SUCCESS;
    }
    else if (_stricmp(args[0], "set") == 0)
    {
        if (args[1] != NULL && args[2] != NULL)
        {
            addEnvironmentVariable(args[1], args[2]);
        }
        else
        {
            fprintf(stderr, "set: usage: set NAME VALUE\n");
        }
        return EXIT_SUCCESS;
    }
    else if (_stricmp(args[0], "unset") == 0)
    {
        if (args[1] != NULL)
        {
            removeEnvironmentVariable(args[1]);
        }
        else
        {
            fprintf(stderr, "unset: usage: unset NAME\n");
        }
        return EXIT_SUCCESS;
    }
    else if (_stricmp(args[0], "echo") == 0)
    {
        int i = 1;
        while (args[i] != NULL)
        {
            if (i > 1)
            {
                printf(" ");
            }
            printf("%s", args[i]);
            i++;
        }
        printf("\n");
        return EXIT_SUCCESS;
    }

    char* cmdPath = locateCommandPath(args[0], pathList);
    if (!cmdPath)
    {
        fprintf(stderr, "%s: command not found\n", args[0]);
        return EXIT_FAILURE;
    }

    HANDLE inFileHandle = INVALID_HANDLE_VALUE;
    HANDLE outFileHandle = INVALID_HANDLE_VALUE;

    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    if (opts->inputFile != NULL)
    {
        inFileHandle = CreateFileA(opts->inputFile, READ_MODE,
            FILE_SHARE_FOR_READ, NULL,
            OPEN_EXISTING_FILE,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (inFileHandle == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "Failed to open input file: %s\n", opts->inputFile);
            free(cmdPath);
            return EXIT_FAILURE;
        }
        hIn = inFileHandle;
    }

    if (opts->outputFile != NULL)
    {
        outFileHandle = CreateFileA(opts->outputFile, WRITE_MODE, 0,
            NULL, CREATE_ALWAYS_FILE,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (outFileHandle == INVALID_HANDLE_VALUE)
        {
            fprintf(stderr, "Failed to open output file: %s\n",
                opts->outputFile);
            if (inFileHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(inFileHandle);
            }
            free(cmdPath);
            return EXIT_FAILURE;
        }
        hOut = outFileHandle;
    }

    char cmdline[4096];
    cmdline[0] = '\0';
    {
        int i = 0;
        while (args[i] != NULL)
        {
            if (i > 0)
            {
                strncat_s(cmdline, sizeof(cmdline), " ", _TRUNCATE);
            }
            strncat_s(cmdline, sizeof(cmdline), args[i], _TRUNCATE);
            i++;
        }
    }

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdInput = hIn;
    si.hStdOutput = hOut;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(pi));

    if (!CreateProcessA(cmdPath, cmdline, NULL, NULL, TRUE, 0, NULL, NULL,
        &si, &pi))
    {
        fprintf(stderr, "Failed to run command: %s\n", cmdline);
        if (inFileHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(inFileHandle);
        }
        if (outFileHandle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(outFileHandle);
        }
        free(cmdPath);
        return EXIT_FAILURE;
    }

    if (inFileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(inFileHandle);
    }
    if (outFileHandle != INVALID_HANDLE_VALUE)
    {
        CloseHandle(outFileHandle);
    }

    free(cmdPath);

    if (!opts->runInBackground)
    {
        WaitForSingleObject(pi.hProcess, INFINITE);
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    return EXIT_SUCCESS;
}

static int executePipeline(char*** cmds, char** pathList)
{
    if (!cmds) return EXIT_SUCCESS;

    int cmdCount = 0;
    while (cmds[cmdCount] != NULL)
    {
        cmdCount++;
    }

    if (cmdCount == 0) return EXIT_SUCCESS;

    if (cmdCount == 1)
    {
        CommandExecutionOptions opts;
        analyzeRedirectionAndBackground(cmds[0], &opts);
        performVariableExpansion(cmds[0]);
        return runSingleCommand(cmds[0], &opts, pathList);
    }

    int lastIdx = cmdCount - 1;

    int argCount = 0;
    while (cmds[lastIdx][argCount] != NULL)
    {
        argCount++;
    }

    char** lastCmdArgs = (char**)malloc(sizeof(char*) * (argCount + 1));
    if (!lastCmdArgs) return EXIT_FAILURE;
    {
        int i;
        for (i = 0; i < argCount; i++)
        {
            lastCmdArgs[i] = cmds[lastIdx][i];
        }
        lastCmdArgs[argCount] = NULL;
    }

    CommandExecutionOptions finalOpts;
    analyzeRedirectionAndBackground(lastCmdArgs, &finalOpts);

    {
        int i;
        for (i = 0; i < cmdCount; i++)
        {
            if (i == lastIdx)
            {
                performVariableExpansion(lastCmdArgs);
                cmds[i] = lastCmdArgs;
            }
            else
            {
                performVariableExpansion(cmds[i]);
            }
        }
    }

    ProcessInfo* procData = (ProcessInfo*)malloc(sizeof(ProcessInfo) * cmdCount);
    if (!procData) return EXIT_FAILURE;
    ZeroMemory(procData, sizeof(ProcessInfo) * cmdCount);

    HANDLE* pipeHandles = NULL;
    if (cmdCount > 1)
    {
        pipeHandles = (HANDLE*)malloc(sizeof(HANDLE) * 2 * (cmdCount - 1));
        if (!pipeHandles)
        {
            free(procData);
            return EXIT_FAILURE;
        }

        for (int px = 0; px < 2 * (cmdCount - 1); px++)
        {
            pipeHandles[px] = INVALID_HANDLE_VALUE;
        }

        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = NULL;
        sa.bInheritHandle = TRUE;

        for (int pipeI = 0; pipeI < cmdCount - 1; pipeI++)
        {
            if (!CreatePipe(&pipeHandles[2 * pipeI],
                &pipeHandles[2 * pipeI + 1], &sa, 0))
            {
                fprintf(stderr, "CreatePipe failed\n");
                free(procData);
                free(pipeHandles);
                return EXIT_FAILURE;
            }
            SetHandleInformation(pipeHandles[2 * pipeI + 1],
                HANDLE_FLAG_INHERIT,
                HANDLE_FLAG_INHERIT);
        }
    }

    for (int commandI = 0; commandI < cmdCount; commandI++)
    {
        char* cmdPath = locateCommandPath(cmds[commandI][0], pathList);
        if (!cmdPath)
        {
            fprintf(stderr, "%s: command not found\n", cmds[commandI][0]);

            if (pipeHandles != NULL)
            {
                for (int closeI = 0; closeI < 2 * (cmdCount - 1); closeI++)
                {
                    if (pipeHandles[closeI] != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(pipeHandles[closeI]);
                    }
                }
                free(pipeHandles);
            }

            free(procData);
            return EXIT_FAILURE;
        }

        HANDLE chosenIn = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE chosenOut = GetStdHandle(STD_OUTPUT_HANDLE);

        if (commandI > 0)
        {
            chosenIn = pipeHandles[2 * (commandI - 1)];
        }

        if (commandI < cmdCount - 1)
        {
            chosenOut = pipeHandles[2 * commandI + 1];
        }
        else
        {
            if (finalOpts.inputFile != NULL)
            {
                HANDLE customInFile = CreateFileA(
                    finalOpts.inputFile, READ_MODE, FILE_SHARE_FOR_READ,
                    NULL, OPEN_EXISTING_FILE, FILE_ATTRIBUTE_NORMAL, NULL
                );
                if (customInFile == INVALID_HANDLE_VALUE)
                {
                    fprintf(stderr, "Failed to open input file: %s\n",
                        finalOpts.inputFile);
                    free(cmdPath);

                    if (pipeHandles != NULL)
                    {
                        for (int pipeCloseI = 0; pipeCloseI < 2 * (cmdCount - 1);
                            pipeCloseI++)
                        {
                            if (pipeHandles[pipeCloseI]
                                != INVALID_HANDLE_VALUE)
                            {
                                CloseHandle(pipeHandles[pipeCloseI]);
                            }
                        }
                        free(pipeHandles);
                    }
                    free(procData);
                    return EXIT_FAILURE;
                }
                chosenIn = customInFile;
            }

            if (finalOpts.outputFile != NULL)
            {
                HANDLE customOutFile = CreateFileA(
                    finalOpts.outputFile, WRITE_MODE, 0, NULL,
                    CREATE_ALWAYS_FILE, FILE_ATTRIBUTE_NORMAL, NULL
                );
                if (customOutFile == INVALID_HANDLE_VALUE)
                {
                    fprintf(stderr, "Failed to open output file: %s\n",
                        finalOpts.outputFile);
                    free(cmdPath);

                    if (pipeHandles != NULL)
                    {
                        for (int pipeCloseI = 0; pipeCloseI < 2 * (cmdCount - 1);
                            pipeCloseI++)
                        {
                            if (pipeHandles[pipeCloseI]
                                != INVALID_HANDLE_VALUE)
                            {
                                CloseHandle(pipeHandles[pipeCloseI]);
                            }
                        }
                        free(pipeHandles);
                    }
                    free(procData);
                    return EXIT_FAILURE;
                }
                chosenOut = customOutFile;
            }
        }

        char assembledLine[4096];
        assembledLine[0] = '\0';
        {
            int i = 0;
            while (cmds[commandI][i] != NULL)
            {
                if (i > 0)
                {
                    strncat_s(assembledLine, sizeof(assembledLine), " ", _TRUNCATE);
                }
                strncat_s(assembledLine, sizeof(assembledLine), cmds[commandI][i], _TRUNCATE);
                i++;
            }
        }

        STARTUPINFOA si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdInput = chosenIn;
        si.hStdOutput = chosenOut;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES;
        ZeroMemory(&pi, sizeof(pi));

        if (!CreateProcessA(cmdPath, assembledLine, NULL, NULL, TRUE, 0,
            NULL, NULL, &si, &pi))
        {
            fprintf(stderr, "Failed to run command: %s\n", assembledLine);
            free(cmdPath);

            if (pipeHandles != NULL)
            {
                for (int pipeCloseI = 0; pipeCloseI < 2 * (cmdCount - 1);
                    pipeCloseI++)
                {
                    if (pipeHandles[pipeCloseI] != INVALID_HANDLE_VALUE)
                    {
                        CloseHandle(pipeHandles[pipeCloseI]);
                    }
                }
                free(pipeHandles);
            }
            free(procData);
            return EXIT_FAILURE;
        }

        free(cmdPath);
        procData[commandI].pi = pi;

        if (commandI > 0 && chosenIn != GetStdHandle(STD_INPUT_HANDLE))
        {
            CloseHandle(chosenIn);
        }
        if (commandI < cmdCount - 1 &&
            chosenOut != GetStdHandle(STD_OUTPUT_HANDLE))
        {
            CloseHandle(chosenOut);
        }
    }

    if (pipeHandles != NULL)
    {
        for (int pipeCloseI = 0; pipeCloseI < 2 * (cmdCount - 1); pipeCloseI++)
        {
            if (pipeHandles[pipeCloseI] != INVALID_HANDLE_VALUE)
            {
                CloseHandle(pipeHandles[pipeCloseI]);
            }
        }
        free(pipeHandles);
    }

    if (!finalOpts.runInBackground)
    {
        for (int waitI = 0; waitI < cmdCount; waitI++)
        {
            WaitForSingleObject(procData[waitI].pi.hProcess, INFINITE);
            CloseHandle(procData[waitI].pi.hThread);
            CloseHandle(procData[waitI].pi.hProcess);
        }
    }
    else
    {
        for (int handleCloseI = 0; handleCloseI < cmdCount; handleCloseI++)
        {
            CloseHandle(procData[handleCloseI].pi.hThread);
            CloseHandle(procData[handleCloseI].pi.hProcess);
        }
    }

    free(procData);
    return EXIT_SUCCESS;
}

void parseAndExecuteCommandPipeline(const char* inputLine, char** pathList)
{
    if (!inputLine) return;

    char** tokens = splitLineIntoTokens(inputLine);
    if (!tokens) return;

    if (tokens[0] == NULL)
    {
        freeTokens(tokens);
        return;
    }

    char*** cmdPipeline = splitByPipe(tokens);
    if (!cmdPipeline)
    {
        freeTokens(tokens);
        return;
    }

    int cmdCount = 0;
    while (cmdPipeline[cmdCount] != NULL)
    {
        cmdCount++;
    }

    executePipeline(cmdPipeline, pathList);

    freeTokens(tokens);
    free(cmdPipeline);
}
