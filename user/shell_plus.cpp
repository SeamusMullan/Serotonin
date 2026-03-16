/**
 * @file shell_plus.cpp
 * @brief Enhanced command-line shell for Serotonin OS
 *
 * A feature-rich shell implementation using C++ STL (via STLport).
 * Features include:
 * - ANSI color-coded output
 * - Command history with !! and !n expansion
 * - Built-in commands (cd, ls, ps, env, etc.)
 * - Background process execution with &
 * - Quote-aware argument parsing
 * - PATH-based command search
 */

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdlib>

// System headers
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>

// Serotonin-specific
#include "syscall/lib5ht/lib5ht.h"

extern "C"
{
    int listdir(const char *path, char *buf, size_t size);
    int open(const char *path, int flags, ...);
}

// ANSI escape codes for colors
namespace Color
{
    const char *Reset = "\033[0m";
    const char *Bold = "\033[1m";
    const char *Red = "\033[31m";
    const char *Green = "\033[32m";
    const char *Yellow = "\033[33m";
    const char *Blue = "\033[34m";
    const char *Magenta = "\033[35m";
    const char *Cyan = "\033[36m";
    const char *White = "\033[37m";
    const char *BoldGreen = "\033[1;32m";
    const char *BoldCyan = "\033[1;36m";
    const char *BoldYellow = "\033[1;33m";
    const char *BoldRed = "\033[1;31m";
}

// Configuration
static const size_t MAX_HISTORY = 50;
static const size_t MAX_CMD_LEN = 512;

// Global state
static std::vector<std::string> g_history;
static std::string g_username = "user";
static std::string g_hostname = "serotonin";

// ============================================================================
// Helper functions
// ============================================================================

std::string get_cwd()
{
    char buf[256];
    if (getcwd(buf, sizeof(buf)))
    {
        return std::string(buf);
    }
    return "?";
}

std::string get_short_cwd()
{
    std::string cwd = get_cwd();
    // Replace home directory with ~
    if (cwd == "/home/" + g_username)
    {
        return "~";
    }
    if (cwd.find("/home/" + g_username + "/") == 0)
    {
        return "~" + cwd.substr(6 + g_username.length());
    }
    // Just return last component if path is long
    if (cwd.length() > 30)
    {
        size_t pos = cwd.rfind('/');
        if (pos != std::string::npos && pos > 0)
        {
            return ".../" + cwd.substr(pos + 1);
        }
    }
    return cwd;
}

void print_prompt()
{
    std::cout << Color::BoldGreen << g_username << "@" << g_hostname
              << Color::Reset << ":"
              << Color::BoldCyan << get_short_cwd()
              << Color::Reset << "$ ";
    std::cout.flush();
}

std::vector<std::string> tokenize(const std::string &cmd)
{
    std::vector<std::string> tokens;
    std::string current;
    bool in_quotes = false;
    char quote_char = 0;

    for (size_t i = 0; i < cmd.length(); ++i)
    {
        char c = cmd[i];

        if (in_quotes)
        {
            if (c == quote_char)
            {
                in_quotes = false;
            }
            else
            {
                current += c;
            }
        }
        else if (c == '"' || c == '\'')
        {
            in_quotes = true;
            quote_char = c;
        }
        else if (c == ' ' || c == '\t')
        {
            if (!current.empty())
            {
                tokens.push_back(current);
                current.clear();
            }
        }
        else
        {
            current += c;
        }
    }

    if (!current.empty())
    {
        tokens.push_back(current);
    }

    return tokens;
}

void add_to_history(const std::string &cmd)
{
    if (cmd.empty())
        return;
    if (!g_history.empty() && g_history.back() == cmd)
        return;

    g_history.push_back(cmd);
    if (g_history.size() > MAX_HISTORY)
    {
        g_history.erase(g_history.begin());
    }
}

// ============================================================================
// Built-in commands
// ============================================================================

