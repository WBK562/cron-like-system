#ifndef LOGGER_H
#define LOGGER_H

enum log_level { MIN, STANDARD, MAX };

int logger_init(const char *filename, int sig_dump, int sig_toggle, int sig_level);
void logger_kill();
void logger_write(enum log_level LogLevel, char *format, ...);

#endif //LOGGER_H
