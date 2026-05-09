# live linux file scanner

this is a small c++ project i built to experiment with linux system calls, concurrency, and file monitoring.
the program runs in the background, watches the current directory, and automatically sorts new files into SAFE or PERIGOSO folders based on what the file actually is, not just the extension.
it's basically a small experiment to understand how file monitors or antiviruses might work internally.

## architecture

the program follows a simple producer-consumer model:

inotify thread (detects new files)
->
thread-safe queue
->
worker thread pool (analyzes files)
->
pipe communication
->
child process that moves files

## how it works

### watching the folder (inotify)
a dedicated thread uses linux inotify to monitor the current directory. whenever a file is closed after writing (IN_CLOSE_WRITE), the program detects it and pushes the filename into a queue.

### thread pool and thread-safe queue
i implemented a custom thread-safe queue using std::mutex and std::condition_variable. a pool of worker threads constantly pulls filenames from the queue and processes them.

### reading magic numbers
instead of trusting file extensions, workers open the file and read the first few bytes (magic numbers).
the program currently detects:

* ELF -> linux executables
* MZ -> windows executables
* #! -> shell scripts

for demonstration purposes these files are classified as "dangerous".

### pipes and forks for moving files
worker threads send the classification result (SAFE or PERIGOSO) through a pipe to a child process.
the child process reads the pipe, creates the folders if they do not exist, and forks again to execute the linux "mv" command using execlp to move the files.

### graceful shutdown
when ctrl+c (SIGINT) is triggered, a signal handler stops the loops safely.
at the end of execution the program prints some basic stats using getrusage:

* total execution time
* max memory usage (rss)

## how to run

compile with g++:
```
g++ -std=c++17 -pthread main.cpp -o scanner
```
