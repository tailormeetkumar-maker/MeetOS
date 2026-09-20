#ifndef ROGER_H
#define ROGER_H

void roger_init(void);
void roger_command(char *arguments);
void roger_exit(void);
void roger_poll(void);

int roger_is_active(void);
void roger_handle_line(char *line);
void roger_read_line(char *line);

#endif
