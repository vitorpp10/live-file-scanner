# live file scanner

so i built this c++ program to mess around with linux system calls and concurrency. basically, it runs in the background, watches the folder you are in, and automatically sorts new files into `SAFE` or `PERIGOSO` (dangerous) folders based on what they actually are, not just their extensions.

it's a fun experiment on how antiviruses or file monitors work under the hood.

## how it works

the code pieces together a few core linux concepts:

* **watching the folder (`inotify`):** there's a thread just using `inotify` to monitor the current directory. whenever a file is closed after writing (`IN_CLOSE_WRITE`), it grabs the filename and pushes it to a queue.
* **thread pool & thread-safe queue:** i made a custom thread-safe queue using `std::mutex` and `std::condition_variable`. a pool of worker threads constantly pulls filenames from this queue to inspect them.
* **reading magic numbers:** instead of trusting the file extension, the workers open the file and read the first 4 bytes (the magic numbers). if it spots `ELF` (linux executable), `MZ` (windows executable), or `#!` (shell script), it flags the file as dangerous.
* **pipes and forks for moving files:** the workers send the classification result (`SAFE` or `PERIGOSO`) through a pipe to a child process. this child process reads the pipe, creates the folders if they don't exist, and forks again to run the linux `mv` command via `execlp` to physically move the file.
* **graceful shutdown:** when you hit ctrl+c (`SIGINT`), a signal handler stops the loops safely. at the end, the program uses `getrusage` to print out the total execution time and the max memory (rss) it used.

## how to run

compile the code using g++ (c++11 or higher is needed for the threading stuff):

```
g++ -pthread main.cpp -o a && ./a
```