void cmd_help()
{
    std::cout << Color::BoldYellow << "\n=== Shell Commands ===" << Color::Reset << "\n\n";

    std::cout << Color::BoldCyan << "Navigation:" << Color::Reset << "\n";
    std::cout << "  cd [dir]      Change directory (default: /)\n";
    std::cout << "  pwd           Print working directory\n";
    std::cout << "  ls [dir]      List directory contents\n";

    std::cout << Color::BoldCyan << "\nInformation:" << Color::Reset << "\n";
    std::cout << "  help          Show this help message\n";
    std::cout << "  ps            List running processes\n";
    std::cout << "  env           Show environment variables\n";
    std::cout << "  history       Show command history\n";
    std::cout << "  uname         Show system information\n";

    std::cout << Color::BoldCyan << "\nUtilities:" << Color::Reset << "\n";
    std::cout << "  echo [args]   Print arguments to stdout\n";
    std::cout << "  clear         Clear the screen\n";
    std::cout << "  cat [file]    Display file contents\n";

    std::cout << Color::BoldCyan << "\nShell:" << Color::Reset << "\n";
    std::cout << "  exit          Exit the shell\n";
    std::cout << "  !n            Execute command n from history\n";
    std::cout << "  !!            Execute last command\n";

    std::cout << Color::BoldCyan << "\nTips:" << Color::Reset << "\n";
    std::cout << "  - Append '&' to run commands in background\n";
    std::cout << "  - Use quotes for arguments with spaces\n";
    std::cout << "\n";
}

void cmd_pwd()
{
    std::cout << get_cwd() << "\n";
}

void cmd_cd(const std::vector<std::string> &args)
{
    const char *target = (args.size() > 1) ? args[1].c_str() : "/";
    if (chdir(target) != 0)
    {
        std::cout << Color::Red << "cd: " << target << ": No such directory"
                  << Color::Reset << "\n";
    }
}

void cmd_clear()
{
    std::cout << "\033[2J\033[H";
}

void cmd_echo(const std::vector<std::string> &args)
{
    for (size_t i = 1; i < args.size(); ++i)
    {
        if (i > 1)
            std::cout << " ";
        std::cout << args[i];
    }
    std::cout << "\n";
}

void cmd_history()
{
    std::cout << Color::BoldYellow << "Command History:" << Color::Reset << "\n";
    for (size_t i = 0; i < g_history.size(); ++i)
    {
        std::cout << Color::Cyan << "  " << (i + 1) << Color::Reset
                  << "  " << g_history[i] << "\n";
    }
}

void cmd_env(char **envp)
{
    std::cout << Color::BoldYellow << "Environment Variables:" << Color::Reset << "\n";
    for (char **e = envp; e && *e; ++e)
    {
        std::string var(*e);
        size_t eq = var.find('=');
        if (eq != std::string::npos)
        {
            std::cout << Color::Cyan << "  " << var.substr(0, eq)
                      << Color::Reset << "=" << var.substr(eq + 1) << "\n";
        }
    }
}

void cmd_uname()
{
    std::cout << Color::BoldYellow << "System Information:" << Color::Reset << "\n";
    std::cout << "  OS:       " << Color::Cyan << "Serotonin" << Color::Reset << "\n";
    std::cout << "  Kernel:   " << Color::Cyan << "5HT" << Color::Reset << "\n";
    std::cout << "  Arch:     " << Color::Cyan << "i686" << Color::Reset << "\n";
    std::cout << "  Shell:    " << Color::Cyan << "shell+" << Color::Reset << "\n";
}

