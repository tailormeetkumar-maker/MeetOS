#ifndef DNS_H
#define DNS_H

void dns_command(char *arguments);

int dns_resolve(
    const char *domain,
    unsigned char *answer_ip
);

#endif
