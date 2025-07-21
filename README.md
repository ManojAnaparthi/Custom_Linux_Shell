# myshell

A simple custom Linux shell implemented in C.

## Features
- Command execution (absolute/relative paths)
- Built-in commands: `cd`, `exit`, `jobs`
- Foreground and background process management (`sleep 5 &`)
- Job table for background jobs, with automatic cleanup and notifications
- I/O redirection: `<`, `>`, `>>`
- Piped commands: `ls | grep txt | wc -l`
- Signal handling: `SIGINT` (Ctrl+C), `SIGTSTP` (Ctrl+Z), `SIGCHLD` (prevents zombies)
- Error handling for invalid commands and files

## Usage
1. Build:
   ```bash
   gcc -o myshell mysh.c
   ```
2. Run:
   ```bash
   ./myshell
   ```
3. Use like a normal shell:
   - Run commands: `ls`, `pwd`, `cat t1.txt`
   - Background jobs: `sleep 5 &`
   - View jobs: `jobs`
   - Redirection: `cat < t1.txt > t2.txt`
   - Piping: `ls | grep txt | wc -l`
   - Exit: `exit`

## Example Commands
```
ls
pwd
cd /tmp
cat < t1.txt > t2.txt
sleep 2 &
jobs
cat t1.txt | wc -l
```

## Known Bugs / Limitations
- No environment variable expansion (e.g., `$HOME` is not expanded)
- No command history or arrow key navigation
- No support for job control (fg/bg/kill)
- No tab completion
- No quoting or escaping (arguments split only by spaces)
- No support for multiple background pipelines in job table
- No support for exporting variables
- No support for advanced shell scripting features

## TODOs
- [ ] Add environment variable expansion
- [ ] Add command history and arrow key navigation
- [ ] Add job control (fg/bg/kill)
- [ ] Add tab completion
- [ ] Add support for quoting/escaping arguments
- [ ] Improve error messages and user feedback
- [ ] Add tests and example scripts

## Author
Manoj

---
This project is for educational purposes and demonstrates basic shell concepts in C.