void cmd_ps()
{
    proc_5ht_t procs[64];
    int count = sys_5ht_list_processes(procs, 64);

    if (count < 0)
    {
        std::cout << Color::Red << "ps: failed to list processes"
                  << Color::Reset << "\n";
        return;
    }

    std::cout << Color::BoldYellow;
    std::cout << "  PID   PRI  PRIV  NAME\n";
    std::cout << Color::Reset;
    std::cout << "  ----  ---  ----  ----\n";

    for (int i = 0; i < count; ++i)
    {
        std::cout << "  ";
        // PID (4 chars, right aligned)
        if (procs[i].pid < 10)
            std::cout << "   ";
        else if (procs[i].pid < 100)
            std::cout << "  ";
        else if (procs[i].pid < 1000)
            std::cout << " ";
        std::cout << procs[i].pid << "  ";

        // Priority (3 chars)
        if (procs[i].priority < 10)
            std::cout << "  ";
        else if (procs[i].priority < 100)
            std::cout << " ";
        std::cout << procs[i].priority << "  ";

        // Privilege (4 chars)
        if (!procs[i].priv)
        {
            std::cout << Color::BoldRed << "KERN" << Color::Reset;
        }
        else
        {
            std::cout << Color::Green << "USER" << Color::Reset;
        }
        std::cout << "  ";

        // Name
        std::cout << Color::Cyan << procs[i].name << Color::Reset << "\n";
    }

    std::cout << "\n"
              << Color::White << "Total: " << count << " processes"
              << Color::Reset << "\n";
}

void cmd_ls(const std::vector<std::string> &args)
{
    const char *path = (args.size() > 1) ? args[1].c_str() : ".";
    char buf[4096];

    errno = 0;
    int ret = listdir(path, buf, sizeof(buf));

    if (errno != 0)
    {
        std::cout << Color::Red << "ls: cannot access '" << path
                  << "'" << Color::Reset << "\n";
        return;
    }

    if (ret > 0)
    {
        // Parse and colorize output
        std::string output(buf, ret);
        size_t pos = 0;
        while (pos < output.length())
        {
            size_t end = output.find('\n', pos);
            if (end == std::string::npos)
                end = output.length();

            std::string entry = output.substr(pos, end - pos);

            // Color directories differently
            if (!entry.empty() && entry[entry.length() - 1] == '/')
            {
                std::cout << Color::BoldCyan << entry << Color::Reset << "\n";
            }
            else if (entry.find(".elf") != std::string::npos)
            {
                std::cout << Color::BoldGreen << entry << Color::Reset << "\n";
            }
            else
            {
                std::cout << entry << "\n";
            }

            pos = end + 1;
        }
    }
}

void cmd_cat(const std::vector<std::string> &args)
{
    if (args.size() < 2)
    {
        std::cout << Color::Red << "cat: missing file operand"
                  << Color::Reset << "\n";
        return;
    }

    int fd = open(args[1].c_str(), 0);
    if (fd < 0)
    {
        std::cout << Color::Red << "cat: " << args[1] << ": No such file"
                  << Color::Reset << "\n";
        return;
    }

    char buf[1024];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
    {
        write(1, buf, n);
    }

    close(fd);
}

// ============================================================================
// External command execution
// ============================================================================

void execute_external(const std::vector<std::string> &args, char **envp, bool background)
{
    pid_t pid = fork();

    if (pid < 0)
    {
        std::cout << Color::Red << "fork failed" << Color::Reset << "\n";
        return;
    }

    if (pid == 0)
    {
        // Child process

        // Convert args to char**
        std::vector<char *> argv;
        for (size_t i = 0; i < args.size(); ++i)
        {
            argv.push_back(const_cast<char *>(args[i].c_str()));
        }
        argv.push_back(nullptr);

        // Try to execute
        if (args[0].find('/') != std::string::npos)
        {
            // Absolute or relative path
            execve(args[0].c_str(), &argv[0], envp);
        }
        else
        {
            // Search PATH
            const char *path_env = nullptr;
            for (char **e = envp; e && *e; ++e)
            {
                if (strncmp(*e, "PATH=", 5) == 0)
                {
                    path_env = *e + 5;
                    break;
                }
            }
            if (!path_env)
                path_env = "/bin";

            std::string path_str(path_env);
            size_t pos = 0;
            while (pos < path_str.length())
            {
                size_t end = path_str.find(':', pos);
                if (end == std::string::npos)
                    end = path_str.length();

                std::string dir = path_str.substr(pos, end - pos);
                std::string candidate = dir + "/" + args[0];

                execve(candidate.c_str(), &argv[0], envp);

                pos = end + 1;
            }

            // Try current directory
            execve(args[0].c_str(), &argv[0], envp);
        }

        // If we get here, exec failed
        std::cout << Color::Red << args[0] << ": command not found"
                  << Color::Reset << "\n";
        _exit(127);
    }
    else
    {
        sys_5ht_set_fid(pid);
        // Parent process
        if (background)
        {
            std::cout << "[" << pid << "] Running in background\n";
        }
        else
        {
            int status;
            waitpid(pid, &status, 0);
        }
    }
}

// ============================================================================
// Main shell loop
// ============================================================================

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;

    // Get username from environment if available
    for (char **e = envp; e && *e; ++e)
    {
        if (strncmp(*e, "USER=", 5) == 0)
        {
            g_username = *e + 5;
            break;
        }
    }

    char input_buf[MAX_CMD_LEN];

    while (true)
    {
        print_prompt();

        ssize_t n = read(0, input_buf, sizeof(input_buf) - 1);

        if (n <= 0)
        {
            // EOF or error
            std::cout << "\nGoodbye!\n";
            break;
        }

        input_buf[n] = '\0';

        // Remove trailing newline
        if (n > 0 && input_buf[n - 1] == '\n')
        {
            input_buf[n - 1] = '\0';
            --n;
        }

        std::string cmd(input_buf);

        // Trim whitespace
        size_t start = cmd.find_first_not_of(" \t");
        if (start == std::string::npos)
            continue;
        size_t end = cmd.find_last_not_of(" \t");
        cmd = cmd.substr(start, end - start + 1);

        if (cmd.empty())
            continue;

        // Handle history expansion
        if (cmd[0] == '!')
        {
            if (cmd == "!!")
            {
                if (g_history.empty())
                {
                    std::cout << Color::Red << "No commands in history"
                              << Color::Reset << "\n";
                    continue;
                }
                cmd = g_history.back();
                std::cout << cmd << "\n";
            }
            else if (cmd.length() > 1)
            {
                int idx = atoi(cmd.c_str() + 1) - 1;
                if (idx >= 0 && (size_t)idx < g_history.size())
                {
                    cmd = g_history[idx];
                    std::cout << cmd << "\n";
                }
                else
                {
                    std::cout << Color::Red << "No such history entry"
                              << Color::Reset << "\n";
                    continue;
                }
            }
        }

        add_to_history(cmd);

        // Check for background execution
        bool background = false;
        if (!cmd.empty() && cmd[cmd.length() - 1] == '&')
        {
            background = true;
            cmd = cmd.substr(0, cmd.length() - 1);
            // Trim again
            end = cmd.find_last_not_of(" \t");
            if (end != std::string::npos)
            {
                cmd = cmd.substr(0, end + 1);
            }
        }

        std::vector<std::string> args = tokenize(cmd);
        if (args.empty())
            continue;

        const std::string &command = args[0];

        // Built-in commands
        if (command == "exit" || command == "quit")
        {
            std::cout << Color::Yellow << "Goodbye!" << Color::Reset << "\n";
            break;
        }
        else if (command == "help" || command == "?")
        {
            cmd_help();
        }
        else if (command == "pwd")
        {
            cmd_pwd();
        }
        else if (command == "cd")
        {
            cmd_cd(args);
        }
        else if (command == "clear" || command == "cls")
        {
            cmd_clear();
        }
        else if (command == "echo")
        {
            cmd_echo(args);
        }
        else if (command == "history")
        {
            cmd_history();
        }
        else if (command == "env")
        {
            cmd_env(envp);
        }
        else if (command == "uname")
        {
            cmd_uname();
        }
        else if (command == "ps")
        {
            cmd_ps();
        }
        else if (command == "ls")
        {
            cmd_ls(args);
        }
        else if (command == "cat")
        {
            cmd_cat(args);
        }
        else
        {
            // External command
            execute_external(args, envp, background);
        }
    }

    return 0;
}
